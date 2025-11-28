/*
  ======================================================================
  === Local Smart Home Temperature & Humidity Monitoring System      ===
  === with Wi-Fi Provisioning Portal                                 ===
  ===                                                                ===
  === FUNCTION: Reads DHT11, checks hardcoded thresholds, outputs    ===
  ===           to LCD, controls Green/Red Alert LEDs.               ===
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
#include <ArduinoMqttClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

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

// ---------- 4. MQTT CONFIG ----------
const char mqttBroker[]   = "broker.hivemq.com";
int        mqttPort       = 1883;
const char mqttTopic[]    = "hope/iot/circuit5/living-room/uno-r4/telemetry";
const char mqttClientId[] = "uno-r4-living-room";

// WiFi + MQTT objects
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

// ---------- 5. STATE ----------
unsigned long lastSensorReadMillis = 0;
unsigned long lastBlinkMillis      = 0;
bool          redLedState          = LOW;
String        alertStatus          = "normal";

// Forward declarations
void connectToMqtt();
void publishSensorData(float temperature, float humidity, const String &status);

// =====================================================================
//               6. WIFI PROVISIONING STRUCTURES + HELPERS
// =====================================================================

// AP used for config mode
const char AP_SSID[]     = "UNO-R4-SETUP";
const char AP_PASSWORD[] = "configureme";
const int  AP_CHANNEL    = 1;

// EEPROM layout
struct WifiCredentials {
  uint8_t magic;      // marker to know if credentials exist
  char ssid[32];
  char password[64];
};

const uint8_t WIFI_MAGIC = 0x42;
const int     EEPROM_ADDR = 0;

WifiCredentials gCreds;
WiFiServer      configServer(80);

// Provisioning forward declarations
bool   loadCredentials(WifiCredentials &creds);
void   saveCredentials(const WifiCredentials &creds);
void   clearCredentials();          // optional factory reset hook
bool   connectWithStoredCredentials(uint32_t timeoutMs);
void   runProvisioningPortal();
void   handleConfigClient(WiFiClient &client);
String getFormField(const String &body, const String &name);
String urlDecode(const String &src);

// ---------- EEPROM helpers ----------
bool loadCredentials(WifiCredentials &creds) {
  EEPROM.get(EEPROM_ADDR, creds);
  if (creds.magic != WIFI_MAGIC) return false;
  if (creds.ssid[0] == '\0')     return false;
  return true;
}

void saveCredentials(const WifiCredentials &creds) {
  EEPROM.put(EEPROM_ADDR, creds);
  // On UNO R4 WiFi, EEPROM writes are committed immediately.
  // No EEPROM.commit() available or needed.
}

void clearCredentials() {
  WifiCredentials empty;
  memset(&empty, 0, sizeof(empty));
  EEPROM.put(EEPROM_ADDR, empty);
  // No EEPROM.commit() on UNO R4 WiFi.
}


// ---------- Wi-Fi connect using stored creds ----------
bool connectWithStoredCredentials(uint32_t timeoutMs) {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("ERROR: WiFi module not found.");
    return false;
  }

  Serial.print("Connecting to ");
  Serial.print(gCreds.ssid);
  Serial.println(" ...");

  unsigned long start = millis();
  int status = WL_IDLE_STATUS;

  while ((millis() - start) < timeoutMs) {
    status = WiFi.begin(gCreds.ssid, gCreds.password);
    if (status == WL_CONNECTED) {
      Serial.println("Connected to Wi-Fi!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      return true;
    }
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nWi-Fi connect timed out.");
  return false;
}

// ---------- Config AP + HTTP portal ----------
void runProvisioningPortal() {
  WiFi.end();  // ensure client mode is off

  Serial.println("Starting Wi-Fi Config AP...");
  int status = WiFi.beginAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
  if (status != WL_AP_LISTENING && status != WL_AP_CONNECTED) {
    Serial.println("ERROR: Failed to start AP.");
    while (true) { delay(1000); }
  }

  IPAddress apIP = WiFi.localIP();
  Serial.print("Config AP SSID: "); Serial.println(AP_SSID);
  Serial.print("Password: ");       Serial.println(AP_PASSWORD);
  Serial.print("Open: http://");    Serial.println(apIP);

  configServer.begin();

  while (true) {
    WiFiClient client = configServer.available();
    if (client) {
      handleConfigClient(client);
      client.stop();
    }
    delay(10);
  }
}

void handleConfigClient(WiFiClient &client) {
  String requestLine = "";
  String headers     = "";
  String body        = "";
  bool   isPost      = false;
  bool   inHeaders   = true;

  unsigned long start = millis();
  while (client.connected() && (millis() - start) < 5000) {
    if (!client.available()) {
      delay(1);
      continue;
    }
    String line = client.readStringUntil('\n');
    if (inHeaders) {
      if (requestLine.length() == 0) {
        requestLine = line;
        if (requestLine.startsWith("POST")) isPost = true;
      } else if (line == "\r") {
        // headers finished
        inHeaders = false;
        if (!isPost) break;
      } else {
        headers += line;
      }
    } else {
      body += line;
    }
  }
  body.trim();

  Serial.println("=== HTTP Request ===");
  Serial.println(requestLine);

  // POST /save → store credentials
  if (requestLine.startsWith("POST /save")) {
    String ssidField = urlDecode(getFormField(body, "ssid"));
    String passField = urlDecode(getFormField(body, "password"));
    ssidField.trim();
    passField.trim();

    Serial.print("Received SSID: ");
    Serial.println(ssidField);
    Serial.print("Password length: ");
    Serial.println(passField.length());

    WifiCredentials newCreds;
    memset(&newCreds, 0, sizeof(newCreds));
    newCreds.magic = WIFI_MAGIC;
    ssidField.substring(0, sizeof(newCreds.ssid) - 1).toCharArray(newCreds.ssid, sizeof(newCreds.ssid));
    passField.substring(0, sizeof(newCreds.password) - 1).toCharArray(newCreds.password, sizeof(newCreds.password));

    saveCredentials(newCreds);
    gCreds = newCreds;

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.println(F("<!DOCTYPE html><html><head><meta charset='utf-8'><title>Wi-Fi Saved</title></head><body>"));
    client.println(F("<h2>Wi-Fi settings saved ✅</h2>"));
    client.print(F("<p>SSID: "));
    client.print(newCreds.ssid);
    client.println(F("</p>"));
    client.println(F("<p>Now reset or power-cycle the board.<br>On next boot it will connect to this network.</p>"));
    client.println(F("</body></html>"));
    Serial.println("Credentials saved to EEPROM. Please reset the board.");
    return;
  }

  // Default: serve configuration form
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.println(F("<!DOCTYPE html><html><head>"
                   "<meta charset='utf-8'>"
                   "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                   "<title>UNO R4 WiFi Setup</title>"
                   "</head><body>"));
  client.println(F("<h2>UNO R4 WiFi Provisioning</h2>"
                   "<p>Enter the Wi-Fi network this device should use.</p>"
                   "<form method='POST' action='/save'>"
                   "SSID:<br><input type='text' name='ssid' required><br><br>"
                   "Password:<br><input type='password' name='password'><br><br>"
                   "<button type='submit'>Save</button>"
                   "</form>"
                   "<p style='font-size:0.9em;color:#666;'>"
                   "Credentials are stored in on-board flash (EEPROM emulation). "
                   "To wipe them later, implement a factory reset calling clearCredentials()."
                   "</p>"
                   "</body></html>"));
}

// Parse form field from x-www-form-urlencoded body
String getFormField(const String &body, const String &name) {
  String key = name + "=";
  int start = body.indexOf(key);
  if (start < 0) return "";
  start += key.length();
  int end = body.indexOf('&', start);
  if (end < 0) end = body.length();
  return body.substring(start, end);
}

// URL decode: + and %xx
String urlDecode(const String &src) {
  String out;
  out.reserve(src.length());
  for (size_t i = 0; i < src.length(); i++) {
    char c = src[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < src.length()) {
      char h1 = src[i + 1];
      char h2 = src[i + 2];
      int hi = (h1 >= 'A') ? ((h1 & ~0x20) - 'A' + 10) : (h1 - '0');
      int lo = (h2 >= 'A') ? ((h2 & ~0x20) - 'A' + 10) : (h2 - '0');
      char decoded = (char)((hi << 4) | (lo & 0x0F));
      out += decoded;
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

// =====================================================================
//                        7. SETUP & LOOP
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
  bool haveCreds = loadCredentials(gCreds);
  if (!haveCreds) {
    Serial.println("No stored Wi-Fi credentials. Entering config portal...");
    lcd.clear();
    lcd.print("AP: UNO-R4-SETUP");
    lcd.setCursor(0, 1);
    lcd.print("Config via WiFi");
    runProvisioningPortal();  // blocks; returns only after /save
    // After saving, we ask the user to reset:
    while (true) { delay(1000); }
  }

  if (!connectWithStoredCredentials(20000)) {
    Serial.println("Failed to connect, starting config portal...");
    lcd.clear();
    lcd.print("WiFi failed");
    lcd.setCursor(0, 1);
    lcd.print("Open AP to fix");
    runProvisioningPortal();
    while (true) { delay(1000); }
  }

  lcd.clear();
  lcd.print("WiFi Connected!");
  Serial.println("WiFi Connected!");

  // --- MQTT setup ---
  mqttClient.setId(mqttClientId);
  connectToMqtt();

  lcd.clear();
  lcd.print("System Ready");
  lcd.setCursor(0, 1);
  lcd.print("Normal Mode");
}

void loop() {
  // Keep Wi-Fi & MQTT alive
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped, reconnecting...");
    connectWithStoredCredentials(20000);
  }

  if (!mqttClient.connected()) {
    connectToMqtt();
  }
  mqttClient.poll();

  // Sensor logic every 10 seconds
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

      // Publish telemetry JSON
      publishSensorData(temperature, humidity, alertStatus);
    }
  }

  // Blink red LED if alert; green steady if normal
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

// =====================================================================
//                        8. MQTT HELPERS
// =====================================================================

void connectToMqtt() {
  Serial.print("Connecting to MQTT broker ");
  Serial.print(mqttBroker);
  Serial.print(":");
  Serial.println(mqttPort);

  while (!mqttClient.connect(mqttBroker, mqttPort)) {
    Serial.print("MQTT connect failed, rc = ");
    Serial.println(mqttClient.connectError());
    delay(2000);
  }

  Serial.println("MQTT connected.");
}

void publishSensorData(float temperature, float humidity, const String &status) {
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
  Serial.print(mqttTopic);
  Serial.print(" : ");
  Serial.println(payload);

  mqttClient.beginMessage(mqttTopic);
  mqttClient.print(payload);
  mqttClient.endMessage();
}
