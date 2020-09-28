// Fan in bathroom Generic ESP8266 with 4M/1M OTA
// Written for a Sonoff TH-10 with sensor connected for
// a bathroom fan which is humidity controlled and can
// be activated via MQTT when you have farted

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include "secrets.h"

/************ User Configuration ******************/
// Wifi
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
IPAddress ip(192, 168, 1, 40); // update this to the desired IP Address
IPAddress dns(192, 168, 1, 1); // set dns to local
IPAddress gateway(192, 168, 1, 1); // set gateway to match your network
IPAddress subnet(255, 255, 255, 0); // set subnet mask to match your network

// MQTT
const char* mqttServer           = MQTT_SERVER;
const char* mqttUsername         = MQTT_USER;
const char* mqttPassword         = MQTT_PASSWORD;
const int   mqttPort             = 1883;
const char* mqttTopicAnnounce    = "fan/announce";
const char* mqttTopicState       = "fan/state";
const char* mqttTopicRising      = "fan/rising";
const char* mqttTopicFalling     = "fan/falling";
const char* mqttTopicSet         = "fan/set";
const char* mqttTopicSetRising   = "fan/set/rising";
const char* mqttTopicSetFalling  = "fan/set/falling";
const char* mqttTopicSetReport   = "fan/set/report";
const char* mqttTopicTemperature = "fan/temperature";
const char* mqttTopicHumidity    = "fan/humidity";

// Host Name and Over The Air reprogramming password
const char* hostName = "fan";
const char* otaPassword = OTA_PASSWORD;

// Time settings
const unsigned long fanInterval = 300000UL; // Time fans run when manually started in milliseconds
const unsigned long sensorInterval = 5000UL; //How often the sensor is read 5 seconds
const unsigned long sensorReportingInterval = 2 * 60 * 1000; // how often sensor is reported in milliseconds

// Numbers are GPIO numbers - used in Sonoff TH10
#define RELAY     12
#define REDLED    13
#define DHTPIN    14         // Pin which is connected to the DHT sensor.
#define BUTTON    0

// Uncomment the type of sensor in use:
// DHT11, DHT22 (AM2302), or DHT21 (AM2301)
#define DHTTYPE  DHT21

/****** End of configuration ******/

// Globals
unsigned long mqttReconnectTimer = 0;
unsigned long wifiReconnectTimer = 0;
unsigned long lastSensorReport = 0;
unsigned long previousMillis = 0;
unsigned long fanOnMillis;
unsigned long sensorMillis;

float temperature;
float humidity;
float humidityThresholdHigh = 70.0;
float humidityThresholdLow = 50.0;

bool fanStatus = false;
int debounceCounter = 0;

// Inits
ESP8266WebServer server(80);

WiFiClient espClient;
PubSubClient client(espClient);

DHT_Unified dht(DHTPIN, DHTTYPE);


// MQTT Callback - when we receive MQTT messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {

    // Creating safe local copies of topic and payload
    // enables publishing MQTT within the callback function
    // We avoid functions using malloc to avoid memory leaks
    
    char topicCopy[strlen(topic) + 1];
    strcpy(topicCopy, topic);
    
    char message[length + 1];
    for (int i = 0; i < length; i++) message[i] = (char)payload[i];
    
    message[length] = '\0';
  
    if ( strcmp(topicCopy, mqttTopicSet) == 0 ) {
        if ( strcmp(message, "on") == 0 ) fanOn();
        if ( strcmp(message, "off") == 0 ) fanOff();
    }
    else if ( strcmp(topicCopy, mqttTopicSetRising ) == 0 ) {
        float rising;
        rising =  (float)strtol(message, NULL, 10);
        if ( rising > 30 && rising < 100 ) {
            humidityThresholdHigh = rising;
            reportThresholds();
        }
    }
    else if ( strcmp(topicCopy, mqttTopicSetFalling ) == 0 ) {
        float falling;
        falling =  (float)strtol(message, NULL, 10);
        if ( falling > 20 && falling < 90 ) {
            humidityThresholdLow = falling;
            reportThresholds();
        }
    }
    else if ( strcmp(topicCopy, mqttTopicSetReport) == 0 ) {
        reportThresholds();
    } 
    else {
        return;
    }

}

// Connect or reconnect to the MQTT server and subscribe to topics
bool mqttConnect() {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(hostName, mqttUsername, mqttPassword)) {
        Serial.println("connected");
        // Once connected, publish an announcement...
        client.publish(mqttTopicAnnounce, "connected");
        client.subscribe(mqttTopicSet);
        client.subscribe(mqttTopicSetRising);
        client.subscribe(mqttTopicSetFalling);
        client.subscribe(mqttTopicSetReport);
    }

    return client.connected();
}

// Sends a HTML page to client
void sendWebPage(void) {

    String webPage = "";
   
    webPage += "<html><head></head><body>\n";
    webPage += "<h1>Bathroom Fan</h1>";
    webPage += "<p>Fan is ";
    webPage += fanStatus;
    webPage += "</p>";
    webPage += "<p>Temperature is ";
    webPage += temperature;
    webPage += " deg C</p>";
    webPage += "<p>Humidity is ";
    webPage += humidity;
    webPage += " % relative</p>";
    webPage += "<p><a href=\"/on\"><button>ON</button></a> <a href=\"/off\"><button>OFF</button></a></p>\n";
    webPage += "<p><a href=\"/\"><button>Status Only</button></a></p>\n";
    webPage += "</body></html>\n";
    
    server.send(200, "text/html", webPage);
}

