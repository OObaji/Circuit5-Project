// MqttTelemetry.cpp
#include <ArduinoMqttClient.h>
#include <WiFiS3.h>

#include "MqttTelemetry.h"

// --- 1. MQTT CONFIG FOR UNO R4 (DEVICE SIDE, TCP, NOT WEBSOCKETS) ---

// Broker host and port (plain MQTT over TCP)
static const char MQTT_BROKER[] = "broker.hivemq.com";
static const int  MQTT_PORT     = 1883; // device uses normal MQTT, not WebSockets

// Topic the UNO publishes to
static const char MQTT_TOPIC[]  = "hope/iot/circuit5/living-room/uno-r4/telemetry";

// Client ID for this device (any unique-ish string is fine)
static const char MQTT_CLIENT_ID[] = "uno-r4-living-room";

// --- 2. GLOBAL MQTT OBJECTS ---

// WiFi client used by MQTT
static WiFiClient wifiClient;

// ArduinoMqttClient instance
static MqttClient gMqttClient(wifiClient);

// Forward declaration of internal helper
static void connectToMqttBroker();

// --- 3. PUBLIC API IMPLEMENTATIONS ----------------------------------

void mqttSetup() {
  Serial.println("MQTT: Initialising client...");

  // Set client ID and keepalive
  gMqttClient.setId(MQTT_CLIENT_ID);
  gMqttClient.setKeepAliveInterval(60);

  // Optional: username/password for brokers that need it
  // (HiveMQ public broker doesn't, but this is harmless)
  gMqttClient.setUsernamePassword("", "");

  // Connect to broker (host + port) in helper
  connectToMqttBroker();
}


void mqttLoop() {
  // Keep MQTT connection alive
  if (!gMqttClient.connected()) {
    connectToMqttBroker();
  }

  gMqttClient.poll();
}

void mqttPublishTelemetry(float temperature, float humidity, const String &status) {
  if (!gMqttClient.connected()) {
    connectToMqttBroker();
    if (!gMqttClient.connected()) {
      Serial.println("MQTT: still not connected, skipping telemetry publish.");
      return;
    }
  }

  // Build JSON payload
  String payload = "{";
  payload += "\"deviceId\":\"uno-r4-living-room\",";
  payload += "\"temperature\":";
  payload += String(temperature, 2);
  payload += ",";
  payload += "\"humidity\":";
  payload += String(humidity, 2);
  payload += ",";
  payload += "\"status\":\"";
  payload += status;
  payload += "\"}";

  Serial.print("MQTT: Publishing to ");
  Serial.print(MQTT_TOPIC);
  Serial.print(" => ");
  Serial.println(payload);

  gMqttClient.beginMessage(MQTT_TOPIC);
  gMqttClient.print(payload);
  gMqttClient.endMessage();
}

// --- 4. INTERNAL HELPER ---------------------------------------------

static void connectToMqttBroker() {
  Serial.print("MQTT: Connecting to broker ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  int attempts = 0;
  while (!gMqttClient.connect(MQTT_BROKER, MQTT_PORT)) {
    Serial.print("MQTT connect failed, error code = ");
    Serial.println(gMqttClient.connectError());

    attempts++;
    if (attempts >= 5) {
      Serial.println("MQTT: giving up for now, will retry in loop.");
      return;
    }

    delay(2000);
  }

  Serial.println("MQTT: Connected to HiveMQ broker.");
}
