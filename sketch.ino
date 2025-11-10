// 1. Include Libraries
#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"

// 2. Define All Pins
// Sensor
#define DHTPIN 15
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Outputs
#define GREEN_LED_PIN 21
#define RED_LED_PIN   22
#define BUZZER_PIN    23  // NEW: Added the buzzer pin

// 3. Define Alert Thresholds
#define MIN_TEMP 18.0
#define MAX_TEMP 26.0
#define MAX_HUMIDITY 60.0 // NEW: Added humidity alert

// 4. Wi-Fi & MQTT Details
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "hope/iot_project/student123";

WiFiClient espClient;
PubSubClient client(espClient);

// 5. Setup Function (runs once)
void setup() {
  Serial.begin(115200);
  
  // Set up all our pins
  dht.begin();
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT); // NEW: Set buzzer pin as output

  // Connect to Wi-Fi
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");

  // Connect to MQTT
  client.setServer(mqtt_server, 1883);
}

// 6. MQTT Reconnect Logic
void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// 7. Loop Function (runs forever)
void loop() {
  // Check MQTT connection
  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.loop();

  // Wait 2 seconds between readings
  delay(2000); 

  // Read data from sensor
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  String alert_status = "normal"; // NEW: For sending status to dashboard

  // Check if sensor read failed
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  // --- NEW: Updated Alert Logic ---
  // Check if *any* condition is in the "alert" state
  if (temperature < MIN_TEMP || temperature > MAX_TEMP || humidity > MAX_HUMIDITY) {
    // --- ALERT STATE ---
    Serial.println("ALERT: Values out of range!");
    alert_status = "alert";

    // Turn on Red LED and Buzzer
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH); // NEW: Turn on buzzer
    
  } else {
    // --- NORMAL STATE ---
    Serial.println("Status: All values normal.");
    alert_status = "normal";

    // Turn on Green LED
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW); // NEW: Turn off buzzer
  }

  // --- Publish Data ---
  // Create JSON payload
  String payload = "{";
  payload += "\"temperature\":";
  payload += temperature;
  payload += ",";
  payload += "\"humidity\":";
  payload += humidity;
  payload += ",";
  payload += "\"status\":\"";  // NEW: Send the alert status
  payload += alert_status;
  payload += "\"";
  payload += "}";

  // Publish
  char payload_char[payload.length() + 1];
  payload.toCharArray(payload_char, payload.length() + 1);
  client.publish(mqtt_topic, payload_char);
  
  Serial.print("Published message: ");
  Serial.println(payload_char);
}
