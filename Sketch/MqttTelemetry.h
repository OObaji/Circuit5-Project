#pragma once

#include <Arduino.h>

// Initialise MQTT after Wi-Fi is connected.
// Sets up client ID and connects to the broker.
void mqttSetup();

// Call this once per loop().
// Keeps MQTT connection alive and reconnects if needed.
void mqttLoop();

// Publish the temperature/humidity/status telemetry JSON
// to the configured MQTT topic.
void mqttPublishTelemetry(float temperature, float humidity, const String &status);