void fanOn(void) {
    fanStatus = true;
    digitalWrite(RELAY, HIGH);
    client.publish(mqttTopicState, "on", true );
}

void fanOff(void) {
    fanStatus = false;
    digitalWrite(RELAY, LOW);
    client.publish(mqttTopicState, "off", true );
}

// Report threshold values via MQTT
void reportThresholds(void) {
    char mqttbuf[10];
    itoa( (int)round(humidityThresholdHigh), mqttbuf, 10 );
    client.publish(mqttTopicRising, mqttbuf, false );
    itoa( (int)round(humidityThresholdLow), mqttbuf, 10 );
    client.publish(mqttTopicFalling, mqttbuf, false );
}

void setup_wifi() {
  
    Serial.print(F("Setting static ip to : "));
    Serial.println(ip);
   
    // Connect to WiFi network
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
  
    // ESP8266 does not follow same order as Arduino
    WiFi.mode(WIFI_STA);
    WiFi.config(ip, gateway, subnet, dns); 
    WiFi.begin(ssid, password);
   
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");  
}


void setup(void) 
{
    // Setup UART
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Booting..");

    // Init the hardware
    pinMode(RELAY, OUTPUT);
    pinMode(REDLED, OUTPUT);
    digitalWrite(RELAY, LOW);
    digitalWrite(REDLED, LOW);
    
    // Setup WIFI
    setup_wifi();
    wifiReconnectTimer = millis();
 
    digitalWrite(REDLED, HIGH);

    // Setup webserver
    server.on("/", [](){
        sendWebPage();
    });
  
    server.on("/on", [](){
        fanOn();
        sendWebPage();
    });

    server.on("/off", [](){
        fanOff();
        sendWebPage();
    });

    server.begin();

    // Setting up DHT
    dht.begin();

    // Set some reasonable default start values
    temperature = 0.0;
    humidity = 0.0;

    // Init the fan timer
    fanOnMillis = millis();

    
    // Setup MQTT
    client.setServer(mqttServer, mqttPort);
    client.setCallback(mqttCallback);
    mqttReconnectTimer = 0;
    mqttConnect();
    
    // Setup Over The Air (OTA) reprogramming
    ArduinoOTA.setHostname(hostName);
    ArduinoOTA.setPassword(otaPassword);
    
    ArduinoOTA.onStart([]() {
        Serial.end();
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";
    
        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    });
    ArduinoOTA.onEnd([]() {  });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {  });
    ArduinoOTA.onError([](ota_error_t error) {  });
    ArduinoOTA.begin();
}
 
void loop()
{
    ESP.wdtFeed();
    delay(1);
    
    unsigned long currentTime = millis();

    // Handle WiFi
    if ( WiFi.status() != WL_CONNECTED ) {
        if ( currentTime - wifiReconnectTimer > 20000 )
            ESP.reset();
    } else
        wifiReconnectTimer = currentTime;

    // Handle MQTT
    if (!client.connected()) {
        if ( currentTime - mqttReconnectTimer > 5000 ) {
            mqttReconnectTimer = currentTime;
            if ( mqttConnect() ) {
                mqttReconnectTimer = 0;
            }
        }
    } else {
      client.loop();
    }

    // Handle Webserver Requests
    server.handleClient();
    // Handle OTA requests
    ArduinoOTA.handle();
    
    // Reset fan timer if fan is not running
    if (!fanStatus) fanOnMillis = currentTime;

    // Check sensors as given by sensorInterval
    if ( currentTime - sensorMillis > sensorInterval ) {
        sensorMillis = currentTime;
        sensors_event_t event;
        dht.temperature().getEvent(&event);

        if (isnan(event.temperature)) {
            Serial.println("Error reading temperature!");
        }
        else {
            temperature = event.temperature;
        }
        
        // Get humidity event and print its value.
        dht.humidity().getEvent(&event);
        if (isnan(event.relative_humidity)) {
            Serial.println("Error reading humidity!");
        }
        else {
            humidity = event.relative_humidity;
        }

        // Turn fan on if above threshold
        if ( humidity >= humidityThresholdHigh ) {
            if ( !fanStatus ) fanOn();
            fanOnMillis = currentTime;      // Keep timer at start value
        }

        // We turn the fan off after it has run the fanInterval multiplied
        // by 3 if the humidity it above lower threshold
        if ( fanStatus ) {
            int intervalMultiplier = humidity > humidityThresholdLow ? 3 : 1;
            if ( ( currentTime - fanOnMillis ) > ( fanInterval * intervalMultiplier ) ) {
                fanOff();
            }
        }

        // Report sensors to MQTT
        if ( currentTime - lastSensorReport > sensorReportingInterval ) {
            lastSensorReport = currentTime;
            char mqttbuf[10];
            itoa( (int)round(humidity), mqttbuf, 10 );
            client.publish(mqttTopicHumidity, mqttbuf, true );
            dtostrf(temperature, 3, 1, mqttbuf);
            client.publish(mqttTopicTemperature, mqttbuf, true );
        }
        
    }

    // Debounce the button on the Sonoff and toggle fan
    if ( debounceCounter == 0 && !digitalRead(BUTTON) ) {
        if ( fanStatus ) {
            fanOff();
        } else {
            fanOn();
            fanOnMillis = currentTime;      // Set timer at start value
        }
        debounceCounter = 300;
    }
    if ( debounceCounter > 0 ) debounceCounter--;
}
