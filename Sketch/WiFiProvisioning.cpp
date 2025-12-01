#include "WiFiProvisioning.h"

#include <WiFiS3.h>
#include <EEPROM.h>

/* ===================== CONFIG =====================
===     Portal Address: http://192.168.4.1        === 
*/


// AP used during provisioning
static const char AP_SSID[]     = "UNO-R4-SETUP";
static const char AP_PASSWORD[] = "configureme";
static const int  AP_CHANNEL    = 1;

// EEPROM layout
static const uint8_t WIFI_MAGIC  = 0x42;
static const int     EEPROM_ADDR = 0;

// HTTP server for config
static WiFiServer configServer(80);

// Forward-declare internal helper functions
static String getFormField(const String &body, const String &name);
static String urlDecode(const String &src);
static void   handleConfigClient(WiFiClient &client, WifiCredentials &creds);

// ===================== PUBLIC API =====================

bool loadWifiCredentials(WifiCredentials &creds) {
  EEPROM.get(EEPROM_ADDR, creds);
  if (creds.magic != WIFI_MAGIC) return false;
  if (creds.ssid[0] == '\0')     return false;
  return true;
}

void saveWifiCredentials(const WifiCredentials &creds) {
  EEPROM.put(EEPROM_ADDR, creds);
  // On UNO R4 WiFi, EEPROM writes are committed immediately.
}

void clearWifiCredentials() {
  WifiCredentials empty;
  memset(&empty, 0, sizeof(empty));
  EEPROM.put(EEPROM_ADDR, empty);
}

// Try to connect to Wi-Fi using stored credentials
bool connectWithStoredCredentials(WifiCredentials &creds, uint32_t timeoutMs) {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("ERROR: WiFi module not found.");
    return false;
  }

  Serial.print("Connecting to ");
  Serial.print(creds.ssid);
  Serial.println(" ...");

  unsigned long start = millis();
  int status = WL_IDLE_STATUS;

  while ((millis() - start) < timeoutMs) {
    status = WiFi.begin(creds.ssid, creds.password);
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

// Start AP + HTTP server and handle config requests
void runProvisioningPortal(WifiCredentials &creds) {
  WiFi.end();  // ensure client mode is off

  Serial.println("Starting Wi-Fi Config AP...");
  int status = WiFi.beginAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
  if (status != WL_AP_LISTENING && status != WL_AP_CONNECTED) {
    Serial.println("ERROR: Failed to start AP.");
    while (true) { delay(1000); }  // fatal
  }

  IPAddress apIP = WiFi.localIP();
  Serial.print("Config AP SSID: "); Serial.println(AP_SSID);
  Serial.print("Password: ");       Serial.println(AP_PASSWORD);
  Serial.print("Open: http://");    Serial.println(apIP);

  configServer.begin();

  while (true) {
    WiFiClient client = configServer.available();
    if (client) {
      handleConfigClient(client, creds);
      client.stop();
    }
    delay(10);
  }
}

// ===================== INTERNAL HELPERS =====================

static void handleConfigClient(WiFiClient &client, WifiCredentials &creds) {
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

    saveWifiCredentials(newCreds);
    creds = newCreds;  // update caller's copy

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
    client.println(F("<p>COMMENT OUT clearWifiCredentials() in Sketch.ino after saving.</p>"));
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
                   "To wipe them later, implement a factory reset calling clearWifiCredentials()."
                   "</p>"
                   "</body></html>"));
}

// Parse form field from x-www-form-urlencoded body
static String getFormField(const String &body, const String &name) {
  String key = name + "=";
  int start = body.indexOf(key);
  if (start < 0) return "";
  start += key.length();
  int end = body.indexOf('&', start);
  if (end < 0) end = body.length();
  return body.substring(start, end);
}

// URL decode: + and %xx
static String urlDecode(const String &src) {
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
