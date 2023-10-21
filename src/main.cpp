#include <Arduino.h>
#include "KMPProDinoESP32.h"
#include "KMPCommon.h"
#include "ArduinoJson.h"
#include "esp_adc_cal.h"
#include "website.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <WebServer.h>

#define FW_VERS "1.0.0"

#define SSID_NAME "PeanutPay"
#define SSID_PASSWORD "PeanutPay"

EthernetClient ethClient;
WiFiClient wifiClient;

// Define text colors.
const char GREEN[] = "#90EE90"; // LightGreen
const char RED[] = "#FF4500";   // OrangeRed
const char GRAY[] = "#808080";

const char *serverStateStr = "keine Verbindung";

char deviceID[13];
uint8_t wifiMac[8];

#define PIN_ADC0 34
#define PIN_ADC1 35
#define PIN_ADC2 39
#define PIN_ADC3 36

IPAddress apIP(8, 8, 8, 8);
IPAddress netMsk(255, 255, 255, 0);

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer webServer;

char sensorDataBuff[1024] = {0};

void updateSensorData();

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

/** Is this an IP? */
boolean isIp(String str)
{
  for (int i = 0; i < str.length(); i++)
  {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9'))
    {
      return false;
    }
  }
  return true;
}

/** IP to String? */
String toStringIp(IPAddress ip)
{
  String res = "";
  for (int i = 0; i < 3; i++)
  {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

boolean captivePortal()
{
  if (!isIp(webServer.hostHeader()))
  {
    Serial.println("Request redirected to captive portal");
    webServer.sendHeader("Location", String("http://") + toStringIp(webServer.client().localIP()), true);
    webServer.send(302, "text/plain", "");
    webServer.client().stop();
    return true;
  }
  return false;
}

char pageBuff[1024];


void applyConfig()
{
  if(webServer.arg("type") == "wifi")
  {
    Serial.println("Wifi Config!");
    Serial.printf("SSID: %s\r\n", webServer.arg("s").c_str());
    Serial.printf("PWD: %s\r\n", webServer.arg("m").c_str());
    WiFi.begin(webServer.arg("s"), webServer.arg("m"));
  }

  if(webServer.arg("type") == "eth")
  {
    Serial.println("Ethernet DHCP Config!");
    Ethernet.begin(wifiMac, 10000);
  }

  if(webServer.arg("type") == "ethman")
  {
    Serial.println("Ethernet manual Config!");
    IPAddress ip, gateway, dns, netmask;

    if(!ip.fromString(webServer.arg("ip")))
    {
      webServer.sendContent("<h2>IP Adresse fehlerhaft</h2>");
      return;
    }

    if(!gateway.fromString(webServer.arg("ip")))
    {
      webServer.sendContent("<h2>Gateway Adresse fehlerhaft</h2>");
      return;
    }

    if(!dns.fromString(webServer.arg("ip")))
    {
      webServer.sendContent("<h2>DNS Adresse fehlerhaft</h2>");
      return;
    }

    if(!netmask.fromString(webServer.arg("ip")))
    {
      webServer.sendContent("<h2>Netmask fehlerhaft</h2>");
      return;
    }
    
    Ethernet.begin(wifiMac, ip, dns, gateway, netmask);
  }
  
}

void handleRoot()
{
  if (captivePortal())
  {
    return;
  }
  

  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");


  webServer.sendContent(webpageHeader1);
  webServer.sendContent("Steuerbox Config");
  webServer.sendContent(webpageHeader2);

  sprintf(pageBuff, R"EOF(
<nav>
<b>Steuerbox</b>
%s
</nav>
<div>
<h1>Konfiguration</h1>
<button onclick="window.location.href='/config';">
Netzwerkeinstellungen
</button>)EOF",
          deviceID);
  webServer.sendContent(pageBuff);

  if(webServer.method() == HTTPMethod::HTTP_POST)
  {
    applyConfig();
  }


  webServer.sendContent(webpageDataTable);
  webServer.sendContent(webpageTableUpdateScript);
  webServer.sendContent("</div>");
  webServer.sendContent(webpageFooter);
}

void handleConfig()
{
  if (captivePortal())
  {
    return;
  }
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");


  webServer.sendContent(webpageHeader1);
  webServer.sendContent("Steuerbox Config");
  webServer.sendContent(webpageHeader2);

  sprintf(pageBuff, R"EOF(
<nav>
<b>Netzwerkeinstellungen</b>
%s
</nav>
<div>
<h1>Config</h1>
<button onclick="window.location.href='/config_wifi';">
WiFi
</button><br>
<button onclick="window.location.href='/config_eth_dhcp';">
Ethernet DHCP
</button><br>
<button onclick="window.location.href='/config_eth_man';">
Ethernet manuell
</button><br>
<button onclick="window.location.href='/';">
&lt-Zurueck
</button><br>
)EOF",
          deviceID);
  webServer.sendContent(pageBuff);
  webServer.sendContent("</div>");
  webServer.sendContent(webpageFooter);
}

