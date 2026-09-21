#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiManager.h>

// Initialize network interfaces and WiFiManager
void setupNetwork();

// Check if the BOOT button is held for resetting Wi-Fi settings
void checkResetButton();
void resetWiFiSettings();
#endif