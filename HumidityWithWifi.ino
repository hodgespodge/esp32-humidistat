#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>//
#include <EEPROM.h>
#include "DHT.h"

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "SPIFFS.h"


//Variables
int i = 0;
int statusCode;
const char* ssid = "Default SSID";
const char* passphrase = "Default password";
String st;
String content;
String esid;
String epass = "";

#define HOTSPOT_LED 32
#define WIFI_LED 33
#define HOTSPOT_BUTTON 15

#define LED_BLINK_DELAY 2000
//#define HUMIDITY_READ_DELAY 5000
//#define PARAMS_READ_DELAY 2000

#define MAX_FAILED_READS_IN_ROW 2

#define DHTPIN 4     // Digital pin connected to the DHT sensor

#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

#define ONBOARD_LED 2
#define HUM_RELAY_PIN 5
#define BONUS_PIN 17

//Function Decalration
bool testWifi(void);
void launchHotspot(void);
void setupAP(void);

//Establishing Local hotspotServer at port 80
WebServer hotspotServer(80);

//WiFiServer controlServer(80);/
/////////////////////////////
AsyncWebServer controlServer(80);
/////////////////////////////

DHT dht(DHTPIN, DHTTYPE);

float h = nanf("");
float t = nanf("");
float f = nanf("");

float new_h;
float new_t;
float new_c;

float targetTemp;
float targetHumidity;

unsigned int sensorReadInterval;

String readDHTHumidity(){
  return String(h);
}

String readDHTTemperature(){
  return String(t);
}

String readTargetTemp(){
  return String(targetTemp);
}

String readTargetHumidity(){
  return String(targetHumidity);
}
//These should read from spiffs on startup and be updated by web
//signed char humidityMode = 1; // [- 1 or 1]
//signed char tempMode = 1;

signed char humidityMode;  // [- 1 or 1]
signed char tempMode;      // [- 1 or 1]
unsigned char bonusPinMode;

//bool humidity_control_out = true;
//bool temp_control_out= true;

bool humidity_control_out = false;
bool temp_control_out= false;

void updateControlStatus(){
  humidity_control_out = humidityMode *(targetHumidity - h) > 0;
  temp_control_out = tempMode*(targetTemp - t) > 0;
}

String readControlStatus(){
  String out = String();

  if (humidityMode > 0){
    out += "humidifier: ";
    
  } else{
    out += "dehumidifier: ";
  }

  updateControlStatus();
 
  if (humidity_control_out){
    out += "on";
  } else{
    out += "off";
  }

  out += "\n";

  if (tempMode > 0){
    out += "heater: ";
    
  } else{
    out += "cooler: ";
  }
 
  if (temp_control_out){
    out += "on";
  } else{
    out += "off";
  }
//  Serial.println(out);
  return String(out);  
}

void setup()
{

  Serial.begin(115200); //Initialising if(DEBUG)Serial Monitor

  if(!SPIFFS.begin(true)){
     Serial.println("An Error has occurred while mounting SPIFFS");
     return;
  }
  
  Serial.println();
  Serial.println("Disconnecting current wifi connection");
  WiFi.disconnect();
  EEPROM.begin(512); //Initialasing EEPROM
  delay(10);
  
  pinMode(HOTSPOT_BUTTON, INPUT);

  pinMode(WIFI_LED, OUTPUT);
  pinMode(HOTSPOT_LED, OUTPUT);

  pinMode(ONBOARD_LED, OUTPUT);
  pinMode(HUM_RELAY_PIN, OUTPUT);

  digitalWrite(WIFI_LED, LOW);
  digitalWrite(HOTSPOT_LED, LOW);

  dht.begin();
  
  Serial.println();
  Serial.println();
  Serial.println("Startup");

  //---------------------------------------- Read eeprom for ssid and pass
  Serial.println("Reading EEPROM ssid");


  for (int i = 0; i < 32; ++i)
  {
    esid += char(EEPROM.read(i));
  }
  Serial.println();
  Serial.print("SSID: ");
  Serial.println(esid);
  Serial.println("Reading EEPROM pass");

  for (int i = 32; i < 96; ++i)
  {
    epass += char(EEPROM.read(i));
  }
  Serial.print("PASS: ");
  Serial.println(epass);


  WiFi.begin(esid.c_str(), epass.c_str());

  

  targetTemp = readFile(SPIFFS, "/tempInput.txt").toFloat();
  targetHumidity = readFile(SPIFFS, "/humidityInput.txt").toFloat();
  tempMode = readFile(SPIFFS,"/tempMode.txt").toInt();
  humidityMode = readFile(SPIFFS, "/humidityMode.txt").toInt();
  sensorReadInterval = readFile(SPIFFS, "/sensorReadInterval.txt").toInt();
  bonusPinMode = readFile(SPIFFS, "/bonusPinMode.txt").toInt();

  /////////////////////////////////////////////////
  // Route for root / web page
  controlServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/controller.html","text/html");
  });
  