void handleConfigWiFi()
{
  if (captivePortal())
  {
    return;
  }
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");


  webServer.sendContent(webpageHeader1);
  webServer.sendContent("Steuerbox Config");
  webServer.sendContent(webpageHeader2);

  sprintf(pageBuff, R"EOF(
<nav>
<b>Netzwerkeinstellungen</b>
%s
</nav>
<div>
<h1>WiFi</h1>
<form action=/ method=post>
<input type="hidden" name="type" value="wifi">
<label>WiFi SSID:</label><input type=text name=s></input>
<label>WiFi PWD:</label><input type=password name=m></input>
<input type=submit value=Verbinden>
</form><br><br>
<button onclick="window.location.href='/config';">
&lt-Zurueck
</button><br>
)EOF",
          deviceID);
  webServer.sendContent(pageBuff);
  webServer.sendContent("</div>");
  webServer.sendContent(webpageFooter);
}

void handleConfigEthDHCP()
{
  if (captivePortal())
  {
    return;
  }
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");


  webServer.sendContent(webpageHeader1);
  webServer.sendContent("Steuerbox Config");
  webServer.sendContent(webpageHeader2);

  sprintf(pageBuff, R"EOF(
<nav>
<b>Netzwerkeinstellungen</b>
%s
</nav>
<div>
<h1>Ethernet DHCP</h1>
<form action=/ method=post>
<input type="hidden" name="type" value="eth">
<input type=submit value=Verbinden>
</form><br><br>
<button onclick="window.location.href='/config';">
&lt-Zurueck
</button><br>
)EOF",
          deviceID);
  webServer.sendContent(pageBuff);
  webServer.sendContent("</div>");
  webServer.sendContent(webpageFooter);
}

void handleConfigEthMan()
{
  if (captivePortal())
  {
    return;
  }
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");


  webServer.sendContent(webpageHeader1);
  webServer.sendContent("Steuerbox Config");
  webServer.sendContent(webpageHeader2);

  sprintf(pageBuff, R"EOF(
<nav>
<b>Netzwerkeinstellungen</b>
%s
</nav>
<div>
<h1>WiFi</h1>
<form action=/ method=post>
<input type="hidden" name="type" value="ethman">
<label>IP:</label><input type=text name=ip></input>
<label>Gateway:</label><input type=text name=gw></input>
<label>DNS:</label><input type=text name=dns></input>
<label>Netmask:</label><input type=text name=nm></input>
<input type=submit value=Verbinden>
</form><br><br>
<button onclick="window.location.href='/config';">
&lt-Zurueck
</button><br>
)EOF",
          deviceID);
  webServer.sendContent(pageBuff);
  webServer.sendContent("</div>");
  webServer.sendContent(webpageFooter);
}

void dataRequestHandler()
{
  if (captivePortal())
  {
    return;
  }
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.send(200, "application/json", sensorDataBuff);
}

