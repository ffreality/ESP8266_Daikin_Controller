#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Daikin.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// LittleFS file serving
#include <FS.h>
#include <LittleFS.h>

// OLED Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_SDA 14  // D5 (GPIO14)
#define OLED_SCL 12  // D6 (GPIO12)
#define OLED_RESET -1 // Reset pin not used on 0.91" OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Network credentials
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASS";

// IR LED pin
const uint16_t kIrLed = D2;  // ESP8266 GPIO4 (D2)

// DS18B20/DS1820 Temperature sensor setup
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

// Timer settings
unsigned long acTimerStart = 0;
unsigned long acTimerDuration = 0;  // Duration in milliseconds (0 = no timer)

// Initialize the IR sender
IRDaikinESP ac(kIrLed);

// Web server on port 80
ESP8266WebServer server(80);

// Function to display IP address on OLED
void displayIPAddress() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connected to WiFi");
  display.println(ssid);
  display.println("");
  display.print("IP: ");
  display.println(WiFi.localIP().toString());
  display.display();
}

void setup()
{
  // Start serial
  Serial.begin(115200);
  delay(200);

  // Initialize the IR sender & defaults
  ac.begin();
  ac.setFan(acFanSpeed);
  ac.setMode(acMode);
  ac.setTemp(acTemp);
  ac.setSwingVertical(false);
  ac.setSwingHorizontal(false);
  ac.setQuiet(false);
  ac.setPowerful(false);

  // Initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Daikin AC Controller");
    display.println("Connecting to WiFi...");
    display.display();
  }

  // Initialize the temperature sensor
  Serial.println("Initializing temperature sensor...");
  sensors.begin();
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

  // OLED IP
  displayIPAddress();

  // Mount LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
  } else {
    Serial.println("LittleFS mounted");
  }

  // Routes
  server.on("/", handleRoot);                 // now serves /index.html from LittleFS
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/temp", handleTemp);
  server.on("/fan", handleFan);
  server.on("/timer", handleTimer);
  server.on("/clear_timer", handleClearTimer);
  server.on("/status", handleStatus);
  server.on("/refresh_temp", handleRefreshTemp);
  server.on("/raw_temp", handleRawTemp);

  server.begin();
  Serial.println("HTTP server started");
}

void loop()
{
  server.handleClient();

  // Update temperature reading every minute
  if (millis() - lastTempUpdate >= TEMP_UPDATE_INTERVAL) {
    updateTemperature();
  }

  // Auto-off timer
  if (acTimerDuration > 0 && acPower) {
    if (millis() - acTimerStart >= acTimerDuration) {
      acPower = false;
      acTimerDuration = 0; // Reset timer
      sendAcCommand();
      Serial.println("Timer expired - AC turned off");
    }
  }
}

// ==== FILE-SERVED ROOT ====
void handleRoot()
{
  File f = LittleFS.open("/index.html", "r");
  if (!f) {
    server.send(404, "text/plain", "index.html not found");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

// Update temperature from the DS1820/DS18B20 sensor
void updateTemperature()
{
  Serial.println("Requesting temperature...");
  sensors.requestTemperatures();

  float tempC = sensors.getTempCByIndex(0);

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
void handleRefreshTemp()
{
  updateTemperature();
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle raw temperature data (for debugging)
void handleRawTemp()
{
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

// Apply current settings and send IR command
void sendAcCommand()
{
  ac.setPower(acPower);
  ac.setTemp(acTemp);
  ac.setFan(acFanSpeed);
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

// Handle power on
void handleOn()
{
  acPower = true;

  if (acTimerDuration > 0) {
    acTimerStart = millis();
  }

  sendAcCommand();
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle power off
void handleOff()
{
  acPower = false;
  sendAcCommand();
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle temperature changes
void handleTemp()
{
  if (server.hasArg("value")) {
    int temp = server.arg("value").toInt();
    if (temp >= 18 && temp <= 30) {
      acTemp = temp;
      if (acPower) {
        sendAcCommand();
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle fan speed changes
void handleFan()
{
  if (server.hasArg("value")) {
    int fan = server.arg("value").toInt();
    if ((fan >= 2 && fan <= 5) || fan == kDaikinFanAuto || fan == kDaikinFanQuiet) {
      acFanSpeed = fan;
      if (acPower) {
        sendAcCommand();
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle timer changes
void handleTimer()
{
  if (server.hasArg("value")) {
    int hours = server.arg("value").toInt();
    if (hours >= 0 && hours <= 4) {
      unsigned long previousTimerDuration = acTimerDuration;

      if (hours == 0) {
        acTimerDuration = 0; // No timer
      } else {
        acTimerDuration = (unsigned long)hours * 3600000UL; // Convert hours to ms
        if (acPower) {
          acTimerStart = millis(); // Start timer if AC is on
        }
      }

      if (previousTimerDuration != acTimerDuration && acPower) {
        sendAcCommand();
        String message = "Timer set for " + String(hours) + " hour" + (hours != 1 ? "s" : "");
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle clearing timer
void handleClearTimer()
{
  bool hadActiveTimer = (acTimerDuration > 0);

  acTimerDuration = 0;

  if (acPower) {
    sendAcCommand();
  }

  Serial.println("Timer cleared");
  server.sendHeader("Location", "/");
  server.send(303);
}

// Return current AC status as JSON
void handleStatus()
{
  String json = "{";
  json += "\"power\":" + String(acPower ? "true" : "false") + ",";
  json += "\"temperature\":" + String(acTemp) + ",";
  json += "\"roomTemperature\":" + String(currentTemperature) + ",";
  json += "\"mode\":" + String(acMode) + ",";
  json += "\"fanSpeed\":" + String(acFanSpeed);

  if (acTimerDuration > 0 && acPower) {
    unsigned long remainingTime = acTimerDuration - (millis() - acTimerStart);
    json += ",\"timerActive\":true";
    json += ",\"timerRemaining\":" + String(remainingTime / 1000UL); // in seconds
  } else {
    json += ",\"timerActive\":false";
    json += ",\"timerRemaining\":0";
  }

  json += "}";

  server.send(200, "application/json", json);
}