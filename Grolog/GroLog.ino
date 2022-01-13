/*
"http://arduino.esp8266.com/stable/package_esp8266com_index.json"
ESP8266

Used Libs
----------

Download ModbusMaster by Doc Walker
Download ArduinoJson by Arduino group

Board: Generic ESP8266 Module
Flash Mode: DIO
Cristal Freq:: 26 MHz
Flash Freq: 40 MHz
Upload Using: Serial
CPU Freq: 80 MHz
Flash Size: 4 MB (FS:none OTA~1019KB)
UploadSpeed: 115200

Based on the work of https://github.com/otti/Growatt_ShineWiFi-S
Which is itself based on the work by Jethro Kairys
https://github.com/jkairys/growatt-esp8266

*/

// ---------------------------------------------------------------
// User configuration area start
// ---------------------------------------------------------------

// Setting this define to 1 will ping the default gateway periodically 
// if the ping is not successful, the wifi connection will be reestablished
#define PINGER_SUPPORTED 1

// Setting this define to 1 will enable the debug output via the serial port.
// The serial port is the same used for the communication to the inverter.
// Enabling this feature can cause problems with the inverter communication!
// For ShineWiFi-S everything seems to work perfect even though this flag is set
#define ENABLE_DEBUG_OUTPUT 0

// Setting this define to 1 will enable a web page (<ip>/debug) where debug messages can be displayed
#define ENABLE_WEB_DEBUG 1


// Setting this flag to 1 will simulate the inverter
// This could be helpful if it is night and the inverter is not working or
// during development where the stick is not connected to the inverter
#define SIMULATE_INVERTER 0

// Data of the Wifi access point
#define WIFI_SSID         "<SSID>"
#define WIFI_PASSWORD     "<SSID_PASSWORD>"
#define HOSTNAME          "Growatt"
 
#define API_TOKEN        "<TOKEN>"
#define API_TOKEN_NAME   "<TOKEN_NAME>"
const char *tcpTarget = "<GRAYLOG_SERVER>";
const uint16_t tcpPort = 12202;

#if PINGER_SUPPORTED == 1
#define GATEWAY_IP IPAddress(1, 1, 1, 1)
#endif

#if ENABLE_WEB_DEBUG == 1
    char acWebDebug[1024] = "";
    uint16_t u16WebMsgNo = 0;
    #define WEB_DEBUG_PRINT(s) {if( (strlen(acWebDebug)+strlen(s)+50) < sizeof(acWebDebug) ) sprintf(acWebDebug, "%s#%i: %s\n", acWebDebug, u16WebMsgNo++, s);}
#else
 #define WEB_DEBUG_PRINT(s) ;
#endif



// ---------------------------------------------------------------
// User configuration area end
// ---------------------------------------------------------------


#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include <WiFiClientSecure.h>

#include <time.h>

#include <ArduinoJson.h>

#include "Growatt.h"

#include "index.h"

#if PINGER_SUPPORTED == 1
#include <Pinger.h>
#include <PingerResponse.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#define LED_GN 0  // GPIO0
#define LED_RT 2  // GPIO2
#define LED_BL 16 // GPIO16

#define NUM_OF_RETRIES 5
char u8RetryCounter = NUM_OF_RETRIES;

long lAccumulatedEnergy = 0;

uint16_t u16PacketCnt = 0;

/* Configuration for NTP */
#define NTP_SERVER "europe.pool.ntp.org"           
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3" 

time_t ntpNow;
tm tm;

#if PINGER_SUPPORTED == 1
Pinger pinger;
#endif

WiFiClientSecure   wifiClient;

Growatt      Inverter;

ESP8266WebServer httpServer(80);

char jsonString[512] = "{\"Status\": \"Disconnected\" }"; // Adjust char size

// -------------------------------------------------------
// Check the WiFi status and reconnect if necessary
// -------------------------------------------------------
void WiFi_Reconnect()
{
  uint16_t cnt = 0;

  if( WiFi.status() != WL_CONNECTED )
  {
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(200);
      #if ENABLE_DEBUG_OUTPUT == 1
      Serial.begin(115200);
      Serial.print("Connecting to WiFi");
      #endif
    }

    #if ENABLE_DEBUG_OUTPUT == 1
    Serial.begin(115200);
    Serial.println("");
    WiFi.printDiag(Serial);
    Serial.print("local IP:");
    Serial.println(WiFi.localIP());
    Serial.print("Hostname: ");
    Serial.println(HOSTNAME);
    #endif

    WEB_DEBUG_PRINT("WiFi reconnected")
    
  }
}

