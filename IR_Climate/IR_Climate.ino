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

// Timer settings
unsigned long acTimerStart = 0;
unsigned long acTimerDuration = 0;  // Duration in milliseconds (0 = no timer)
bool timerChanged = false;  // Flag to indicate timer changes that need feedback

// Initialize the IR sender
IRDaikinESP ac(kIrLed);

// Web server on port 80
ESP8266WebServer server(80);

// Message for notifications
String notificationMessage = "";

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
  
  // Initialize the IR sender
  ac.begin();
  setDefaultSettings();
  
  // Initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    // Don't proceed, but continue with other initializations
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Daikin AC Controller");
    display.println("Connecting to WiFi...");
    display.display();
  }
  
  // Initialize the temperature sensors - SIMPLIFIED approach
  Serial.println("Initializing temperature sensor...");
  sensors.begin();
  
  // Get initial temperature reading
  updateTemperature();
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to WiFi. IP address: ");
  Serial.println(WiFi.localIP());
  
  // Display IP address on OLED
  displayIPAddress();
  
  // Define API endpoints
  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/temp", handleTemp);
  server.on("/fan", handleFan);  // New endpoint for fan speed
  server.on("/timer", handleTimer);  // New endpoint for timer
  server.on("/clear_timer", handleClearTimer);  // New endpoint for clearing timer
  server.on("/status", handleStatus);
  server.on("/refresh_temp", handleRefreshTemp);
  server.on("/raw_temp", handleRawTemp);  // Added raw temperature endpoint for debugging
  
  // Start the server
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
  
  // Check if timer is active and has expired
  if (acTimerDuration > 0 && acPower) {
    if (millis() - acTimerStart >= acTimerDuration) {
      // Timer expired, turn off the AC
      acPower = false;
      acTimerDuration = 0; // Reset timer
      sendAcCommand();
      Serial.println("Timer expired - AC turned off");
    }
  }
}

