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

#include <EEPROM.h>

#define FW_VERS "1.0.2"

#define SSID_NAME "PeanutPay"
#define SSID_PASSWORD "PeanutPay"

EthernetClient ethClient;
WiFiClient wifiClient;

enum connectionType : uint8_t
{
  conn_none,
  conn_wifi,
  conn_eth_dhcp,
  conn_eth_man
};

typedef struct __packed
{
  enum connectionType type;

  union
  {
    struct
    {
      char ssid[64];
      char pwd[64];
    } wifiConfig;

    struct
    {
      uint32_t ip;
      uint32_t gw;
      uint32_t dns;
      uint32_t nm;
    } ethConfig;
  };

} networkConfiguration;

// Define text colors.
const char GREEN[] = "#90EE90"; // LightGreen
const char RED[] = "#FF4500";   // OrangeRed
const char GRAY[] = "#808080";

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

networkConfiguration netConfig;

void updateSensorData();

uint32_t adc0_filt = 0;
uint32_t adc1_filt = 0;
uint32_t adc2_filt = 0;
uint32_t adc3_filt = 0;

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
  if (webServer.arg("type") == "wifi")
  {
    Serial.println("Wifi Config!");

    netConfig.type = conn_wifi;
    strcpy(netConfig.wifiConfig.ssid, webServer.arg("s").c_str());
    strcpy(netConfig.wifiConfig.pwd, webServer.arg("m").c_str());

    Serial.printf("SSID: %s\r\n", netConfig.wifiConfig.ssid);
    Serial.printf("PWD: %s\r\n", netConfig.wifiConfig.pwd);
    WiFi.begin(netConfig.wifiConfig.ssid, netConfig.wifiConfig.pwd);

    if (EEPROM.begin(sizeof(netConfig)))
    {
      EEPROM.put<networkConfiguration>(0, netConfig);
      EEPROM.end();
    }
  }

  if (webServer.arg("type") == "eth")
  {
    Serial.println("Ethernet DHCP Config!");
    Ethernet.begin(wifiMac, 10000);
    netConfig.type = conn_eth_dhcp;
    if (EEPROM.begin(sizeof(netConfig)))
    {
      EEPROM.put<networkConfiguration>(0, netConfig);
      EEPROM.end();
    }
  }

  if (webServer.arg("type") == "ethman")
  {
    Serial.println("Ethernet manual Config!");
    IPAddress ip, gateway, dns, netmask;

    if (!ip.fromString(webServer.arg("ip")))
    {
      webServer.sendContent("<h2>IP Adresse fehlerhaft</h2>");
      return;
    }

    if (!gateway.fromString(webServer.arg("ip")))
    {
      webServer.sendContent("<h2>Gateway Adresse fehlerhaft</h2>");
      return;
    }

    if (!dns.fromString(webServer.arg("ip")))
    {
      webServer.sendContent("<h2>DNS Adresse fehlerhaft</h2>");
      return;
    }

    if (!netmask.fromString(webServer.arg("ip")))
    {
      webServer.sendContent("<h2>Netmask fehlerhaft</h2>");
      return;
    }

    netConfig.type = conn_eth_man;
    netConfig.ethConfig.ip = ip;
    netConfig.ethConfig.dns = dns;
    netConfig.ethConfig.gw = gateway;
    netConfig.ethConfig.nm = netmask;
    if (EEPROM.begin(sizeof(netConfig)))
    {
      EEPROM.put<networkConfiguration>(0, netConfig);
      EEPROM.end();
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

  if (webServer.method() == HTTPMethod::HTTP_POST)
  {
    applyConfig();
  }

  webServer.sendContent(webpageDataTable);

  webServer.sendContent(R"EOF(
<button onclick="window.location.href='/disable';">
AP deaktivieren
</button>)EOF");

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

uint8_t apDisabled = 1;

void handleDisable()
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
<h1>Accesspoint deaktiviert!</h1><br>
<p>Bis Bald!</p>
)EOF",
          deviceID);
  webServer.sendContent(pageBuff);
  webServer.sendContent("</div>");
  webServer.sendContent(webpageFooter);

  WiFi.softAPdisconnect(true);
  apDisabled = 1;
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

uint32_t getIOState(uint32_t channel)
{
  if (KMPProDinoESP32.getRelayState(channel))
  {
    return 1;
  }
  return 0;
}

