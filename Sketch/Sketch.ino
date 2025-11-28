/*
  ======================================================================
  === Local Smart Home Temperature & Humidity Monitoring System      ===
  === with Wi-Fi Provisioning Portal + Modular MQTT Telemetry        ===
  ===                                                                ===
  === BOARD:    Arduino UNO R4 WiFi                                  ===
  === SENSORS:  DHT11 (on D2)                                        ===
  === DISPLAY:  I2C LCD (on SDA/SCL)                                 ===
  === ALERTS:   Green LED (D10, ON = OK), Red LED (D11, BLINK ALERT) ===
  === NETWORK:  WiFiS3 + ArduinoMqttClient → broker.hivemq.com       ===
  === TOPIC:    hope/iot/circuit5/living-room/uno-r4/telemetry       ===
  === JSON:     { deviceId, temperature, humidity, status }          ===
  ======================================================================
*/

// ---------- 1. LIBRARIES ----------
#include <WiFiS3.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

#include "WiFiProvisioning.h"
#include "MqttTelemetry.h"

// ---------- 2. HARDWARE PINS & OBJECTS ----------

// DHT11
#define DHTPIN   2
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);

// LEDs
#define GREEN_LED_PIN 10
#define RED_LED_PIN   11

// LCD: adjust address if needed (0x27/0x3F are common)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------- 3. ALERT THRESHOLDS ----------
#define MIN_TEMP      18.0
#define MAX_TEMP      26.0
#define MAX_HUMIDITY  60.0

// ---------- 4. STATE ----------
unsigned long lastSensorReadMillis = 0;
unsigned long lastBlinkMillis      = 0;
bool          redLedState          = LOW;
String        alertStatus          = "normal";

// Wi-Fi credentials (managed by WiFiProvisioning module)
WifiCredentials gWifiCreds;

// =====================================================================
//                        5. SETUP & LOOP
// =====================================================================

void setup() {
  Serial.begin(9600);

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN,   OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Local Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Booting...");

  dht.begin();

  // --- Wi-Fi provisioning logic ---
  bool haveCreds = loadWifiCredentials(gWifiCreds);
  if (!haveCreds) {
    Serial.println("No stored Wi-Fi credentials. Entering config portal...");
    lcd.clear();
    lcd.print("AP: UNO-R4-SETUP");
    lcd.setCursor(0, 1);
    lcd.print("Config via WiFi");
    runProvisioningPortal(gWifiCreds);  // blocks inside AP/HTTP loop
    while (true) { delay(1000); }       // wait for user reset
  }

  if (!connectWithStoredCredentials(gWifiCreds, 20000)) {
    Serial.println("Failed to connect, starting config portal...");
    lcd.clear();
    lcd.print("WiFi failed");
    lcd.setCursor(0, 1);
    lcd.print("Open AP to fix");
    runProvisioningPortal(gWifiCreds);
    while (true) { delay(1000); }
  }

  lcd.clear();
  lcd.print("WiFi Connected!");
  Serial.println("WiFi Connected!");

  // --- MQTT setup (now handled by module) ---
  mqttSetup();

  lcd.clear();
  lcd.print("System Ready");
  lcd.setCursor(0, 1);
  lcd.print("Normal Mode");
}

void loop() {
  // --- Keep Wi-Fi & MQTT alive ---

  // If Wi-Fi drops, try reconnecting using stored credentials
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped, reconnecting...");
    connectWithStoredCredentials(gWifiCreds, 20000);
  }

  // Let MQTT module handle its own connection/polling logic
  mqttLoop();

  // --- Sensor logic every 10 seconds ---

  if (millis() - lastSensorReadMillis >= 10000) {
    lastSensorReadMillis = millis();

    float humidity    = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      lcd.clear();
      lcd.print("Sensor Error!");
      alertStatus = "alert";
    } else {
      // Determine alert status
      if (temperature < MIN_TEMP || temperature > MAX_TEMP || humidity > MAX_HUMIDITY) {
        alertStatus = "alert";
      } else {
        alertStatus = "normal";
      }

      // Update LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Temp:");
      lcd.print(temperature, 1);
      lcd.print((char)223);
      lcd.print("C");

      lcd.setCursor(0, 1);
      lcd.print("Hum:");
      lcd.print(humidity, 1);
      lcd.print("% ");

      if (alertStatus == "alert") {
        lcd.print("ALERT");
      } else {
        lcd.print("OK");
      }

      // Publish telemetry via MQTT module
      mqttPublishTelemetry(temperature, humidity, alertStatus);
    }
  }

  // --- LED alert behaviour ---

  if (alertStatus == "alert") {
    if (millis() - lastBlinkMillis >= 500) {
      lastBlinkMillis = millis();
      redLedState = !redLedState;
      digitalWrite(RED_LED_PIN, redLedState ? HIGH : LOW);
    }
    digitalWrite(GREEN_LED_PIN, LOW);
  } else {
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(GREEN_LED_PIN, HIGH);
  }
}