// Update temperature from the DS1820/DS18B20 sensor
// Using the simpler approach from the working code
void updateTemperature()
{
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

// Set default AC settings
void setDefaultSettings()
{
  ac.setFan(acFanSpeed);
  ac.setMode(acMode);
  ac.setTemp(acTemp);
  ac.setSwingVertical(false);
  ac.setSwingHorizontal(false);
  ac.setQuiet(false);
  ac.setPowerful(false);
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

// Root page handler
void handleRoot()
{
  String html = "<html><head>";
  html += "<meta charset=\"UTF-8\">";
  html += "<title>Daikin AC Control</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; text-align: center; background-color: black; color: white; }";
  html += "button { background-color: #4CAF50; border: none; color: white; padding: 15px 32px; margin: 10px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; border-radius: 8px; }";
  html += ".off { background-color: #f44336; }";
  html += ".temp { background-color: #008CBA; }";
  html += ".refresh { background-color: #FF9800; }";
  html += ".clear { background-color: #9C27B0; }";
  html += ".sensor { font-size: 1.2em; font-weight: bold; margin: 20px 0; padding: 10px; background-color: #333; border-radius: 5px; color: #fff; }";
  html += ".error { color: #ff6b6b; }";
  html += ".notification { background-color: #2196F3; color: white; padding: 10px; margin: 10px 0; border-radius: 5px; animation: fadeOut 5s forwards; }";
  html += "select { padding: 10px; font-size: 16px; border-radius: 8px; background-color: #008CBA; color: white; margin: 10px; border: none; cursor: pointer; }";
  html += "select:focus { outline: none; }";
  html += ".temp-control, .fan-control, .timer-control { margin: 15px 0; }";
  html += "@keyframes fadeOut { from { opacity: 1; } to { opacity: 0; display: none; } }";
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
  html += "      const timerStatus = document.getElementById('timerStatus');";
  html += "      if(data.timerActive) {";
  html += "        const hours = Math.floor(data.timerRemaining / 3600);";
  html += "        const mins = Math.floor((data.timerRemaining % 3600) / 60);";
  html += "        timerStatus.innerHTML = 'Timer: ' + hours + 'h ' + mins + 'm remaining';";
  html += "        timerStatus.style.display = 'block';";
  html += "      } else {";
  html += "        timerStatus.style.display = 'none';";
  html += "      }";
  html += "    });";
  html += "}";
  html += "function changeTemp() {";
  html += "  const temp = document.getElementById('tempSelect').value;";
  html += "  location.href = '/temp?value=' + temp;";
  html += "}";
  html += "function changeFan() {";
  html += "  const fan = document.getElementById('fanSelect').value;";
  html += "  location.href = '/fan?value=' + fan;";
  html += "}";
  html += "function setTimer() {";
  html += "  const timer = document.getElementById('timerSelect').value;";
  html += "  location.href = '/timer?value=' + timer;";
  html += "}";
  html += "function clearTimer() {";
  html += "  if (confirm('Are you sure you want to clear the timer?')) {";
  html += "    location.href = '/clear_timer';";
  html += "  }";
  html += "}";
  html += "window.onload = function() {";
  html += "  refreshTemp();";
  html += "  setTimeout(function() {";
  html += "    const notification = document.getElementById('notification');";
  html += "    if (notification) notification.style.display = 'none';";
  html += "  }, 5000);";
  html += "};";
  html += "setInterval(refreshTemp, 60000);"; // Refresh temp display every 60 seconds
  html += "</script>";
  html += "</head><body>";
  html += "<h1>Daikin AC Control</h1>";
  
  // Display notification if available
  if (notificationMessage.length() > 0) {
    html += "<div id='notification' class='notification'>" + notificationMessage + "</div>";
    notificationMessage = ""; // Clear the message after displaying it
  }
  
  html += "<div class='sensor'>Room Temperature: <span id='roomTemp'>";
  if (currentTemperature > -100)
  {
    html += String(currentTemperature, 1) + "&deg;C";  // HTML entity for degree symbol
  } 
  
  else
  {
    html += "<span class='error'>Sensor Error</span>";
  }

  html += "</span></div>";
  
  html += "<button class='refresh' onclick='location.href=\"/refresh_temp\"'>Refresh Temperature</button><br>";
  html += "<p>AC Status: " + String(acPower ? "ON" : "OFF") + "</p>";
  html += "<p>Set Temperature: " + String(acTemp) + "&deg;C</p>";
  html += "<button onclick='location.href=\"/on\"'>Turn ON</button>";
  html += "<button class='off' onclick='location.href=\"/off\"'>Turn OFF</button><br>";
  
  // Temperature selection box
  html += "<div class='temp-control'>";
  html += "<label for='tempSelect'>Select Temperature: </label>";
  html += "<select id='tempSelect' onchange='changeTemp()'>";
  
  // Generate options from 18 to 28 degrees
  for (int t = 18; t <= 28; t++) 
  {
    html += "<option value='" + String(t) + "'";
    if (t == acTemp) html += " selected";  // Pre-select the current temperature
    html += ">" + String(t) + "&deg;C</option>";
  }
  
  html += "</select>";
  html += "</div>";
  
  // Fan Speed selection box
  html += "<div class='fan-control'>";
  html += "<label for='fanSelect'>Select Fan Speed: </label>";
  html += "<select id='fanSelect' onchange='changeFan()'>";
  
  // Fan speed options from 2 to 5
  html += "<option value='10'" + String(acFanSpeed == kDaikinFanAuto ? " selected" : "") + ">Auto</option>";
  for (int f = 2; f <= 5; f++) {
    html += "<option value='" + String(f) + "'";
    if (f == acFanSpeed) html += " selected";
    html += ">" + String(f) + "</option>";
  }
  html += "<option value='11'" + String(acFanSpeed == kDaikinFanQuiet ? " selected" : "") + ">Quiet</option>";
  
  html += "</select>";
  html += "</div>";
  
  // Timer selection box
  html += "<div class='timer-control'>";
  html += "<label for='timerSelect'>Auto-Off Timer: </label>";
  html += "<select id='timerSelect' onchange='setTimer()'>";
  
  // Timer options
  html += "<option value='0'" + String(acTimerDuration == 0 ? " selected" : "") + ">No Timer</option>";
  for (int h = 1; h <= 4; h++) {
    html += "<option value='" + String(h) + "'";
    if (acTimerDuration == h * 3600000) html += " selected";
    html += ">" + String(h) + " Hour" + (h > 1 ? "s" : "") + "</option>";
  }
  
  html += "</select>";
  
  // Clear Timer button
  html += "<button class='clear' onclick='clearTimer()'>Clear Timer</button>";
  
  // Show remaining time if timer is active
  html += "<p id='timerStatus' style='display: " + String(acTimerDuration > 0 && acPower ? "block" : "none") + ";'>";
  if (acTimerDuration > 0 && acPower) {
    unsigned long remainingTime = acTimerDuration - (millis() - acTimerStart);
    int remainingMins = (remainingTime / 60000) % 60;
    int remainingHours = remainingTime / 3600000;
    html += "Timer: " + String(remainingHours) + "h " + String(remainingMins) + "m remaining";
  }
  html += "</p>";
  
  html += "</div>";
  
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

// Handle power on
void handleOn()
{
  acPower = true;
  
  // Reset timer start time if timer is active
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
      // Store previous timer state to determine if there's a change
      unsigned long previousTimerDuration = acTimerDuration;
      
      if (hours == 0) {
        acTimerDuration = 0; // No timer
      } else {
        acTimerDuration = hours * 3600000; // Convert hours to milliseconds
        if (acPower) {
          acTimerStart = millis(); // Start the timer if AC is on
        }
      }
      
      // If there's a change in timer settings and AC is on, send the command
      if (previousTimerDuration != acTimerDuration && acPower) {
        sendAcCommand();
        String message = "Timer set for " + String(hours) + " hour" + (hours != 1 ? "s" : "");
        notificationMessage = message;
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handle clearing timer
void handleClearTimer()
{
  // Only need to notify if there was an active timer
  bool hadActiveTimer = (acTimerDuration > 0);
  
  // Clear timer
  acTimerDuration = 0;
  
  // If AC is on, send the command to ensure timer is cleared on the device
  if (acPower) {
    sendAcCommand();
  }
  
  if (hadActiveTimer) {
    notificationMessage = "Timer has been cleared";
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
  
  // Add timer information
  if (acTimerDuration > 0 && acPower) {
    unsigned long remainingTime = acTimerDuration - (millis() - acTimerStart);
    json += ",\"timerActive\":true";
    json += ",\"timerRemaining\":" + String(remainingTime / 1000); // in seconds
  } else {
    json += ",\"timerActive\":false";
    json += ",\"timerRemaining\":0";
  }
  
  json += "}";
  
  server.send(200, "application/json", json);
}