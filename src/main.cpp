#include <Arduino.h>
#include <Ticker.h>

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

#include <WiFiUdp.h>
#include <NTPClient.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Wire.h>
#include <RtcDS3231.h>

#define WIFI_SSID ""
#define WIFI_PWD ""

#define DHT_PWR 2
#define DHT_DATA 5
#define DHT_GND 15

#define DHTTYPE DHT22 /* DHT11, DHT22, DHT21 */
#define TICKER_PERIOD 60
#define TIME_ZONE 9
#define MAX_DATA_SIZE 1440

typedef struct
{
  char strTime[20];
  float temperature;
  float humidity;
} TempHumidity;

int dataIndex = 0;
TempHumidity data[MAX_DATA_SIZE];

bool tickerFlag = true;
Ticker ticker;
AsyncWebServer server(80);
DHT_Unified dht(DHT_DATA, DHTTYPE);
RtcDS3231<TwoWire> Rtc(Wire);

void tickerHandler(void);
void initWiFi(void);
void initRTC(void);
void initDHT(void);
void gatherData(void);
void initWebServer(void);
void notFound(AsyncWebServerRequest *request);
void sendCurrentTemp(AsyncWebServerRequest *request);
void sendDailyTemp(AsyncWebServerRequest *request);
void sendCurrentHumidity(AsyncWebServerRequest *request);
void sendDailyHumidity(AsyncWebServerRequest *request);

void setup()
{
  Serial.begin(115200);
  
  initWiFi();
  initRTC();
  initDHT();
  initWebServer();

  ticker.attach(TICKER_PERIOD, tickerHandler);
}

void loop()
{  
  if (tickerFlag)
  {
    tickerFlag = false;
    gatherData();    
  }
}

void tickerHandler()
{
  tickerFlag = true;
}

void initWiFi()
{
  Serial.println("Initialize WiFi...");

  if (!WiFi.begin(WIFI_SSID, WIFI_PWD))
  {
    Serial.println("ERROR: WiFi.begin");
    return;
  }

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("OK: WiFi connected");
  Serial.print("IP address: "); 
  Serial.println(WiFi.localIP());

  delay(1000);
}

void initRTC()
{
  WiFiUDP udp;
  NTPClient timeClient(udp, "pool.ntp.org", TIME_ZONE * 3600);
  
  Serial.println("Getting NTP time...");
  timeClient.update();

  /* 2000 - 1970 = 30 years = 946684800UL sec */
  unsigned long epochTime = timeClient.getEpochTime() - 946684800UL;
  
  Rtc.Begin();
  Rtc.SetDateTime(epochTime);

  if (!Rtc.GetIsRunning())
  {
    Rtc.SetIsRunning(true);
  }
}

void initDHT()
{ 
/* Power off & on DHT22 */  
  digitalWrite(DHT_PWR, HIGH);
  digitalWrite(DHT_GND, LOW);
  pinMode(DHT_PWR, OUTPUT);
  pinMode(DHT_GND, OUTPUT);  
  delay(2000);

  /* Initialize DHT22 */
  dht.begin();

  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.println("Temperature");
  Serial.print("Sensor:       ");
  Serial.println(sensor.name);
  
  dht.humidity().getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.println("Humidity");
  Serial.print("Sensor:       ");
  Serial.println(sensor.name);  
}

void gatherData()
{ 
  Serial.println("Gathering sensor data...");

  /* When array is full, remove oldest data */
  if(dataIndex >= MAX_DATA_SIZE)
  {
    for(int i = 1; i < MAX_DATA_SIZE; i++)
    {
      data[i-1] = data[i];
    }
    dataIndex = MAX_DATA_SIZE - 1;
  }
    
  sensors_event_t event;

  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) 
  {
    Serial.println("Temp. sensor data...FAILED");
    return;
  }
  data[dataIndex].temperature = event.temperature;

  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) 
  {
    Serial.println("Humidity sensor data...FAILED");
    return;
  }
  data[dataIndex].humidity = event.relative_humidity;

  if (!Rtc.IsDateTimeValid())
  {
    Serial.println("RTC data...FAILED");
    return;  
  } 
  RtcDateTime now = Rtc.GetDateTime();
  sprintf(data[dataIndex].strTime, "%04d-%02d-%02d %02d:%02d:%02d", //%d allows to print an integer to the string
        now.Year(),                            //get year method
        now.Month(),                           //get month method
        now.Day(),                             //get day method
        now.Hour(),                            //get hour method
        now.Minute(),                          //get minute method
        now.Second()                           //get second method
  );
  
  dataIndex++;  
  Serial.println("Gathering sensor data...OK");
}

void initWebServer()
{
  Serial.println("Initialize SPIFFS...");
  
  if (!SPIFFS.begin())
  {
    Serial.println("ERROR: SPIFFS.begin");
    return;
  }
      
  server.on("/currenttemp", HTTP_GET, sendCurrentTemp);
  server.on("/currenthumid", HTTP_GET, sendCurrentHumidity);  
  server.on("/dailytemp", HTTP_GET, sendDailyTemp);
  server.on("/dailyhumid", HTTP_GET, sendDailyHumidity);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false, nullptr);
  });

  server.serveStatic("/img", SPIFFS, "/img");  
  server.begin();
  
  Serial.println("Initialize Web Server...");  
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void sendCurrentTemp(AsyncWebServerRequest *request)
{
  Serial.println("http://[YOUR IP]/currenttemp is called");

  if (dataIndex > 0)
  {
    TempHumidity th = data[dataIndex - 1];

    String json = "{\"time\":\"" + String(th.strTime) + "\",";
    json += "\"temperature\":\"" + String(th.temperature, 1) + "\"}";

    request->send(200, "application/json", json);
  }
  else
  {
    request->send(500, "application/json", "");
  }
}

void sendCurrentHumidity(AsyncWebServerRequest *request)
{
  Serial.println("http://[YOUR IP]/currenthumid is called");

  if (dataIndex > 0)
  {
    TempHumidity th = data[dataIndex - 1];

    String json = "{\"time\":\"" + String(th.strTime) + "\",";  
    json += "\"humidity\":\"" + String(th.humidity, 1) + "\"}";

    request->send(200, "application/json", json);
  }
  else
  {
    request->send(500, "application/json", "");
  }
}

void sendDailyTemp(AsyncWebServerRequest *request)
{
  Serial.println("http://[YOUR IP]/dailytemp is called");

  if (dataIndex > 0)
  {
    String json = "[";
    for(int i = 0; i < dataIndex; i++)
    {
      TempHumidity th = data[i];
      json += "{\"time\":\"" + String(th.strTime) + "\",";
      json += "\"temperature\":\"" + String(th.temperature, 1) + "\"},";
    }
    json.remove(json.lastIndexOf(','));
    json += "]";
    
    request->send(200, "application/json", json);
  }
  else
  {
    request->send(500, "application/json", "");
  }
}

void sendDailyHumidity(AsyncWebServerRequest *request)
{
  Serial.println("http://[YOUR IP]/dailyhumid is called");
 
  if (dataIndex > 0)
  {
    String json = "[";
    for(int i = 0; i < dataIndex; i++)
    {
      TempHumidity th = data[i];
      json += "{\"time\":\"" + String(th.strTime) + "\",";
      json += "\"humidity\":\"" + String(th.humidity, 1) + "\"},";
    }
    json.remove(json.lastIndexOf(','));
    json += "]";

    request->send(200, "application/json", json);
  }
  else
  {
    request->send(500, "application/json", "");
  }
}