/*
  ======================================================================
  === Local Smart Home Temperature & Humidity Monitoring System      ===
  ===                                                                ===
  === FUNCTION: Reads DHT11, checks hardcoded thresholds, outputs    ===
  ===           to LCD, controls Green/Red Alert LEDs.               ===
  ===                                                                ===
  === BOARD:    Arduino UNO R4 WiFi                                  ===
  === SENSORS:  DHT11 (on D2)                                        ===
  === DISPLAY:  I2C LCD (on SDA/SCL)                                 ===
  === ALERTS:   Green LED (D10, ON), Red LED (D11, BLINKING)         ===
  ===                                                                ===
  ======================================================================
*/

// --- 1. INCLUDE LIBRARIES ---
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <WiFiS3.h> // RESTORED: Wi-Fi Library
#include <ArduinoMqttClient.h> // RESTORED: MQTT Library
// Removed: #include <Arduino_JSON.h>
// Removed: #include <ArduinoMqttClient.h>

// --- 2. HARDWARE DEFINITIONS ---
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27, 20, 4); // Use 0x3F if 0x27 is blank

#define GREEN_LED_PIN 10  // Green LED (Normal) on D10
#define RED_LED_PIN   11  // Red LED (Alert) on D11

// --- 3. ALERT THRESHOLDS (Now fixed, local constants) ---
#define MIN_TEMP 18.0
#define MAX_TEMP 26.0
#define MAX_HUMIDITY 60.0

// --- 4. WI-FI CONFIGURATION (RESTORED) ---
// !!!
// !!! ENTER YOUR WI-FI NAME AND PASSWORD HERE !!!
// !!!
const char* ssid = "Elanet-228002B7";
const char* password = "tyycc9296j";

// --- 4b. MQTT CONFIGURATION ---
const char mqttBroker[]   = "broker.hivemq.com";
int        mqttPort       = 1883;                      // same as Python PORT
const char mqttTopic[]    = "hope/iot/circuit5/living-room/uno-r4/telemetry"; // updated topic structure for better security & scalability
const char mqttClientId[] = "";     // any unique ID

// Create WiFi + MQTT client objects
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);


// --- 5. STATE VARIABLES ---
unsigned long lastSensorReadMillis = 0; // Timer for sensor reads
unsigned long lastBlinkMillis = 0;      // Timer for blinking
bool redLedState = LOW;                 // Tracks the blink state
String alertStatus = "normal";          // Global status tracker

// --- 5b. FORWARD DECLARATIONS FOR MQTT HELPERS ---
void connectToMqtt();
void publishSensorData(float temperature, float humidity, const String &status);