// Conection can fail after sunrise. The stick powers up before the inverter.
// So the detection of the inverter will fail. If no inverter is detected, we have to retry later (s. loop() )
// The detection without running inverter will take several seconds, because the ModBus-Lib has a timeout of 2s 
// for each read access (and we do several of them). The WiFi can crash during this function. Perhaps we can fix 
// this by using the callback function of the ModBus-Lib
void InverterReconnect(void)
{
  // Baudrate will be set here, depending on the version of the stick
  Inverter.begin(Serial);

  #if ENABLE_WEB_DEBUG == 1
  if( Inverter.GetWiFiStickType() == ShineWiFi_S )
    WEB_DEBUG_PRINT("ShineWiFi-S (Serial) found")
  else if( Inverter.GetWiFiStickType() == ShineWiFi_X )
    WEB_DEBUG_PRINT("ShineWiFi-X (USB) found")
  else
    WEB_DEBUG_PRINT("Error: Undef. Stick")
  #endif
}

// -------------------------------------------------------
// Will be executed once after power on
// -------------------------------------------------------
void setup()
{

  WEB_DEBUG_PRINT("Setup()")
  
  WiFi.hostname(HOSTNAME);
  while (WiFi.status() != WL_CONNECTED)
    WiFi_Reconnect();

  httpServer.on("/status", SendJsonSite);
  httpServer.on("/setAccumulatedEnergy", HTTP_POST, vSetAccumulatedEnergy);
  httpServer.on("/", MainPage);
  #if ENABLE_WEB_DEBUG == 1
  httpServer.on("/debug", SendDebug);
  #endif

  configTime(TIMEZONE, NTP_SERVER);

  InverterReconnect();

  httpServer.begin();
}

void SendJsonSite(void)
{
  httpServer.send(200, "application/json", jsonString);
}

#if ENABLE_WEB_DEBUG == 1
void SendDebug(void)
{
  httpServer.send(200, "text/plain", acWebDebug);
}
#endif

void MainPage(void)
{
  httpServer.send(200, "text/html", MAIN_page);
}

void vSetAccumulatedEnergy()
{
  if (httpServer.hasArg("AcE"))
  {
    // only react if AcE is transmitted
    char * msg;
    msg = jsonString;

    if (lAccumulatedEnergy <= 0)
    {
      lAccumulatedEnergy = httpServer.arg("AcE").toInt() * 3600;
      sprintf(msg, "Setting accumulated value to %d", httpServer.arg("AcE").toInt());
    }
    else
    {
      sprintf(msg, "Error: AccumulatedEnergy was not Zero or lower. Set to 0 first.");
    }

    if (httpServer.arg("AcE").toInt() == 0)
    {
      lAccumulatedEnergy = -1000 * 3600;
      sprintf(msg, "Prepared to set AcE. You can change it as long as it is negative.");
    }

    httpServer.send(200, "text/plain", msg);
  }
  else
  {
    httpServer.send(400, "text/plain", "400: Invalid Request"); // The request is invalid, so send HTTP status 400
  }
}

void handlePostData()
{
  char * msg;
  uint16_t u16Tmp;

  msg = jsonString;
  msg[0] = 0;

  if (!httpServer.hasArg("reg") || !httpServer.hasArg("val")) 
  {
    // If the POST request doesn't have data
    httpServer.send(400, "text/plain", "400: Invalid Request"); // The request is invalid, so send HTTP status 400
    return;
  }
  else
  {
    if (httpServer.arg("rd") == "Rd")
    {
      if (Inverter.ReadHoldingReg(httpServer.arg("reg").toInt(), & u16Tmp))
      {
        sprintf(msg, "Read register %d with value %d", httpServer.arg("reg").toInt(), u16Tmp);
      } 
      else 
      {
        sprintf(msg, "Read register %d impossible - not connected?", httpServer.arg("reg").toInt());
      }
    }
    else
    {
      if (Inverter.WriteHoldingReg(httpServer.arg("reg").toInt(), httpServer.arg("val").toInt()))
        sprintf(msg, "Wrote Register %d to a value of %d!", httpServer.arg("reg").toInt(), httpServer.arg("val").toInt());
      else
        sprintf(msg, "Did not write Register %d to a value of %d - fault!", httpServer.arg("reg").toInt(), httpServer.arg("val").toInt());
    }
    httpServer.send(200, "text/plain", msg);
    return;
  }
}

// -------------------------------------------------------
// Main loop
// -------------------------------------------------------
long Timer500ms = 0;
long Timer5s = 0;
long Timer2m = 0;

