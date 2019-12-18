#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <time.h>
#include <math.h>

// --------------------------------------------------------------------------------------------
//        UPDATE CONFIGURATION TO MATCH IBM CLOUD MQTT SETTINGS
// --------------------------------------------------------------------------------------------

// Watson IoT connection details
#define MQTT_HOST "n3hvwe.messaging.internetofthings.ibmcloud.com"
#define MQTT_PORT 1883
#define MQTT_DEVICEID "d:n3hvwe:ESP8266:iot-device"
#define MQTT_USER "use-token-auth"
#define MQTT_TOKEN "ESPToken123"
#define MQTT_TOPIC "iot-2/evt/status/fmt/json"
#define MQTT_TOPIC_ALL "iot-2/type/+/id/+/evt/+/fmtjson" // Start serial console

#define MQTT_TOPIC_DISPLAY "iot-2/cmd/display/fmt/json"
#define MQTT_TOPIC_INTERVAL "iot-2/cmd/interval/fmt/json"
#define MQTT_TOPIC_SWITCH "iot-2/cmd/switch/fmt/json"

// --------------------------------------------------------------------------------------------
//     GPIO DEFINITIONS
// --------------------------------------------------------------------------------------------
#define RGB_PIN 5         //D1 GPIO pin the data line of RGB LED is connected to 
#define DHT_PIN 4         //D2 GPIO pin the data line of the DHT sensor is connected 
#define buttonPin  14    // D5 For the relay control signal
const int sensorIn = A0;
/*
  Scheduled Operation
*/
unsigned long previousMillis = 0;

/*
  Measuring AC Current Using ACS712
*/
int mVperAmp = 185; // use 100 for 20A Module and 66 for 30A Module
double Voltage = 0;
double VRMS = 0;
double AmpsRMS = 0;
unsigned long last_time =0;
unsigned long current_time =0;
char watt[5];

// Specify DHT11 (Blue) or DHT22 (White) sensor
#define DHTTYPE DHT11
#define NEOPIXEL_TYPE NEO_RGB + NEO_KHZ800

// Temperatures to set LED by (assume temp in C)
#define ALARM_COLD 0.0
#define ALARM_HOT 30.0
#define WARN_COLD 10.0
#define WARN_HOT 25.0

// Add WiFi connection information
char ssid[] = "ssid";     //  your network SSID (name)
char pass[] = "password";  // your network password

// --------------------------------------------------------------------------------------------
//        SHOULD NOT NEED TO CHANGE ANYTHING BELOW THIS LINE
// --------------------------------------------------------------------------------------------
Adafruit_NeoPixel pixel = Adafruit_NeoPixel(1, RGB_PIN, NEOPIXEL_TYPE);
DHT dht(DHT_PIN, DHTTYPE);

// MQTT objects
void mqtt_callback(char* topic, byte* payload, unsigned int length);
WiFiClient wifiClient;
PubSubClient mqtt(MQTT_HOST, MQTT_PORT, mqtt_callback, wifiClient);

// variables to hold data
StaticJsonDocument<100> jsonDoc;
JsonObject payload = jsonDoc.to<JsonObject>();
JsonObject status = payload.createNestedObject("d");s
StaticJsonDocument<100> jsonReceiveDoc;
static char msg[100];

float h = 0.0;
float t = 0.0;

unsigned char r = 0;
unsigned char g = 0;
unsigned char b = 0;

int32_t ReportingInterval = 5; // Reporting interval in seconds
int32_t ScheduledOperation= 0; 

  void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] : ");

  payload[length] = 0; // ensure valid content is zero terminated so can treat as c-string
  Serial.println((char *)payload);

  DeserializationError err = deserializeJson(jsonReceiveDoc, (char *)payload);
  if (err) {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(err.c_str());
  } else {
    JsonObject cmdData = jsonReceiveDoc.as<JsonObject>();
    if (0 == strcmp(topic, MQTT_TOPIC_SWITCH)) {
      int switchCommand = cmdData["switch"];
      Serial.print(">>>>>>>> SMART PLUG COMMAND excecuting: ");
      if (switchCommand) {
        Serial.println("TURN ON <<<<<<<");
        digitalWrite(buttonPin, HIGH);
        digitalWrite(LED_BUILTIN, LOW);
        jsonReceiveDoc.clear();
      } else {
        Serial.println("TURN OFF <<<<<<<");
        digitalWrite(buttonPin, LOW);
        digitalWrite(LED_BUILTIN, HIGH);
        jsonReceiveDoc.clear();
      }
    } else if (0 == strcmp(topic, MQTT_TOPIC_DISPLAY)) {
      //valid message received
      r = cmdData["r"].as<unsigned char>(); // this form allows you specify the type of the data you want from the JSON object
      g = cmdData["g"];
      b = cmdData["b"];
      jsonReceiveDoc.clear();
      pixel.setPixelColor(0, r, g, b);     pixel.show();
    } else if (0 == strcmp(topic, MQTT_TOPIC_INTERVAL)) {
      //valid message received
      if (cmdData["Interval"] != 0 && cmdData["Interval"] != NULL) {

        ScheduledOperation = cmdData["Interval"].as<int32_t>(); // this form allows you specify the type of the data you want from the JSON object
        Serial.print("Scheduling Timer: ");
        Serial.println(ScheduledOperation);
        jsonReceiveDoc.clear();
      }
      int switchCommand = cmdData["switch"];
      Serial.print(">>>>>>>> SMART PLUG COMMAND excecuting: ");
      if (switchCommand) {
        Serial.println("TURN ON <<<<<<<");
        digitalWrite(buttonPin, HIGH);
        digitalWrite(LED_BUILTIN, LOW);
        jsonReceiveDoc.clear();
      } else {
        Serial.println("TURN OFF <<<<<<<");
        digitalWrite(buttonPin, LOW);
        digitalWrite(LED_BUILTIN, HIGH);
        jsonReceiveDoc.clear();
      }
    } else {
      Serial.println("Unknown command received");
    }
  }
}

