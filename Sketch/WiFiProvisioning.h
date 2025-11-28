#pragma once

#include <Arduino.h>

// Simple struct for storing Wi-Fi credentials in EEPROM
struct WifiCredentials {
  uint8_t magic;        // marker to know if credentials are valid
  char ssid[32];        // 31 chars + null
  char password[64];    // 63 chars + null
};

// Load credentials from EEPROM. 
// Returns true if valid credentials exist.
bool loadWifiCredentials(WifiCredentials &creds);

// Save credentials to EEPROM.
void saveWifiCredentials(const WifiCredentials &creds);

// Clear credentials from EEPROM (factory reset helper).
void clearWifiCredentials();

// Try to connect to Wi-Fi using stored credentials.
// Returns true on success, false on timeout/failure.
bool connectWithStoredCredentials(WifiCredentials &creds, uint32_t timeoutMs);

// Run the provisioning portal: start AP, HTTP server, form handler.
// Blocks forever until you reset the board.
void runProvisioningPortal(WifiCredentials &creds);