void setup()
{
  delay(3000);
  Serial.begin(115200);
  Serial.printf("Steuerbox FW: %s\r\n", FW_VERS);

  // Init Dino board.
  KMPProDinoESP32.begin(ProDino_ESP32_Ethernet);
  KMPProDinoESP32.setStatusLed(blue);

  // Reset Relay status.
  KMPProDinoESP32.setAllRelaysOff();

  // Connect to WiFi network
  WiFi.begin("VisioVerdis", "visioverdis");

  uint8_t wifiMac[8];
  WiFi.macAddress(wifiMac);

  // bisschen cringe, aber ist ja auch schon 1:07
  sprintf(deviceID, "%02X%02X%02X%02X%02X%02X", wifiMac[0], wifiMac[1], wifiMac[2], wifiMac[3], wifiMac[4], wifiMac[5]);

  Serial.printf("Hallo, Ich bin: %s\r\n", deviceID);

  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAPdisconnect(true);
  WiFi.setAutoReconnect(true);

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  webServer.on("/", handleRoot);
  webServer.on("/disable", handleDisable);
  webServer.on("/config", handleConfig);
  webServer.on("/config_wifi", handleConfigWiFi);
  webServer.on("/config_eth_dhcp", handleConfigEthDHCP);
  webServer.on("/config_eth_man", handleConfigEthMan);
  webServer.on("/data", dataRequestHandler);
  webServer.on("/generate_204", handleRoot);
  webServer.onNotFound(handleNotFound);
  webServer.begin(); // Web server start
  Serial.println("HTTP server started");

  if (EEPROM.begin(sizeof(netConfig)))
  {
    EEPROM.get<networkConfiguration>(0, netConfig);
    EEPROM.end();
  }

  if (netConfig.type == conn_wifi)
  {
    Serial.println("WiFi Config");
    WiFi.begin(netConfig.wifiConfig.ssid, netConfig.wifiConfig.pwd);
  }
  else if (netConfig.type == conn_eth_dhcp)
  {
    Serial.println("DHCP Config");
    if (Ethernet.begin(wifiMac, 10000) == 0)
    {
      Serial.println("Failed to configure Ethernet using DHCP");
    }
    Serial.println("Ethernet IP:");
    Serial.print(Ethernet.localIP());
  }
  else if (netConfig.type == conn_eth_man)
  {
    Serial.println("Eth Man Config");
    Ethernet.begin(wifiMac, IPAddress(netConfig.ethConfig.ip), IPAddress(netConfig.ethConfig.dns), IPAddress(netConfig.ethConfig.gw), IPAddress(netConfig.ethConfig.nm));
    Serial.println("Ethernet IP:");
    Serial.print(Ethernet.localIP());
  }
  else
  {
    Serial.println("No Config");
    WiFi.softAP(("VV_SB_" + String(deviceID)).c_str(), "visioverdis");
  }

  KMPProDinoESP32.setStatusLed(red);
}
bool connOnce = false;

uint32_t pollMillis = 0;
uint32_t lastServerOnline = 0;
uint32_t telemetryMillis = 0;
uint32_t actionMillis = 0;
uint32_t updateSensorMillis = 0;
char respBuff[1000];
char reqBuff[1000];

uint8_t serverStatus = 0;

Client *currentClient = nullptr;

typedef enum
{
  ACT_IDLE,
  ACT_START_CHANGE,
  ACT_CHANGE,
  ACT_START_PULSE,
  ACT_PULSE_HIGH,
  ACT_PULSE_HOLD,
  ACT_PULSE_LOW
} actionState;

bool pendingAction = false;
int actionCH = -1;
int actionType = -1;
int actionValue = -1;
actionState cActionState;