float getVPP()
{
  float result;

  int readValue;             //value read from the sensor
  int maxValue = 0;          // store max value here
  int minValue = 1024;          // store min value here

  uint32_t start_time = millis();
  while ((millis() - start_time) < 1000) //sample for 1 Sec
  {
    readValue = analogRead(sensorIn);
    // see if you have a new maxValue
    if (readValue > maxValue)
    {
      /*record the maximum sensor value*/
      maxValue = readValue;
    }
    if (readValue < minValue)
    {
      /*record the maximum sensor value*/
      minValue = readValue;
    }
  }
  // Subtract min from max
  result = ((maxValue - minValue) * 5.0) / 1024.0;

  return result;
}

void setup() {
  // Start serial console
  Serial.begin(115200);
  Serial.setTimeout(2000);
  while (!Serial) { }
  Serial.println();
  Serial.println("ESP8266 Sensor Application");

  WiFiManager wifiManager;
  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("Pluggy-AP");
    
  Serial.println("");
  Serial.println("WiFi Connected");

  // Start connected devices
  pinMode(buttonPin, OUTPUT);
  digitalWrite(buttonPin, LOW);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  dht.begin();
  pixel.begin();

  // Connect to MQTT - IBM Watson IoT Platform
  if (mqtt.connect(MQTT_DEVICEID, MQTT_USER, MQTT_TOKEN)) {
    Serial.println("MQTT Connected");
    mqtt.subscribe(MQTT_TOPIC_ALL);
  } else {
    Serial.println("MQTT Failed to connect!");
    ESP.reset();
  }
}

void loop() {
  mqtt.loop();
  while (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqtt.connect(MQTT_DEVICEID, MQTT_USER, MQTT_TOKEN)) {
      Serial.println("MQTT Connected");
      mqtt.subscribe(MQTT_TOPIC_DISPLAY);
      mqtt.subscribe(MQTT_TOPIC_INTERVAL);

      mqtt.loop();
    } else {
      Serial.println("MQTT Failed to connect!");
      delay(5000);
    }
  }
  h = dht.readHumidity(); 
  t = dht.readTemperature(); 

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
  } else {
    /* Control of the LED is now handles by the incoming command*/
    // Set RGB LED Colour based on temp
    b = (t < ALARM_COLD) ? 255 : ((t < WARN_COLD) ? 150 : 0);
    r = (t >= ALARM_HOT) ? 255 : ((t > WARN_HOT) ? 150 : 0);
    g = (t > ALARM_COLD) ? ((t <= WARN_HOT) ? 255 : ((t < ALARM_HOT) ? 150 : 0)) : 0;
    pixel.setPixelColor(0, r, g, b);
    pixel.show();

    Voltage = getVPP();
    VRMS = (Voltage / 2.0) * 0.707;
    AmpsRMS = (VRMS * 1000) / mVperAmp;
    Serial.print(AmpsRMS);
    Serial.println(" Amps RMS");

    float P = (AmpsRMS * 230);
    last_time = current_time;
    current_time = millis();
    float   Wh = Wh +  P * (( current_time - last_time) / 3600000.0) ;
    dtostrf(Wh, 4, 2, watt);
    Serial.print(watt);
    Serial.print(" Watt");
    Serial.println(Wh);

    // Print Message to console in JSON format
    status["temp"] = t; //Add the temperature data
    status["humidity"] = h; //Add humidity data

    if(P>25){
      status["switch"] = 1;
    }else{
      status["switch"] = 0;
    }
    status["current"] = AmpsRMS - 0.07; //0.07 to remove the hall effect offset
    status["power"] = P ;
    status["watt"] = watt;

    serializeJson(jsonDoc, msg, 100);
    Serial.println(msg);
    if (!mqtt.publish(MQTT_TOPIC, msg)) {
      Serial.println("MQTT Publish failed");
    }
    
    Serial.println();

    if(ScheduledOperation>0){
      unsigned long currentMillis = millis();
      digitalWrite(LED_BUILTIN, LOW);
      digitalWrite(buttonPin, HIGH);//buttonPin
      if (currentMillis - previousMillis >= (ScheduledOperation*1000)) {
        // save the last time you blinked the LED
        previousMillis = currentMillis;
        ScheduledOperation=0;
        digitalWrite(buttonPin, LOW);//buttonPin
        digitalWrite(LED_BUILTIN, HIGH);//buttonPin
      }
    }
    
    // Pause - but keep polling MQTT for incoming messages
    for (int32_t i = 0; i < ReportingInterval; i++) {
      mqtt.loop();
      delay(1000);
    }
  }
}
