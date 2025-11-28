#include "MqttTelemetry.h"

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>

// ---------- MQTT CONFIG ----------
const MQTT_BROKER    = "broker.hivemq.com";
const MQTT_PORT      = 8884;                 // secure WebSocket port
const MQTT_PATH      = "/mqtt";              // required path for HiveMQ WS
const MQTT_TOPIC     = "hope/iot/circuit5/living-room/uno-r4/telemetry";
const MQTT_CLIENT_ID = "webdash_" + Math.random().toString(16).slice(2, 8);


// Under-the-hood MQTT objects (not visible to Sketch.ino)
static WiFiClient   gWifiClient;
static MqttClient   gMqttClient(gWifiClient);

// Forward declaration
static void connectToMqttBroker();

// ---------- PUBLIC API IMPLEMENTATION ----------

void mqttSetup() {
  // Wi-Fi must already be connected at this point.
  gMqttClient.setId(MQTT_CLIENT_ID);
  connectToMqttBroker();
}

void mqttLoop() {
  // Only try MQTT if Wi-Fi is actually up
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!gMqttClient.connected()) {
    connectToMqttBroker();
  }

  gMqttClient.poll();
}

void mqttPublishTelemetry(float temperature, float humidity, const String &status) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("mqttPublishTelemetry: WiFi not connected, skipping publish.");
    return;
  }

  if (!gMqttClient.connected()) {
    Serial.println("mqttPublishTelemetry: MQTT not connected, attempting reconnect...");
    connectToMqttBroker();
    if (!gMqttClient.connected()) {
      Serial.println("mqttPublishTelemetry: Reconnect failed, dropping message.");
      return;
    }
  }

  // Build JSON payload
  String payload = "{";
  payload += "\"deviceId\":\"uno-r4\",";
  payload += "\"temperature\":";
  payload += String(temperature, 1);
  payload += ",\"humidity\":";
  payload += String(humidity, 1);
  payload += ",\"status\":\"";
  payload += status;
  payload += "\"}";

  Serial.print("Publishing -> ");
  Serial.print(MQTT_TOPIC);
  Serial.print(" : ");
  Serial.println(payload);

  gMqttClient.beginMessage(MQTT_TOPIC);
  gMqttClient.print(payload);
  gMqttClient.endMessage();
}

// ---------- INTERNAL HELPER ----------

static void connectToMqttBroker() {
  Serial.print("Connecting to MQTT broker ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  while (!gMqttClient.connect(MQTT_BROKER, MQTT_PORT)) {
    Serial.print("MQTT connect failed, rc = ");
    Serial.println(gMqttClient.connectError());
    delay(2000);
  }

  Serial.println("MQTT connected.");
}