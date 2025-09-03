# ESP8266_Daikin_Controller
ESP8266 based Daikin Climate IR Remote Controller and Dallas DS1820 Temprature Sensor.

## FEATURES
- Turn on and off AC.
- Change temperature
- Get room temperature with sensor (it refreshes at 60 seconds)
- It has a web panel and API.

## LIBRARIES
You can install them from Arduino IDE libraries section.</br>
IRremoteESP8266 : https://github.com/crankyoldgit/IRremoteESP8266 </br>
DallasTemperature : https://github.com/milesburton/Arduino-Temperature-Control-Library </br>
OneWire : https://www.pjrc.com/teensy/td_libs_OneWire.html </br>

## ARDUINO IDE ESP8266 INSTALLATION
- Add this to ``Arduino IDE / File / Preferences / Additional Boards Manager URLs`` ``http://arduino.esp8266.com/stable/package_esp8266com_index.json``
- Install ESP8266 Boards from ``Arduino IDE / Tools / Boards Manager / Search "esp8266 by ESP8266 Community"``
- Select ``NodeMCU MCU 1.0 (ESP-12E Module) as target device.``
- If Windows 11 can't see ESP8266 correctly in ``Device Manager``, you have to install this driver. </br>
Unzip driver, right click device, select update driver, choose driver folder. </br> https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers?tab=downloads

## INFO ABOUT PHOTO.JPG
I didn't have correct colored male to male cables for connection. So, they are mixed. In the future I can consider to connect them on perfboard.
