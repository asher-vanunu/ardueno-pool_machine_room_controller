#include "NetworkManager.h"
#include "Config.h"
#include "NanoFlasher.h"

WiFiManager wm;

static unsigned long buttonPressStart = 0;
static bool buttonStatePrevious = HIGH;
static bool wasConnected = false; 
static unsigned long lastReconnectAttempt = 0;

static String savedNetworkInfo = "Not connected to WiFi yet.";

static void configureWiFiCountry() {
  // הסרנו מכאן את הקיבוע ל-WIFI_STA כדי שהפורטל יוכל לעבוד
  wifi_country_t country = {
    .cc = "IL",
    .schan = 1,
    .nchan = 13,
    .max_tx_power = 20,
    .policy = WIFI_COUNTRY_POLICY_AUTO
  };
  esp_wifi_set_country(&country);
}

void setupNetwork() {
  WiFi.persistent(true); 
  
  // השורה WiFi.disconnect(true, true) נמחקה מכאן כדי לא לדרוס את הזיכרון!
  delay(200);

  WiFi.mode(WIFI_STA); 
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  
  configureWiFiCountry();
  
  wm.setConfigPortalBlocking(false); 
  wm.setConfigPortalTimeout(180);     
  wm.setConnectTimeout(20); 

  WebSerial.println("[Network] Starting network connection process...");

  bool res = wm.autoConnect("Pool-Controller-AP");

  if (!res) {
    WebSerial.println("[Network] Could not connect immediately. Config Portal 'Pool-Controller-AP' started in background.");
    wasConnected = false;
  } else {
    savedNetworkInfo = "SSID: " + WiFi.SSID() + " | IP: " + WiFi.localIP().toString() + " | RSSI: " + String(WiFi.RSSI()) + " dBm";
    WebSerial.println("\n[Network] WiFi Connected successfully on boot!");
    WebSerial.println("[Network] " + savedNetworkInfo);
    wasConnected = true;
  }
}

void handleNetwork() {
  wm.process();

  bool isConnected = (WiFi.status() == WL_CONNECTED);
  unsigned long currentMillis = millis();

  if (isConnected && !wasConnected) {
    savedNetworkInfo = "SSID: " + WiFi.SSID() + " | IP: " + WiFi.localIP().toString() + " | RSSI: " + String(WiFi.RSSI()) + " dBm";
    WebSerial.println("\n---------------------------------------------");
    WebSerial.println("[Network Status] WiFi Reconnected Successfully!");
    WebSerial.println("[Network Status] " + savedNetworkInfo);
    WebSerial.println("---------------------------------------------\n");
    wasConnected = true;
  } 
  else if (!isConnected && wasConnected) {
    WebSerial.println("\n---------------------------------------------");
    WebSerial.println("[Network Status] WiFi Disconnected! Polling Arduino continues in offline mode...");
    WebSerial.println("---------------------------------------------\n");
    wasConnected = false;
    lastReconnectAttempt = currentMillis; 
  }

  if (!isConnected && (currentMillis - lastReconnectAttempt >= 10000)) {
    lastReconnectAttempt = currentMillis;
    if (!wm.getConfigPortalActive()) {
      WebSerial.println("[Network] Attempting active background reconnect to WiFi...");
      WiFi.reconnect(); 
    }
  }

  static bool lastWebOutputState = false;
  
  if (WebSerial.webOutputEnabled && !lastWebOutputState) {
    WebSerial.println("\n=== Saved Network Status ===");
    WebSerial.println(savedNetworkInfo);
    WebSerial.println("============================\n");
  }
  
  lastWebOutputState = WebSerial.webOutputEnabled;
}

void checkResetButton() {
  bool buttonStateCurrent = digitalRead(BOOT_BUTTON_PIN);

  if (buttonStateCurrent == LOW && buttonStatePrevious == HIGH) {
    buttonPressStart = millis();
  }

  if (buttonStateCurrent == LOW && (millis() - buttonPressStart > 3000)) {
    WebSerial.println("\n[RESET] BOOT Button held for 3s! Clearing Wi-Fi credentials...");
    wm.resetSettings();
    WiFi.disconnect(true, true);
    delay(1000);
    ESP.restart();
  }

  buttonStatePrevious = buttonStateCurrent;
}

void resetWiFiSettings() {
  wm.resetSettings();
  WiFi.disconnect(true, true);
  WebSerial.println("Wi-Fi settings reset manually via code.");
}