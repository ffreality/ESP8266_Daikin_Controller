#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Daikin.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Network credentials
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";

// IR LED pin
const uint16_t kIrLed = D2;  // ESP8266 GPIO4 (D2)

// DS18B20/DS1820 Temperature sensor setup
// IMPORTANT: Changed to D4 (GPIO2) as in the working example
#define ONE_WIRE_BUS 2  // D4 (GPIO2)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float currentTemperature = -127.0;
unsigned long lastTempUpdate = 0;
const unsigned long TEMP_UPDATE_INTERVAL = 60000;  // Update every 60 seconds (1 minute)

// AC settings
bool acPower = false;
uint8_t acTemp = 25;  // Default temperature in Celsius
uint8_t acFanSpeed = kDaikinFanAuto;
uint8_t acMode = kDaikinCool;

// Initialize the IR sender
IRDaikinESP ac(kIrLed);

// Web server on port 80
ESP8266WebServer server(80);

void setup() {
  // Start serial
  Serial.begin(115200);
  delay(200);
  
  // Initialize the IR sender
  ac.begin();
  setDefaultSettings();
  
  // Initialize the temperature sensors - SIMPLIFIED approach
  Serial.println("Initializing temperature sensor...");
  sensors.begin();
  
  // Get initial temperature reading
  updateTemperature();
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi. IP address: ");
  Serial.println(WiFi.localIP());
  
  // Define API endpoints
  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/temp", handleTemp);
  server.on("/status", handleStatus);
  server.on("/refresh_temp", handleRefreshTemp);
  server.on("/raw_temp", handleRawTemp);  // Added raw temperature endpoint for debugging
  
  // Start the server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  
  // Update temperature reading every minute
  if (millis() - lastTempUpdate >= TEMP_UPDATE_INTERVAL) {
    updateTemperature();
  }
}

// Update temperature from the DS1820/DS18B20 sensor
// Using the simpler approach from the working code
void updateTemperature() {
  Serial.println("Requesting temperature...");
  sensors.requestTemperatures();
  
  // Simplified approach: just read the first sensor on the bus by index
  float tempC = sensors.getTempCByIndex(0);
  
  // Check if reading is valid
  if (tempC != DEVICE_DISCONNECTED_C) {
    currentTemperature = tempC;
    Serial.print("Temperature: ");
    Serial.println(currentTemperature);
  } else {
    Serial.println("Error: Could not read temperature data");
  }
  
  lastTempUpdate = millis();
}

// Handle refresh temperature request
void handleRefreshTemp() {
  updateTemperature();
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle raw temperature data (for debugging)
void handleRawTemp() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  
  String json = "{\"raw_temp\":";
  json += String(tempC);
  json += ",\"device_status\":";
  json += (tempC == DEVICE_DISCONNECTED_C ? "\"disconnected\"" : "\"connected\"");
  json += ",\"device_count\":";
  json += String(sensors.getDeviceCount());
  json += "}";
  
  server.send(200, "application/json", json);
}

// Set default AC settings
void setDefaultSettings() {
  ac.setFan(acFanSpeed);
  ac.setMode(acMode);
  ac.setTemp(acTemp);
  ac.setSwingVertical(false);
  ac.setSwingHorizontal(false);
  ac.setQuiet(false);
  ac.setPowerful(false);
}

// Apply current settings and send IR command
void sendAcCommand() {
  ac.setPower(acPower);
  ac.setTemp(acTemp);
  ac.send();
  
  // Debug output
  Serial.println("Sending IR Command:");
  Serial.print("Power: ");
  Serial.println(acPower ? "ON" : "OFF");
  Serial.print("Temperature: ");
  Serial.println(acTemp);
  Serial.print("Mode: ");
  Serial.println(acMode);
  Serial.print("Fan Speed: ");
  Serial.println(acFanSpeed);
}

// Root page handler
void handleRoot() {
  String html = "<html><head>";
  html += "<meta charset=\"UTF-8\">";
  html += "<title>Daikin AC Control</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }";
  html += "button { background-color: #4CAF50; border: none; color: white; padding: 15px 32px; margin: 10px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; border-radius: 8px; }";
  html += ".off { background-color: #f44336; }";
  html += ".temp { background-color: #008CBA; }";
  html += ".refresh { background-color: #FF9800; }";
  html += ".sensor { font-size: 1.2em; font-weight: bold; margin: 20px 0; padding: 10px; background-color: #f1f1f1; border-radius: 5px; }";
  html += ".error { color: red; }";
  html += "</style>";
  html += "<script>";
  html += "function refreshTemp() {";
  html += "  fetch('/status')";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      const temp = document.getElementById('roomTemp');";
  html += "      if(data.roomTemperature > -100) {";
  html += "        temp.innerText = data.roomTemperature.toFixed(1) + '\\u00B0C';";  // Unicode for degree symbol
  html += "        temp.className = '';";
  html += "      } else {";
  html += "        temp.innerText = 'Sensor Error';";
  html += "        temp.className = 'error';";
  html += "      }";
  html += "    });";
  html += "}";
  html += "window.onload = refreshTemp;";
  html += "setInterval(refreshTemp, 60000);"; // Refresh temp display every 60 seconds
  html += "</script>";
  html += "</head><body>";
  html += "<h1>Daikin AC Control</h1>";
  
  html += "<div class='sensor'>Room Temperature: <span id='roomTemp'>";
  if (currentTemperature > -100) {
    html += String(currentTemperature, 1) + "&deg;C";  // HTML entity for degree symbol
  } else {
    html += "<span class='error'>Sensor Error</span>";
  }
  html += "</span></div>";
  
  html += "<button class='refresh' onclick='location.href=\"/refresh_temp\"'>Refresh Temperature</button><br>";
  html += "<p>AC Status: " + String(acPower ? "ON" : "OFF") + "</p>";
  html += "<p>Set Temperature: " + String(acTemp) + "&deg;C</p>";
  html += "<button onclick='location.href=\"/on\"'>Turn ON</button>";
  html += "<button class='off' onclick='location.href=\"/off\"'>Turn OFF</button><br>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=18\"'>18&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=19\"'>19&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=20\"'>20&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=21\"'>21&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=22\"'>22&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=23\"'>23&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=24\"'>24&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=25\"'>25&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=26\"'>26&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=27\"'>27&deg;C</button>";
  html += "<button class='temp' onclick='location.href=\"/temp?value=28\"'>28&deg;C</button>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

// Handle power on
void handleOn() {
  acPower = true;
  sendAcCommand();
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle power off
void handleOff() {
  acPower = false;
  sendAcCommand();
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle temperature changes
void handleTemp() {
  if (server.hasArg("value")) {
    int temp = server.arg("value").toInt();
    if (temp >= 18 && temp <= 30) {  // Typical Daikin temperature range
      acTemp = temp;
      if (acPower) {
        sendAcCommand();
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Return current AC status as JSON
void handleStatus() {
  String json = "{";
  json += "\"power\":" + String(acPower ? "true" : "false") + ",";
  json += "\"temperature\":" + String(acTemp) + ",";
  json += "\"roomTemperature\":" + String(currentTemperature) + ",";
  json += "\"mode\":" + String(acMode) + ",";
  json += "\"fanSpeed\":" + String(acFanSpeed);
  json += "}";
  
  server.send(200, "application/json", json);
}
