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
const char* passphrase = "Default passord";
String st;
String content;
String esid;
String epass = "";

#define HOTSPOT_LED 32
#define WIFI_LED 33
#define HOTSPOT_BUTTON 15

#define LED_BLINK_DELAY 2000
#define HUMIDITY_READ_DELAY 5000
#define PARAMS_READ_DELAY 2000

#define MAX_FAILED_READS_IN_ROW 2

#define DHTPIN 4     // Digital pin connected to the DHT sensor

#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

#define ONBOARD_LED 2
#define RELAY_PIN 5

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

float h;
float t;
float f;

float new_h;
float new_t;
float new_f;

float targetTemp;
float targetHumidity;

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
  pinMode(RELAY_PIN, OUTPUT);

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

  /////////////////////////////////////////////////
  // Route for root / web page
  controlServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/controller.html","text/html");
  });
  
  // Route to load style.css file
  controlServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });

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
  controlServer.on("/setTemp",HTTP_GET,[] (AsyncWebServerRequest *request){
    String inputMessage;
    inputMessage = request->getParam("value")->value();
    writeFile(SPIFFS, "/tempInput.txt", inputMessage.c_str());
    targetTemp = inputMessage.toFloat();
  });

  controlServer.on("/setHumidity",HTTP_GET,[] (AsyncWebServerRequest *request){
    String inputMessage;
    inputMessage = request->getParam("value")->value();
    writeFile(SPIFFS, "/humidityInput.txt", inputMessage.c_str());
    targetHumidity = inputMessage.toFloat();
  });

}


String readFile(fs::FS &fs, const char * path){
//  Serial.printf("Reading file: %s\r\n", path);/
  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
//    Serial.println("- empty file or failed to open file");/
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
//    Serial.println("- failed to open file for writing");/
    return;
  }
  if(file.print(message)){
//    Serial.println("- file written");/
  } else {
//    Serial.println("- write failed");/
  }
  file.close();
}

//String processor(const String& var){
//  //Serial.println(var);
//  if(var == "tempInput"){
//    return readFile(SPIFFS, "/tempInput.txt");
//  }
//  else if(var == "humidityInput"){
//    return readFile(SPIFFS, "/humidityInput.txt");
//  }
//
//  return String();
//}

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

unsigned long currentTimeParams = millis();
unsigned long previousTimeParams = 0;

void loop() {

  currentTimeParams = millis();

  if (currentTimeParams - previousTimeParams > PARAMS_READ_DELAY){
    previousTimeParams = currentTimeParams;
    
    targetTemp = readFile(SPIFFS, "/tempInput.txt").toFloat();
  
    targetHumidity = readFile(SPIFFS, "/humidityInput.txt").toFloat();

  }


  currentTimeLED = millis();

  if (currentTimeLED - previousTimeLED > LED_BLINK_DELAY){
    previousTimeLED = currentTimeLED;
    led_on = !led_on;
  }

  currentTimeDHT = millis();

  if(currentTimeDHT - previousTimeDHT > HUMIDITY_READ_DELAY){
    previousTimeDHT = currentTimeDHT;
    
    new_h = dht.readHumidity();
    // Read temperature as Celsius (the default)
    new_f = dht.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    new_t = dht.readTemperature(true);
    
     // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t) || isnan(f)) {
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
      f = new_f;
    }

    Serial.print(F("Humidity: "));
    Serial.print(h);
    Serial.print(F("%  Temperature: "));
    Serial.print(t);
    Serial.print(F("°C "));
    Serial.print(f);
    Serial.println(F("°f "));
      
  }

  // ADD OUT SIGNAL HERE 

  //  if(h < PARAM_TARGET_HUMIDITY - low_tolerance){
//    digitalWrite(ONBOARD_LED, HIGH);
//    digitalWrite(RELAY_PIN,HIGH); // Set output to 1
//    
//  } else if(h > PARAM_TARGET_HUMIDITY + high_tolerance){
//    digitalWrite(ONBOARD_LED, LOW);
//    digitalWrite(RELAY_PIN,LOW); // Set output to 1
//  }
  
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


//----------------------------------------------- Fuctions used for WiFi credentials saving and connecting to it which you do not need to change
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
  WiFi.softAP("HumidiController", "");
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