void loop()
{
  currentClient = nullptr;
  if (netConfig.type == conn_wifi)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      currentClient = &wifiClient;
    }
    else
    {
      serverStatus = 0;
      KMPProDinoESP32.setStatusLed(red);
    }
  }

  if (netConfig.type == conn_eth_dhcp || netConfig.type == conn_eth_man)
  {
    if (Ethernet.linkStatus() == LinkON)
    {
      currentClient = &ethClient;
    }
    else
    {
      serverStatus = 0;
      KMPProDinoESP32.setStatusLed(red);
    }
  }

  if ((millis() - pollMillis > 30 * 1000) && !pendingAction && currentClient)
  {
    serverStatus = 0;
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
        if (myDoc.containsKey("channel"))
        {
          serverStatus = 1;
          KMPProDinoESP32.setStatusLed(green);
          lastServerOnline = millis();
          // valid response
          actionCH = myDoc["channel"];
          actionType = myDoc["action_type"];
          actionValue = myDoc["action_value"];
          if (actionType != -1)
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
      KMPProDinoESP32.setStatusLed(red);
    }
  }

  if ((millis() - telemetryMillis > (60 * 1000)) && currentClient)
  {
    serverStatus = 0;
    telemetryMillis = millis();
    sprintf(reqBuff, "mac=%s&a0=%d&a1=%d&a2=%d&a3=%d", deviceID, adc0_filt, adc1_filt, adc2_filt, adc3_filt);
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
          serverStatus = 1;
          KMPProDinoESP32.setStatusLed(green);
          lastServerOnline = millis();
          Serial.println("Transmit Success");
        }
      }
    }
    else
    {
      Serial.println("Connection Failed");
      KMPProDinoESP32.setStatusLed(red);
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
    {
      if (actionCH < 4 && actionCH >= 0)
      {
        if (actionType == 0)
        {
          cActionState = ACT_START_PULSE;
        }
        if (actionType == 1)
        {
          cActionState = ACT_START_CHANGE;
        }
        if (actionType == 99)
        {
          // recovery mode
          if (apDisabled)
          {
            apDisabled = 0;
            Serial.println("Starting Recovery Mode!");
            WiFi.softAP(("VV_SB_" + String(deviceID)).c_str(), "visioverdis");
          }
        }
      }

      pendingAction = false;
      actionMillis = millis();
    }

    break;

  case ACT_START_PULSE:
    if (millis() - actionMillis > 1000)
    {
      actionMillis = millis();

      sprintf(reqBuff, "mac=%s&channel=%d&value=%d", deviceID, actionCH, 0);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);

      Serial.println(respBuff);

      cActionState = ACT_PULSE_HIGH;
    }

    break;

  case ACT_PULSE_HIGH:
    if (millis() - actionMillis > 1000)
    {
      actionMillis = millis();
      KMPProDinoESP32.setRelayState(actionCH, 1);

      sprintf(reqBuff, "mac=%s&channel=%d&value=%d", deviceID, actionCH, 1);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);

      Serial.println(respBuff);

      cActionState = ACT_PULSE_HOLD;
    }
    break;

  case ACT_PULSE_HOLD:
    if (millis() - actionMillis > actionValue * 1000)
    {
      actionMillis = millis();

      sprintf(reqBuff, "mac=%s&channel=%d&value=%d", deviceID, actionCH, 1);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);

      Serial.println(respBuff);

      cActionState = ACT_PULSE_LOW;
    }
    break;

  case ACT_PULSE_LOW:
    if (millis() - actionMillis > 1000)
    {
      actionMillis = millis();

      sprintf(reqBuff, "mac=%s&channel=%d&value=%d", deviceID, actionCH, 0);
      KMPProDinoESP32.setRelayState(actionCH, 0);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);

      Serial.println(respBuff);

      cActionState = ACT_IDLE;
    }
    break;

  case ACT_START_CHANGE:
    if (millis() - actionMillis > 1000)
    {
      actionMillis = millis();
      
      sprintf(reqBuff, "mac=%s&channel=%d&value=%d", deviceID, actionCH, getIOState(actionCH));
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);
      
      Serial.println(respBuff);
          
      cActionState = ACT_CHANGE;
    }
    break;

  case ACT_CHANGE:
    if (millis() - actionMillis > 1000)
    {
      actionMillis = millis();

      sprintf(reqBuff, "mac=%s&channel=%d&value=%d", deviceID, actionCH, actionValue);
      KMPProDinoESP32.setRelayState(actionCH, actionValue);
      requestServer(currentClient, "http://api.graviplant-online.de", "/steuerbox/v1/actionLog/", reqBuff, respBuff, 2000);

      Serial.println(respBuff);

      cActionState = ACT_IDLE;
    }
    
    break;

  default:
    cActionState = ACT_IDLE;
    break;
  }

  if (netConfig.type == conn_eth_dhcp || netConfig.type == conn_eth_man)
  {
    Ethernet.maintain();
  }

  dnsServer.processNextRequest();
  webServer.handleClient();

  if (millis() - lastServerOnline > 10 * 60 * 1000)
  {
    if (apDisabled)
    {
      apDisabled = 0;
      Serial.println("Starting Recovery Mode!");
      WiFi.softAP(("VV_SB_" + String(deviceID)).c_str(), "visioverdis");
    }
  }
  // put your main code here, to run repeatedly:
}

void updateSensorData()
{

  uint32_t ain0, ain1, ain2, ain3;

  ain0 = readPinCal(PIN_ADC0);
  ain1 = readPinCal(PIN_ADC1);
  ain2 = readPinCal(PIN_ADC2);
  ain3 = readPinCal(PIN_ADC3);

  adc0_filt = ((adc0_filt * 49) + ain0) / 50;
  adc1_filt = ((adc1_filt * 49) + ain1) / 50;
  adc2_filt = ((adc2_filt * 49) + ain2) / 50;
  adc3_filt = ((adc3_filt * 49) + ain3) / 50;

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
          serverStatus ? "Verbunden" : "Getrennt",
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