//  // Route to load style.css file
//  controlServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
//    request->send(SPIFFS, "/style.css", "text/css");
//  });

  controlServer.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readDHTTemperature().c_str());
  });
  controlServer.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readDHTHumidity().c_str());
  });
  controlServer.on("/getTargetHumidity", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readTargetHumidity().c_str());
  });
  controlServer.on("/getTargetTemp", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readTargetTemp().c_str());
  });
  controlServer.on("/getStatus", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readControlStatus().c_str());
  });
  controlServer.on("/getReadInterval", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(sensorReadInterval).c_str());
  });
  controlServer.on("/getBonusPinMode", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(bonusPinMode).c_str()); //TODO
  });
  
  controlServer.on("/setTemp",HTTP_GET,[] (AsyncWebServerRequest *request){
    String inputValue;
    inputValue = request->getParam("value")->value();
    request->send(200, "text/plain", "targetTemp received");

    if (targetTemp != inputValue.toFloat()){ // Only update the value if new value is different
      targetTemp = inputValue.toFloat();
      writeFile(SPIFFS, "/tempInput.txt", inputValue.c_str());  
    }
    
  });

  controlServer.on("/setHumidity",HTTP_GET,[] (AsyncWebServerRequest *request){
    String inputValue;
    inputValue = request->getParam("value")->value();
    request->send(200, "text/plain", "targetHumidity received");

    if (targetHumidity != inputValue.toFloat()){
      targetHumidity = inputValue.toFloat();
      writeFile(SPIFFS, "/humidityInput.txt", inputValue.c_str());
    }
  });

  controlServer.on("/setTempMode",HTTP_GET,[] (AsyncWebServerRequest *request){
    String inputValue;
    inputValue = request->getParam("value")->value();
    request->send(200, "text/plain", "tempMode received");

    if (tempMode != inputValue.toInt()){
      tempMode = inputValue.toInt();
      writeFile(SPIFFS, "/tempMode.txt", inputValue.c_str());
    }
  });


  controlServer.on("/setHumidityMode",HTTP_GET,[] (AsyncWebServerRequest *request){
    String inputValue;
    inputValue = request->getParam("value")->value();
    request->send(200, "text/plain", "humidityMode received");

    if (humidityMode != inputValue.toInt()){
      humidityMode = inputValue.toInt();
      writeFile(SPIFFS, "/humidityMode.txt", inputValue.c_str());
    }
  });

  controlServer.on("/setReadInterval",HTTP_GET,[] (AsyncWebServerRequest *request){
    String inputValue;
    inputValue = request->getParam("value")->value();
    request->send(200, "text/plain", "readInterval received");

    if (sensorReadInterval != inputValue.toInt()){
      sensorReadInterval = inputValue.toInt();
      writeFile(SPIFFS, "/sensorReadInterval.txt", inputValue.c_str());
    }
  });

  controlServer.on("/setBonusPinMode",HTTP_GET,[] (AsyncWebServerRequest *request){
    String inputValue;
    inputValue = request->getParam("value")->value();
    request->send(200, "text/plain", "bonusPinMode received");
    Serial.print("received /setBonusPinMode ");
    Serial.println(inputValue);

    if (bonusPinMode != inputValue.toInt()){
      bonusPinMode = inputValue.toInt();
      writeFile(SPIFFS, "/bonusPinMode.txt", inputValue.c_str());
    }
  });

}


