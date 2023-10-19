#include <Arduino.h>
#include "KMPProDinoESP32.h"
#include "KMPCommon.h"

#include <WiFi.h>
#include <WiFiClient.h>

#define SSID_NAME "PeanutPay"
#define SSID_PASSWORD "PeanutPay"


// Define text colors.
const char GREEN[] = "#90EE90"; // LightGreen
const char RED[] = "#FF4500"; // OrangeRed 
const char GRAY[] = "#808080";





void setup() {
  delay(5000);
	Serial.begin(115200);
	Serial.println("The example via WiFi and Ethernet is starting...");

	// Init Dino board.
	KMPProDinoESP32.begin(ProDino_ESP32_Ethernet);
	//KMPProDinoESP32.begin(ProDino_ESP32_Ethernet_GSM);
	//KMPProDinoESP32.begin(ProDino_ESP32_Ethernet_LoRa);
	//KMPProDinoESP32.begin(ProDino_ESP32_Ethernet_LoRa_RFM);
	KMPProDinoESP32.setStatusLed(blue);
	
	// Reset Relay status.
	KMPProDinoESP32.setAllRelaysOff();

	// Connect to WiFi network
	WiFi.begin(SSID_NAME, SSID_PASSWORD);
	Serial.print("\n\r \n\rWorking to connect");

	// Wait for connection
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	Serial.println();
	Serial.print("WiFi IP: ");
	Serial.print(WiFi.localIP());

  uint8_t wifiMac[8];
  WiFi.macAddress(wifiMac);

	// Start the Ethernet connection and the server.
	if (Ethernet.begin(wifiMac) == 0) {
		Serial.println("Failed to configure Ethernet using DHCP");
		// no point in carrying on, so do nothing forevermore:
		while (1);
	}

	Serial.println("Ethernet IP:");
	Serial.print(Ethernet.localIP());

	KMPProDinoESP32.offStatusLed();
}

void loop() {
  // put your main code here, to run repeatedly:
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}