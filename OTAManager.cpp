#include "OTAManager.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include "NanoFlasher.h"

// דגל גלובלי — true כשעדכון OTA מתבצע. loop() משתמש בו כדי לדלג על משימות כבדות.
bool otaInProgress = false;

// Hard-reset יסודי של ה-ESP32 באמצעות Deep-Sleep + Wake Timer.
// יותר יסודי מ-esp_restart()/ESP.restart() (soft reset) — מכבה ומדליק מחדש
// את ה-CPU, רדיו WiFi/BLE, וכל ההיקפים הפנימיים. ההתעוררות מתבצעת כבוט רגיל דרך setup().
// משמש את OTA onEnd, כפתור "אתחל ESP32" בפורטל, ו-MQTT pool/control/esp_reset/set.
void performHardReset() {
  // ניתוק WiFi מבלי למחוק את הגדרות הרשת (הפרמטר השני false שומר אותן)
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  delay(200);

  // Deep-Sleep + Wake Timer = hard reset אמיתי: מכבה ומדליק מחדש את ה-CPU,
  // רדיו WiFi/BLE, וכל ההיקפים הפנימיים. פותר את הבעיה של "צריך ללחוץ RESET פיזית".
  // ההתעוררות מתבצעת כבוט רגיל דרך setup(), ו-setupNetwork() מחזיר את ה-WiFi ל-WIFI_STA.
  esp_sleep_enable_timer_wakeup(100000ULL); // 100ms
  esp_deep_sleep_start();
  while (true) {}
}

void setupOTA() {
  ArduinoOTA.setHostname("Pool-ESP32-Controller");
  ArduinoOTA.setRebootOnSuccess(false);
  // הגדלת timeout של OTA ל-2 שניות (ברירת מחדל קצרה מדי ל-WiFi חלש/לא יציב).
  // מונע ניתוק מוקדם של ה-PC כשחבילה לא מגיעה בזמן (WinError 10053).
  ArduinoOTA.setTimeout(2000);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    WebSerial.println("\n[OTA] Update started: " + type);
    // אבחון: הדפסת RSSI ו-heap פנוי כדי לזהות בעיות WiFi/זיכרון שגורמות ל-timeout
    WebSerial.printf("[OTA] Diag: RSSI=%ld dBm, FreeHeap=%u bytes, MinFreeHeap=%u bytes\n",
                     WiFi.RSSI(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
    // הפעלת דגל — loop() ידלג על משימות כבדות כדי לתת ל-OTA נתיב CPU נקי
    otaInProgress = true;
  });

  ArduinoOTA.onEnd([]() {
    WebSerial.println("\n[OTA] Update finished successfully. Performing deep-sleep hard reset...");
    delay(200);
    WebSerial.buffer = "";
    // כיבוי הדגל (ליתר ביטחון — בכל מקרה עושים hard reset מיד)
    otaInProgress = false;
    performHardReset();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    WebSerial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    WebSerial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) WebSerial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) WebSerial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) WebSerial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) WebSerial.println("Receive Failed");
    else if (error == OTA_END_ERROR) WebSerial.println("End Failed");
    // כיבוי הדגל גם בשגיאה — מחזיר את המערכת לפעילות רגילה
    otaInProgress = false;
  });

  ArduinoOTA.begin();
  WebSerial.println("[OTA] Service initialized and listening.");
}

void handleOTA() {
  ArduinoOTA.handle();
}