String readFile(fs::FS &fs, const char * path){
//  Serial.printf("Reading file: %s\r\n", path);/
  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    Serial.println("- empty file or failed to open file");
    return String();
  }
//  Serial.println("- read from file:");/
  String fileContent;
  while(file.available()){
    fileContent+=String((char)file.read());
  }
  file.close();
//  Serial.println(fileContent);/
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message){
//  Serial.printf("Writing file: %s\r\n", path);/
  File file = fs.open(path, "w");
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
//    Serial.println("- file written");/
  } else {
//    Serial.println("- write failed");/
  }
  file.close();
}



bool controlServer_started = false;

// Variable to store the HTTP request
String header;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

bool led_on = true;

unsigned short num_failed_reads_in_row = 0;

unsigned long currentTimeLED = millis();
unsigned long previousTimeLED = 0;

unsigned long currentTimeDHT= millis();
unsigned long previousTimeDHT = 0;

//unsigned long currentTimeParams = millis();
//unsigned long previousTimeParams = 0;

void loop() {

  currentTimeLED = millis();

  if (currentTimeLED - previousTimeLED > LED_BLINK_DELAY){
    previousTimeLED = currentTimeLED;
    led_on = !led_on;
  }

  currentTimeDHT = millis();

  if(currentTimeDHT - previousTimeDHT > sensorReadInterval){
    previousTimeDHT = currentTimeDHT;
    
    new_h = dht.readHumidity();
    // Read temperature as Celsius (the default)
    new_c = dht.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    new_t = dht.readTemperature(true);
    
     // Check if any reads failed 
    if (isnan(new_h) || isnan(new_t) || isnan(new_c)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      num_failed_reads_in_row++;

      if (num_failed_reads_in_row > MAX_FAILED_READS_IN_ROW){
        Serial.println("- failed to read too many times. RESETTING ESP32");

        ESP.restart();
      }
      
    }else{ 

      num_failed_reads_in_row = 0;
      // only update values when sensor returns correct values
      h = new_h;
      t = new_t;
      f = new_c;

      updateControlStatus();

      if ( humidity_control_out ){
        digitalWrite(HUM_RELAY_PIN, HIGH);
        Serial.println("Humidity pin out HIGH");
      }else{
        digitalWrite(HUM_RELAY_PIN, LOW);
        Serial.println("Humidity pin out LOW");
      }
      
      if (temp_control_out){
        Serial.println("Temp pin out High ");
      }else{
        Serial.println("Temp pin out LOW");
      }

      if(bonusPinMode){
        digitalWrite(BONUS_PIN, HIGH);
        Serial.println("BONUS pin out HIGH");
      }else{
        digitalWrite(BONUS_PIN, LOW);
        Serial.println("BONUS pin out LOW");
      }
      
    }

    Serial.print(F("Humidity: "));
    Serial.print(h);
    Serial.print(F("%  Temperature: "));
    Serial.print(t);
    Serial.print(F("°C "));
    Serial.print(f);
    Serial.println(F("°f "));
      
  }
  
  if ((WiFi.status() == WL_CONNECTED)) {

    digitalWrite(HOTSPOT_LED, LOW);

    if (led_on){
      digitalWrite(WIFI_LED, HIGH);
    }else{
      digitalWrite(WIFI_LED, LOW);
    }
    

    if (!controlServer_started) {
      Serial.println(WiFi.localIP());
      // OUTPUT LOCAL IP TO OLED
      controlServer.begin();
      controlServer_started = true;
    }

  }
  else {

  }

  if (testWifi() && (digitalRead(15) != 1))
  {
//    Serial.println(" connection status positive");
    return;
  }

  else
  {
    Serial.println("Connection Status Negative / D15 HIGH");
    Serial.println("Turning the HotSpot On");
    
    if (controlServer_started) {
      controlServer.end();
///      controlServer.close();
      controlServer_started = false;
    }
    
    launchHotspot();
    setupAP();// Setup HotSpot
  }

  Serial.println();
  Serial.println("Waiting.");

  while ((WiFi.status() != WL_CONNECTED))
  {

    currentTimeLED = millis();
    if (currentTimeLED - previousTimeLED > LED_BLINK_DELAY){
      previousTimeLED = currentTimeLED;
      led_on = !led_on;
    }

    digitalWrite(WIFI_LED, LOW);
    
    if (led_on){
      digitalWrite(HOTSPOT_LED, HIGH);
    }else{
      digitalWrite(HOTSPOT_LED, LOW);
    }
    
    delay(100);
    hotspotServer.handleClient();
  }
  delay(200);
}