// --- 6. SETUP FUNCTION (runs once) ---
void setup() {
  Serial.begin(9600);

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  
  lcd.init();
  lcd.backlight();
  lcd.print("Local Monitor Starting...");

  dht.begin();
  
  // --- CONNECT TO WI-FI (RESTORED) ---
  lcd.setCursor(0, 1);
  lcd.print("Connecting to WiFi...");
  while (WiFi.begin(ssid, password) != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  lcd.clear();
  lcd.print("WiFi Connected!");
  Serial.println("WiFi Connected!");
  // --- END WI-FI SETUP ---
  
  // --- MQTT SETUP ---
  mqttClient.setId(mqttClientId);   // use our chosen client ID
  connectToMqtt();                  // try to connect to broker

  Serial.println("System Ready: Local + MQTT Mode");

  // Serial.println("System Ready: Local Mode"); // OLD: Replaced with Local + MQTT
  
  delay(1000);
}

// --- 7. MAIN LOOP (runs forever) ---
void loop() {

    // --- KEEP WIFI & MQTT CONNECTIONS ALIVE (NEW) ---
  if (WiFi.status() != WL_CONNECTED) {
    // Try to reconnect Wi-Fi if it drops
    while (WiFi.begin(ssid, password) != WL_CONNECTED) {
      Serial.print(".");
      delay(500);
    }
    Serial.println("WiFi reconnected");
  }

  if (!mqttClient.connected()) {
    connectToMqtt();  // Reconnect to MQTT broker if needed
  }
  mqttClient.poll();  // Process incoming/outgoing MQTT packets


  // --- SENSOR READING (Every 10 seconds) ---
  if (millis() - lastSensorReadMillis >= 10000) {
    lastSensorReadMillis = millis(); // Reset the timer

    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Sensor Error!");
      alertStatus = "error"; 
    } else {
      // Check alert logic using local #define thresholds
      if (temperature < MIN_TEMP || temperature > MAX_TEMP || humidity > MAX_HUMIDITY) {
        alertStatus = "alert";
      } else {
        alertStatus = "normal";
      }

      // Update LCD Display
      lcd.clear();
      lcd.setCursor(0, 0); // Row 0
      lcd.print("Temp:     ");
      lcd.print(temperature, 1);
      lcd.print((char)223); // Degree symbol
      lcd.print("C");
      
      lcd.setCursor(0, 1); // Row 1
      lcd.print("Humidity: ");
      lcd.print(humidity, 1);
      lcd.print("%");

      lcd.setCursor(0, 2); // Row 2
      lcd.print("Status:   ");
      lcd.print(alertStatus);
      
      Serial.print("Read: T=");
      Serial.print(temperature);
      Serial.print("C, H=");
      Serial.print(humidity);
      Serial.print("%, Status=");
      Serial.println(alertStatus);

      publishSensorData(temperature, humidity, alertStatus);
    }
  }


  // --- LED CONTROL (Runs every loop for fast blinking) ---
  if (alertStatus == "alert") {
    // --- ALERT STATE ---
    digitalWrite(GREEN_LED_PIN, LOW); // Green OFF

    // Blink logic for Red LED (every 500ms)
    if (millis() - lastBlinkMillis >= 500) {
      lastBlinkMillis = millis();
      redLedState = !redLedState; // Toggle state (HIGH -> LOW -> HIGH)
      digitalWrite(RED_LED_PIN, redLedState);
    }
    
  } else if (alertStatus == "normal") {
    // --- NORMAL STATE ---
    digitalWrite(GREEN_LED_PIN, HIGH); // Green ON
    digitalWrite(RED_LED_PIN, LOW);    // Red OFF
    
  } else {
    // --- ERROR STATE ---
    digitalWrite(GREEN_LED_PIN, LOW);  // Green OFF
    digitalWrite(RED_LED_PIN, LOW);    // Red OFF
  }
}


// --- 8. MQTT HELPER FUNCTIONS (NEW) ---

void connectToMqtt() {
  Serial.print("Connecting to MQTT at ");
  Serial.print(mqttBroker);
  Serial.print(":");
  Serial.println(mqttPort);

  while (!mqttClient.connect(mqttBroker, mqttPort)) {
    Serial.print("MQTT connect failed, error code = ");
    Serial.println(mqttClient.connectError());
    Serial.println("Retrying in 1 second...");
    delay(1000);
  }

  Serial.println("MQTT Connected!");

  // OPTIONAL: later you can subscribe to control topic here:
  // mqttClient.subscribe("hope/iot_project/student123/cmd");
}


void publishSensorData(float temperature, float humidity, const String &status) {
  // Build a JSON-like payload manually
  String payload = "{";
  payload += "\"deviceId\":\"uno-r4\"";
  payload += ",\"temperature\":";
  payload += String(temperature, 1);
  payload += ",\"humidity\":";
  payload += String(humidity, 1);
  payload += ",\"status\":\"";
  payload += status;
  payload += "\"}";

  Serial.print("Publishing to ");
  Serial.print(mqttTopic);
  Serial.print(" -> ");
  Serial.println(payload);

  mqttClient.beginMessage(mqttTopic);
  mqttClient.print(payload);
  mqttClient.endMessage();
}