void loop()
{
  time(&ntpNow);
  localtime_r(&ntpNow, &tm);
  long timestamp = ntpNow;
  
  long now = millis();
  long lTemp;
  char readoutSucceeded;
  
  WiFi_Reconnect();
  
  httpServer.handleClient();

  // InverterReconnect() takes a long time --> wifi will crash
  // Do it only every two minutes
  if( (now - Timer2m) > (1000 * 60 * 2) )
  {
    if( Inverter.GetWiFiStickType() == Undef_stick )
      InverterReconnect();
    Timer2m = now;
  }

    // Read Inverter every 5 s
  // ------------------------------------------------------------
  if( (now - Timer5s) > 5000)
  {
    if ((WiFi.status() == WL_CONNECTED) && (Inverter.GetWiFiStickType()) )
    {
      readoutSucceeded = 0;
      while ((u8RetryCounter) && !(readoutSucceeded))
      {
        #if SIMULATE_INVERTER == 1
        if( 1 ) // do it always
        #else
        if( Inverter.UpdateData() ) // get new data from inverter
        #endif
        {
          WEB_DEBUG_PRINT("UpdateData() successful")
          u16PacketCnt++;
          u8RetryCounter = NUM_OF_RETRIES;

          StaticJsonDocument<400> gelfJsonDoc;
          CreateGelfJson(gelfJsonDoc);
          gelfJsonDoc["timestamp"] = timestamp;
          gelfJsonDoc[API_TOKEN_NAME] = API_TOKEN;
          serializeJson(gelfJsonDoc, jsonString);
     
          wifiClient.setInsecure();
          if (!wifiClient.connect(tcpTarget, tcpPort)) {
            WEB_DEBUG_PRINT("Secured connection failed")
          }
          wifiClient.print(jsonString);
          //wifiClient.flush();
          wifiClient.stop();

          // if we got data, calculate the accumulated energy
          lTemp = (now - Timer5s) * Inverter.GetAcPower();      // we now get an increment in milliWattSeconds
          lTemp /= 1000;                                        // WattSeconds
          lAccumulatedEnergy += lTemp;                          // WattSeconds
          
          // leave while-loop
          readoutSucceeded = 1;
        }
        else
        {
          WEB_DEBUG_PRINT("UpdateData() NOT successful")
          if(u8RetryCounter)
          {
            u8RetryCounter--;
          }
          else
          {
            WEB_DEBUG_PRINT("Retry counter\n")
            StaticJsonDocument<200> gelfJsonDoc;
            gelfJsonDoc["version"] =  "1.1";
            gelfJsonDoc["host"] =  HOSTNAME;
            gelfJsonDoc["timestamp"] = timestamp;
            gelfJsonDoc["short_message"] = "Disconnected";
            gelfJsonDoc[API_TOKEN_NAME] = API_TOKEN;
            serializeJson(gelfJsonDoc, jsonString);
    
            wifiClient.setInsecure();
            if (!wifiClient.connect(tcpTarget, tcpPort)) {
              WEB_DEBUG_PRINT("Secured connection failed")
            }
            wifiClient.print(jsonString);
            //wifiClient.flush();
            wifiClient.stop();
          }
        }
      }
    }

    
    #if PINGER_SUPPORTED == 1
    //frequently check if gateway is reachable
    if (pinger.Ping(GATEWAY_IP) == false) 
      WiFi.disconnect();
    #endif

    Timer5s = now;
  }
}

void CreateGelfJson(JsonDocument& gelfJsonDoc)
{
  gelfJsonDoc["version"] =  "1.1";
  gelfJsonDoc["host"] =  HOSTNAME;
  gelfJsonDoc["_Cnt_int"] = u16PacketCnt;
#if SIMULATE_INVERTER != 1
  switch( Inverter.GetStatus() )
  {
    case GwStatusWaiting:
      gelfJsonDoc["short_message"] =  "Waiting";
      break;
    case GwStatusNormal: 
      gelfJsonDoc["short_message"] =  "Normal";
      break;
    case GwStatusFault:
      gelfJsonDoc["short_message"] =  "Fault";
      break;
    default:
      gelfJsonDoc["short_message"] =  Inverter.GetStatus();
  }
  
  gelfJsonDoc["_DcVoltage_double"] = Inverter.GetDcVoltage();
  gelfJsonDoc["_AcFreq_int"] = Inverter.GetAcFrequency();
  gelfJsonDoc["_AcVoltage_double"] = Inverter.GetAcVoltage();
  gelfJsonDoc["_AcPower_double"] = Inverter.GetAcPower();
  gelfJsonDoc["_EnergyToday_double"] = Inverter.GetEnergyToday();
  gelfJsonDoc["_EnergyTotal_double"] = Inverter.GetEnergyTotal();
  gelfJsonDoc["_OperatingTime_int"] = Inverter.GetOperatingTime();
  gelfJsonDoc["_Temperature_double"] = Inverter.GetInverterTemperature();
  gelfJsonDoc["_AccumulatedEnergy_long"] = lAccumulatedEnergy / 3600;
#else
  #warning simulating the inverter
  gelfJsonDoc["short_message"] =  "Normal";
  gelfJsonDoc["_DcVoltage_double"] = 70.5;
  gelfJsonDoc["_AcFreq_int"] = 50.00;
  gelfJsonDoc["_AcVoltage_double"] = 230.0;
  gelfJsonDoc["_AcPower_double"] = 0.00;
  gelfJsonDoc["_EnergyToday_double"] = 0.3;
  gelfJsonDoc["_EnergyTotal_double"] = 69.0;
  gelfJsonDoc["_OperatingTime_int"] = 123456;
  gelfJsonDoc["_Temperature_double"] = 20.0;
  gelfJsonDoc["_Temperature_double"] = 236;
#endif // SIMULATE_INVERTER  
}
