#include <Arduino.h>
#include "KMPProDinoESP32.h"
#include "KMPCommon.h"
#include "ArduinoJson.h"
#include "esp_adc_cal.h"

#include <WiFi.h>
#include <WiFiClient.h>

#define SSID_NAME "PeanutPay"
#define SSID_PASSWORD "PeanutPay"

EthernetClient ethClient;
WiFiClient wifiClient;

// Define text colors.
const char GREEN[] = "#90EE90"; // LightGreen
const char RED[] = "#FF4500";   // OrangeRed
const char GRAY[] = "#808080";

char deviceID[13];

#define PIN_ADC0 34
#define PIN_ADC1 35
#define PIN_ADC2 39
#define PIN_ADC3 36


uint32_t readPinCal(uint8_t pin)
{
  int adcResult = analogRead(pin);
  esp_adc_cal_characteristics_t adc_chars;
  
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  return esp_adc_cal_raw_to_voltage(adcResult, &adc_chars);
}

bool requestServer(Client *netClient, const char *server, const char *path, const char *payload, char *response, uint32_t timeout)
{
  if (netClient->connect(server, 80))
  {
    // build request;
    netClient->printf("POST %s HTTP/1.1\r\nHost: api.graviplant-online.de\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n\r\n%s\r\n", path, strlen(payload), payload);
    uint32_t startMillis = millis();
    while (!netClient->available() && (millis() - startMillis < timeout))
      ;

    if (!netClient->available())
    {
      netClient->stop();
      return false;
    }

    int i = 0;
    while (netClient->available())
    {
      response[i] = netClient->read();
      i++;
    }
    response[i] = 0;
    netClient->stop();

    // filter json output
    int start = 0;
    for (size_t c = 0; c < i; c++)
    {
      if (response[c] == '{')
      {
        start = c;
      }

      if (start)
      {
        response[c - start] = response[c];
      }

      if (response[c] == '}')
      {
        response[c - start + 1] = 0;
        start = 0;
      }
    }

    return true;
  }
  return false;
}

void setup()
{
  delay(5000);
  Serial.begin(115200);
  Serial.println("The example via WiFi and Ethernet is starting...");

  // Init Dino board.
  KMPProDinoESP32.begin(ProDino_ESP32_Ethernet);
  KMPProDinoESP32.setStatusLed(blue);

  // Reset Relay status.
  KMPProDinoESP32.setAllRelaysOff();

  // Connect to WiFi network
  WiFi.begin(SSID_NAME, SSID_PASSWORD);
  Serial.print("\n\rConnecting");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi IP: ");
  Serial.print(WiFi.localIP());

  uint8_t wifiMac[8];
  WiFi.macAddress(wifiMac);

  // bisschen cringe, aber ist ja auch schon 1:07
  sprintf(deviceID, "%02X%02X%02X%02X%02X%02X", wifiMac[0], wifiMac[1], wifiMac[2], wifiMac[3], wifiMac[4], wifiMac[5]);

  Serial.printf("Hallo, Ich bin: %s\r\n", deviceID);

  IPAddress ipa(100, 100, 100, 5);
  IPAddress dns(100, 100, 100, 1);
  IPAddress gat(100, 100, 100, 1);
  IPAddress nma(255, 255, 255, 0);

  // Start the Ethernet connection and the server.
  // Ethernet.begin(wifiMac, ipa, dns, gat, nma);
  if (Ethernet.begin(wifiMac) == 0)
  {
    Serial.println("Failed to configure Ethernet using DHCP");
    // no point in carrying on, so do nothing forevermore:
    while (1)
      ;
  }

  Serial.println("Ethernet IP:");
  Serial.print(Ethernet.localIP());

  KMPProDinoESP32.offStatusLed();
}
bool connOnce = false;

uint32_t pollMillis = 0;
uint32_t telemetryMillis = 0;
uint32_t actionMillis = 0;
char respBuff[1000];
char reqBuff[1000];

Client *currentClient = &wifiClient;

typedef enum{
  ACT_IDLE,
  ACT_PREPARED,
  ACT_STARTING,
  ACT_RUNNING,
  ACT_STOPPING
} actionState;

bool pendingAction = false;
int actionID = -1;
int action = -1;
actionState cActionState;



void loop()
{
  if ((millis() - pollMillis > 30000) && !pendingAction)
  {
    pollMillis = millis();
    sprintf(reqBuff, "mac=%s", deviceID);
    if (requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actions/", reqBuff, respBuff, 2000))
    {
      Serial.println("Connection Succeeded");
      StaticJsonDocument<1000> myDoc;
      Serial.println(respBuff);
      if (deserializeJson(myDoc, respBuff) == DeserializationError::Ok)
      {
        Serial.println("Valid JSON!");
        if(myDoc.containsKey("actionid"))
        {
          //valid response
          actionID = myDoc["actionid"];
          action = myDoc["action"];
          if(actionID != -1)
          {
            pendingAction = true;
            Serial.println("Pending Action!");
          }
        }
      }
    }
    else
    {
      Serial.println("Connection Failed");
    }
  }

  if (millis() - telemetryMillis > (10 * 1000))
  {
    telemetryMillis = millis();
    sprintf(reqBuff, "mac=%s&a0=%d&a1=%d&a2=%d&a3=%d", deviceID, readPinCal(PIN_ADC0), readPinCal(PIN_ADC1), readPinCal(PIN_ADC2), readPinCal(PIN_ADC3));
    if (requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/telemetry/", reqBuff, respBuff, 2000))
    {
      Serial.println("Connection Succeeded");
      StaticJsonDocument<1000> myDoc;
      Serial.println(respBuff);
      if (deserializeJson(myDoc, respBuff) == DeserializationError::Ok)
      {
        String erg = myDoc["result"];
        if(erg == "OK")
        {
          Serial.println("Transmit Success");
        }
      }
    }
    else
    {
      Serial.println("Connection Failed");
    }
  }


  switch (cActionState)
  {
  case ACT_IDLE:
    if(pendingAction)
      cActionState = ACT_PREPARED;
    break;

  case ACT_PREPARED:
    sprintf(reqBuff, "mac=%s&action=%d&value=%d", deviceID, actionID, 0);
    requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);
    Serial.println(respBuff);
    cActionState = ACT_STARTING;
    actionMillis = millis();
    break;

  case ACT_STARTING:
    if(millis() - actionMillis > 1000)
    {
      sprintf(reqBuff, "mac=%s&action=%d&value=%d", deviceID, actionID, 1);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);
      cActionState = ACT_RUNNING;
      actionMillis = millis();
    }    
    break;

  case ACT_RUNNING:
    if(millis() - actionMillis > action*1000)
    {
      sprintf(reqBuff, "mac=%s&action=%d&value=%d", deviceID, actionID, 1);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);
      cActionState = ACT_STOPPING;
      actionMillis = millis();
    }    
    break;

  case ACT_STOPPING:
    if(millis() - actionMillis > 1000)
    {
      sprintf(reqBuff, "mac=%s&action=%d&value=%d", deviceID, actionID, 0);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);
      cActionState = ACT_IDLE;
      actionMillis = millis();
      pendingAction = false;
    }    
    break;
  
  default:
    cActionState = ACT_IDLE;
    break;
  }

  Ethernet.maintain();
  // put your main code here, to run repeatedly:
}

// put function definitions here:
int myFunction(int x, int y)
{
  return x + y;
}