bool testWifi(void)
{
  int c = 0;
  //Serial.println("Waiting for Wifi to connect");
  while ( c < 20 ) {
    if (WiFi.status() == WL_CONNECTED)
    {
      return true;
    }
    delay(500);
    Serial.print("*");
    c++;
  }
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
  return false;
}


void launchHotspot()
{
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("WiFi connected");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());
  createHotspotServer();
  // Start the hotspotServer
  hotspotServer.begin();
  Serial.println("Hotspot server started");
}

void setupAP(void)
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      //Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
  st = "<ol>";
  for (int i = 0; i < n; ++i)
  {
    // Print SSID and RSSI for each network found
    st += "<li>";
    st += WiFi.SSID(i);
    st += " (";
    st += WiFi.RSSI(i);

    st += ")";
    //st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    st += "</li>";
  }
  st += "</ol>";
  delay(100);

  IPAddress IP = IPAddress (192, 168, 1, 1); 
//  IPAddress gateway = IPAddress (10, 10, 2, 8); 
  IPAddress NMask = IPAddress (255, 255, 255, 0); 
  
  WiFi.softAPConfig(IP, IP, NMask);
  WiFi.softAP("IOTClimateController", "");
  Serial.println("Initializing_softap_for_wifi credentials_modification");
  launchHotspot();
  Serial.println("over");
}

void createHotspotServer()
{
  {
    hotspotServer.on("/", []() {

      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html>Welcome to Wifi Credentials Update page";
      content += "<form action=\"/scan\" method=\"POST\"><input type=\"submit\" value=\"scan\"></form>";
      content += ipStr;
      content += "<p>";
      content += st;
      content += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>";
      content += "</html>";
      hotspotServer.send(200, "text/html", content);
    });
    hotspotServer.on("/scan", []() {
      //setupAP();
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);

      content = "<!DOCTYPE HTML>\r\n<html>go back";
      hotspotServer.send(200, "text/html", content);
    });

    hotspotServer.on("/setting", []() {
      String qsid = hotspotServer.arg("ssid");
      String qpass = hotspotServer.arg("pass");
      if (qsid.length() > 0 && qpass.length() > 0) {
        Serial.println("clearing eeprom");
        for (int i = 0; i < 96; ++i) {
          EEPROM.write(i, 0);
        }
        Serial.println(qsid);
        Serial.println("");
        Serial.println(qpass);
        Serial.println("");

        Serial.println("writing eeprom ssid:");
        for (int i = 0; i < qsid.length(); ++i)
        {
          EEPROM.write(i, qsid[i]);
          Serial.print("Wrote: ");
          Serial.println(qsid[i]);
        }
        Serial.println("writing eeprom pass:");
        for (int i = 0; i < qpass.length(); ++i)
        {
          EEPROM.write(32 + i, qpass[i]);
          Serial.print("Wrote: ");
          Serial.println(qpass[i]);
        }
        EEPROM.commit();

        content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
        statusCode = 200;
        ESP.restart();
      } else {
        content = "{\"Error\":\"404 not found\"}";
        statusCode = 404;
        Serial.println("Sending 404");
      }
      hotspotServer.sendHeader("Access-Control-Allow-Origin", "*");
      hotspotServer.send(statusCode, "application/json", content);

    });
  }
}
