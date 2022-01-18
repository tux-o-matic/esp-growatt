# esp-growatt
Arduino project for ESP8266 boards to collect production numbers from Growatt inverters and push them to a Graylog server.

## Needed libs in your Arduino IDE library
- ModbusMaster by Doc Walker
- ArduinoJson by Arduino group

## Tested board configuration
http://arduino.esp8266.com/stable/package_esp8266com_index.json

```shell
Board: Generic ESP8266 Module
Flash Mode: DIO
Cristal Freq:: 26 MHz
Flash Freq: 40 MHz
Upload Using: Serial
CPU Freq: 80 MHz
Flash Size: 4 MB (FS:none OTA~1019KB)
UploadSpeed: 115200
```

## Credits
Based on the work of https://github.com/otti/Growatt_ShineWiFi-S
Which is itself based on the work by Jethro Kairys
https://github.com/jkairys/growatt-esp8266
