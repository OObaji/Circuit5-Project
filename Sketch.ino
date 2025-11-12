/*
  ======================================================================
  === IoT-Based Smart Home Temperature & Humidity Monitoring System  ===
  ===                                                                ===
  === BOARD:    Arduino UNO R4 WiFi                                  ===
  === SENSORS:  DHT11 (on D2)                                        ===
  === DISPLAY:  I2C LCD (on SDA/SCL)                                 ===
  === ALERTS:   Green LED (D10, ON), Red LED (D11, BLINKING)         ===
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

#define GREEN_LED_PIN 10  // Green LED on D10
#define RED_LED_PIN   11  // Red LED on D11

// --- 3. ALERT THRESHOLDS ---
#define MIN_TEMP 18.0
#define MAX_TEMP 26.0
#define MAX_HUMIDITY 60.0

// --- 4. WI-FI & MQTT CONFIGURATION ---
const char* ssid = "VM3574649";
const char* password = "rr7wxNskPwfj";
const char* mqtt_broker = "broker.hivemq.com";
const int mqtt_port = 1883;
// !!! CHANGE "student181" TO A UNIQUE NAME FOR YOUR GROUP !!!
const char* mqtt_topic = "hope/iot_project/student181";

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

// --- 5. TIMING & STATE VARIABLES ---
unsigned long lastSensorReadMillis = 0; // Timer for sensor reads
unsigned long lastBlinkMillis = 0;      // Timer for blinking
bool redLedState = LOW;                 // Tracks the blink state
String alertStatus = "normal";          // Global status tracker

// --- 6. SETUP FUNCTION (runs once) ---
void setup() {
  Serial.begin(9600);

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  
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
  delay(1000);
}

// --- 7. MAIN LOOP (runs forever) ---
void loop() {
  // Always maintain MQTT connection
  mqttClient.poll();

  // --- SENSOR READING & PUBLISHING (Every 10 seconds) ---
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
      // Check logic
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
      lcd.print((char)223); "C";
      
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
    // --- SENSOR ERROR STATE ---
    digitalWrite(GREEN_LED_PIN, LOW);  // Green OFF
    digitalWrite(RED_LED_PIN, LOW);    // Red OFF
  }
}
