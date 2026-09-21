#include "NetworkManager.h"
#include "Config.h"

WiFiManager wm;

static unsigned long buttonPressStart = 0;
static bool buttonStatePrevious = HIGH;

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
  WiFi.persistent(false); // מונע שחיקה של זיכרון ה-Flash
  WiFi.disconnect(true, true); // ניתוק וניקוי הגדרות Wi-Fi מזיכרון ה-RAM
  delay(200);

  // 2. הגדרת מצב תחנה (Station) בלבד
  WiFi.mode(WIFI_STA);

  // 3. ביטול מנגנון חיסכון בחשמל (Sleep Mode) - קריטי לראוטרים ביתיים!
  WiFi.setSleep(false);
  
  configureWiFiCountry();
  wm.setConfigPortalTimeout(180);

  bool res = wm.autoConnect("Pool-Controller-AP");

  if (!res) {
    Serial.println("Failed to connect or hit timeout. Restarting...");
    ESP.restart();
  } else {
    Serial.println("\nWiFi Connected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
}

void checkResetButton() {
  bool buttonStateCurrent = digitalRead(BOOT_BUTTON_PIN);

  if (buttonStateCurrent == LOW && buttonStatePrevious == HIGH) {
    buttonPressStart = millis();
  }

  if (buttonStateCurrent == LOW && (millis() - buttonPressStart > 3000)) {
    Serial.println("\n[RESET] BOOT Button held for 3s! Clearing Wi-Fi credentials...");
    wm.resetSettings();
    delay(1000);
    ESP.restart();
  }

  buttonStatePrevious = buttonStateCurrent;
}

void resetWiFiSettings() {
  wm.resetSettings();
  Serial.println("Wi-Fi settings reset manually via code.");
}