void handleNotFound()
{
  if (captivePortal())
  {
    return;
  }
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");

  String p;
  p += F(
      "<html><head></head><body>"
      "<h1>Not found!!</h1>");
  p += F("</body></html>");

  webServer.send(404, "text/html", p);
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

  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(("VV_SB_" + String(deviceID)).c_str(), "visioverdis");

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  webServer.on("/", handleRoot);
  webServer.on("/config", handleConfig);
  webServer.on("/config_wifi", handleConfigWiFi);
  webServer.on("/config_eth_dhcp", handleConfigEthDHCP);
  webServer.on("/config_eth_man", handleConfigEthMan);
  webServer.on("/data", dataRequestHandler);
  webServer.on("/generate_204", handleRoot);
  webServer.onNotFound(handleNotFound);
  webServer.begin(); // Web server start
  Serial.println("HTTP server started");

  IPAddress ipa(100, 100, 100, 5);
  IPAddress dns(100, 100, 100, 1);
  IPAddress gat(100, 100, 100, 1);
  IPAddress nma(255, 255, 255, 0);

  // Start the Ethernet connection and the server.
  // Ethernet.begin(wifiMac, ipa, dns, gat, nma);
  if (Ethernet.begin(wifiMac, 10000) == 0)
  {
    Serial.println("Failed to configure Ethernet using DHCP");
  }

  Serial.println("Ethernet IP:");
  Serial.print(Ethernet.localIP());

  KMPProDinoESP32.offStatusLed();
}
bool connOnce = false;

uint32_t pollMillis = 0;
uint32_t telemetryMillis = 0;
uint32_t actionMillis = 0;
uint32_t updateSensorMillis = 0;
char respBuff[1000];
char reqBuff[1000];

Client *currentClient = &wifiClient;

typedef enum
{
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
        if (myDoc.containsKey("actionid"))
        {
          // valid response
          actionID = myDoc["actionid"];
          action = myDoc["action"];
          if (actionID != -1)
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
        if (erg == "OK")
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

  if (millis() - updateSensorMillis > (1000))
  {
    updateSensorMillis = millis();
    updateSensorData();
  }

  switch (cActionState)
  {
  case ACT_IDLE:
    if (pendingAction)
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
    if (millis() - actionMillis > 1000)
    {
      sprintf(reqBuff, "mac=%s&action=%d&value=%d", deviceID, actionID, 1);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);
      cActionState = ACT_RUNNING;
      actionMillis = millis();
    }
    break;

  case ACT_RUNNING:
    if (millis() - actionMillis > action * 1000)
    {
      sprintf(reqBuff, "mac=%s&action=%d&value=%d", deviceID, actionID, 1);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);
      cActionState = ACT_STOPPING;
      actionMillis = millis();
    }
    break;

  case ACT_STOPPING:
    if (millis() - actionMillis > 1000)
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

  // Ethernet.maintain();
  dnsServer.processNextRequest();
  webServer.handleClient();
  // put your main code here, to run repeatedly:
}

void updateSensorData()
{

  int32_t ain0, ain1, ain2, ain3;

  ain0 = readPinCal(PIN_ADC0);
  ain1 = readPinCal(PIN_ADC1);
  ain2 = readPinCal(PIN_ADC2);
  ain3 = readPinCal(PIN_ADC3);

  uint8_t sout0, sout1;

  sout0 = 0;
  sout1 = 0;

  sprintf(sensorDataBuff, R"EOF({
"hwID":"%s",
"serverstate":"%s",
"wifissid":"%s",
"wifiip":"%s",
"wifistat_x":"%d",
"ethip":"%s",
"ethstate_y":"%d",
"AIN0_mV":%d,
"AIN1_mV":%d,
"AIN2_mV":%d,
"AIN3_mV":%d,
"OUT0_s":%d,
"OUT1_s":%d,
"fwvers":"%s"
})EOF",
          deviceID,
          serverStateStr,
          WiFi.SSID().c_str(),
          WiFi.localIP().toString().c_str(),
          WiFi.status(),
          Ethernet.localIP().toString().c_str(),
          Ethernet.linkStatus(),
          ain0,
          ain1,
          ain2,
          ain3,
          sout0,
          sout1,
          FW_VERS);
}