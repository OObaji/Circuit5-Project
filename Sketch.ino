/*
  ======================================================================
  === IoT-Based Smart Home Temperature & Humidity Monitoring System  ===
  ===                                                                ===
  === BOARD:    Arduino UNO R4 WiFi                                  ===
  === SENSORS:  DHT11 (on D2)                                        ===
  === DISPLAY:  I2C LCD (on SDA/SCL)                                 ===
  === ALERTS:   Green LED (D10, ON), Red LED (D11, BLINKING)         ===
  ===           Buzzer (D12, BLINKING)                               ===
  === PROTOCOL: MQTT                                                 ===
  ======================================================================
*/

// --- 1. INCLUDE LIBRARIES ---
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <Arduino_JSON.h>
#include <WiFiS3.h>
#include <ArduinoMqttClient.h>

// --- 2. HARDWARE DEFINITIONS ---
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27, 20, 4); // Use 0x3F if 0x27 is blank

#define GREEN_LED_PIN 10  // Green LED (Normal) on D10
#define RED_LED_PIN   11  // Red LED (Alert) on D11
#define BUZZER_PIN    12  // Buzzer on D12

// --- 3. ALERT THRESHOLDS (Now mutable global variables) ---
float MIN_TEMP = 18.0;
float MAX_TEMP = 26.0;
float MAX_HUMIDITY = 60.0;

// --- 4. WI-FI & MQTT CONFIGURATION ---
// !!!
// !!! ENTER YOUR WI-FI NAME AND PASSWORD HERE !!!
// !!!
const char* ssid = "iPhone";
const char* password = "*Konami2003*";

// UPDATED: Using the reliable HiveMQ broker
const char* mqtt_broker = "broker.hivemq.com"; 
const int mqtt_port = 1883;

// !!! 
// !!! CHANGE "student181" TO A UNIQUE NAME FOR YOUR GROUP !!!
// !!!
const char* mqtt_topic = "hope/iot_project/student181";
const char* mqtt_control_topic = "hope/iot_project/student181/control";

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

// --- 5. TIMING & STATE VARIABLES ---
unsigned long lastSensorReadMillis = 0; // Timer for sensor reads
unsigned long lastBlinkMillis = 0;      // Timer for blinking
bool redLedState = LOW;                 // Tracks the blink state
String alertStatus = "normal";          // Global status tracker

// NEW: Master control for the device
bool isDeviceActive = true; 

// --- 6. SETUP FUNCTION (runs once) ---
void setup() {
  Serial.begin(9600);

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT); 
  
  lcd.init();
  lcd.backlight();
  lcd.print("IoT Monitor Starting...");

  dht.begin();

  // Connect to Wi-Fi
  lcd.setCursor(0, 1);
  lcd.print("Connecting to WiFi...");
  while (WiFi.begin(ssid, password) != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  lcd.clear();
  lcd.print("WiFi Connected!");
  Serial.println("WiFi Connected!");

  // Connect to MQTT
  lcd.setCursor(0, 1);
  lcd.print("Connecting to MQTT...");
  while (!mqttClient.connect(mqtt_broker, mqtt_port)) {
    Serial.print("MQTT connection failed! rc=");
    Serial.print(mqttClient.connectError());
    Serial.println(" retrying...");
    delay(2000);
  }
  lcd.clear();
  lcd.print("MQTT Connected!");
  Serial.println("MQTT Connected!");
  
  // Subscribe to the control topic
  mqttClient.subscribe(mqtt_control_topic);
  Serial.print("📡 Subscribed to control topic: ");
  Serial.println(mqtt_control_topic);
  
  delay(1000);
}

