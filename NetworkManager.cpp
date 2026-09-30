#include "NetworkManager.h"
#include "Config.h"
#include "NanoFlasher.h"

WiFiManager wm;

static unsigned long buttonPressStart = 0;
static bool buttonStatePrevious = HIGH;
static bool wasConnected = false; 
static unsigned long lastReconnectAttempt = 0;

static void configureWiFiCountry() {
  WiFi.mode(WIFI_STA);
  WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);

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
  WiFi.persistent(false); 
  WiFi.disconnect(true, true); 
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  
  WiFi.setAutoReconnect(true);
  
  configureWiFiCountry();
  
  wm.setConfigPortalBlocking(false); 
  wm.setConfigPortalTimeout(60);     
  wm.setConnectTimeout(3); 

  WebSerial.println("[Network] Starting network connection process...");

  bool res = wm.autoConnect("Pool-Controller-AP");

  if (!res) {
    WebSerial.println("[Network] Could not connect immediately. Config Portal 'Pool-Controller-AP' started in background.");
    wasConnected = false;
  } else {
    WebSerial.println("\n[Network] WiFi Connected successfully on boot!");
    WebSerial.print("[Network] IP Address: ");
    WebSerial.println(WiFi.localIP());
    wasConnected = true;
  }
}

void handleNetwork() {
  wm.process();

  bool isConnected = (WiFi.status() == WL_CONNECTED);
  unsigned long currentMillis = millis();

  if (isConnected && !wasConnected) {
    WebSerial.println("\n---------------------------------------------");
    WebSerial.println("[Network Status] WiFi Reconnected Successfully!");
    WebSerial.print("[Network Status] IP Address: ");
    WebSerial.println(WiFi.localIP());
    WebSerial.println("---------------------------------------------\n");
    wasConnected = true;
  } 
  else if (!isConnected && wasConnected) {
    WebSerial.println("\n---------------------------------------------");
    WebSerial.println("[Network Status] WiFi Disconnected! Polling Arduino continues in offline mode...");
    WebSerial.println("---------------------------------------------\n");
    wasConnected = false;
    lastReconnectAttempt = currentMillis; // התחלת ספירה לאחור לניסיון חיבור מחדש
  }

  // הפעלת פקודת התחברות יזומה ואגרסיבית כל 10 שניות כדי להתגבר על חוסר התגובה של הראוטר
  if (!isConnected && (currentMillis - lastReconnectAttempt >= 10000)) {
    lastReconnectAttempt = currentMillis;
    if (!wm.getConfigPortalActive()) {
      WebSerial.println("[Network] Attempting active background reconnect to WiFi...");
      WiFi.reconnect(); // ניסיון התחברות שלא תוקע את הלולאה
    }
  }
}

void checkResetButton() {
  bool buttonStateCurrent = digitalRead(BOOT_BUTTON_PIN);

  if (buttonStateCurrent == LOW && buttonStatePrevious == HIGH) {
    buttonPressStart = millis();
  }

  if (buttonStateCurrent == LOW && (millis() - buttonPressStart > 3000)) {
    WebSerial.println("\n[RESET] BOOT Button held for 3s! Clearing Wi-Fi credentials...");
    wm.resetSettings();
    delay(1000);
    ESP.restart();
  }

  buttonStatePrevious = buttonStateCurrent;
}

void resetWiFiSettings() {
  wm.resetSettings();
  WebSerial.println("Wi-Fi settings reset manually via code.");
}