// --- 7. MAIN LOOP (runs forever) ---
void loop() {
  // Always maintain MQTT connection
  mqttClient.poll();

  // --- COMMAND PROCESSING (Polling for messages) ---
  if (mqttClient.connected()) {
    while (mqttClient.available()) {
      // Must read topic first, then read message payload
      String topic = mqttClient.messageTopic();
      String payload = mqttClient.readString();
      
      Serial.print("Incoming command: [");
      Serial.print(topic);
      Serial.print("] ");
      Serial.println(payload);

      // --- 1. Check for SETTINGS or POWER commands on the control topic ---
      if (topic.equals(mqtt_control_topic)) {
        JSONVar data = JSON.parse(payload);
        
        // Handle THRESHOLD updates (camelCase keys from dashboard)
        if (data.hasOwnProperty("maxTemp")) {
            MAX_TEMP = (float)(double)data["maxTemp"];
            MIN_TEMP = (float)(double)data["minTemp"];
            MAX_HUMIDITY = (float)(double)data["maxHumidity"];
            
            Serial.print("New Thresholds: T[");
            Serial.print(MIN_TEMP); Serial.print("-");
            Serial.print(MAX_TEMP); Serial.print("] H[<=");
            Serial.print(MAX_HUMIDITY); Serial.println("]");
        }
        
        // Handle POWER state updates
        if (data.hasOwnProperty("power")) {
            String power_state = (const char*)data["power"];
            if (power_state.equals("SYSTEM_ON")) {
                isDeviceActive = true;
                Serial.println("System Power ON.");
            } else if (power_state.equals("SYSTEM_OFF")) {
                isDeviceActive = false;
                Serial.println("System Power OFF.");
            }
        }
        
        // Handle DELETE action
        if (data.hasOwnProperty("action")) {
            String action = (const char*)data["action"];
            if (action.equals("DELETE_SYSTEM")) {
                isDeviceActive = false;
                Serial.println("SYSTEM PERMANENTLY DELETED/SHUTDOWN.");
            }
        }
        
        // If any command was received, force a sensor read and publish immediately
        lastSensorReadMillis = 0;
      }
    }
  }


  // --- SENSOR READING & PUBLISHING (Conditional on Device Active) ---
  if (isDeviceActive) {
      if (millis() - lastSensorReadMillis >= 10000) {
        lastSensorReadMillis = millis(); // Reset the timer

        float humidity = dht.readHumidity();
        float temperature = dht.readTemperature();

        if (isnan(humidity) || isnan(temperature)) {
          Serial.println(F("Failed to read from DHT sensor!"));
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Sensor Error!");
          alertStatus = "error"; // Set status to error
        } else {
          // Check alert logic using current MUTABLE thresholds
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
          
          // Publish to MQTT
          JSONVar payload;
          payload["temperature"] = temperature;
          payload["humidity"] = humidity;
          payload["status"] = alertStatus;
          String payloadString = JSON.stringify(payload);

          mqttClient.beginMessage(mqtt_topic);
          mqttClient.print(payloadString);
          mqttClient.endMessage();

          Serial.print("Published: ");
          Serial.println(payloadString);
        }
      }
  } else {
      // --- DEVICE INACTIVE STATE ---
      // Still display last status but don't read sensors or publish
      digitalWrite(GREEN_LED_PIN, LOW); // Green OFF
      digitalWrite(RED_LED_PIN, LOW);    // Red OFF
      digitalWrite(BUZZER_PIN, LOW);     // Buzzer OFF
      
      lcd.clear();
      lcd.setCursor(0, 0); 
      lcd.print("System is OFFLINE");
      lcd.setCursor(0, 1);
      lcd.print("Sent by Dashboard");
      
      // If we are off, skip the readMillis check to prevent publishing/reading
  }


  // --- LED & BUZZER CONTROL (Runs every loop for fast blinking) ---
  if (alertStatus == "alert" && isDeviceActive) {
    // --- ALERT STATE ---
    digitalWrite(GREEN_LED_PIN, LOW); // Green OFF

    // Blink logic for Red LED & Buzzer (every 500ms)
    if (millis() - lastBlinkMillis >= 500) {
      lastBlinkMillis = millis();
      redLedState = !redLedState; // Toggle state (HIGH -> LOW -> HIGH)
      digitalWrite(RED_LED_PIN, redLedState);
      digitalWrite(BUZZER_PIN, redLedState); // Buzzer syncs with LED
    }
    
  } else if (alertStatus == "normal" && isDeviceActive) {
    // --- NORMAL STATE ---
    digitalWrite(GREEN_LED_PIN, HIGH); // Green ON
    digitalWrite(RED_LED_PIN, LOW);    // Red OFF
    digitalWrite(BUZZER_PIN, LOW);     // Buzzer OFF
    
  } else {
    // --- OFF/ERROR STATE ---
    digitalWrite(GREEN_LED_PIN, LOW);  // Green OFF
    digitalWrite(RED_LED_PIN, LOW);    // Red OFF
    digitalWrite(BUZZER_PIN, LOW);     // Buzzer OFF
  }
}
