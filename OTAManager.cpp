#include "OTAManager.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_ota_ops.h>
#include "NanoFlasher.h"

// דגל גלובלי — true כשעדכון OTA מתבצע. loop() משתמש בו כדי לדלג על משימות כבדות.
bool otaInProgress = false;

// Hard-reset יסודי של ה-ESP32 באמצעות Deep-Sleep + Wake Timer.
// משמש את כפתור "אתחל ESP32" בפורטל, ו-MQTT pool/control/esp_reset/set.
void performHardReset() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  delay(200);

  esp_sleep_enable_timer_wakeup(100000ULL); // 100ms
  esp_deep_sleep_start();
  while (true) {}
}

void setupOTA() {
  ArduinoOTA.setHostname("Pool-ESP32-Controller");

  // מאפשר ל-ArduinoOTA לסיים את כל הטרנזקציה ולאתחל עצמאית בצורה מסודרת
  ArduinoOTA.setRebootOnSuccess(true);

  // הגדלת timeout של OTA ל-2 שניות
  ArduinoOTA.setTimeout(2000);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    WebSerial.println("\n[OTA] Update started: " + type);
    WebSerial.printf("[OTA] Diag: RSSI=%ld dBm, FreeHeap=%u bytes, MinFreeHeap=%u bytes\n",
                     WiFi.RSSI(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
    otaInProgress = true;
  });

  ArduinoOTA.onEnd([]() {
    WebSerial.println("\n[OTA] Update finished successfully.");

    // הבטחת סימון המחיצה החדשה (החלפת ה-Flag) ברמת ה-Bootloader
    const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(NULL);
    if (nextPartition != NULL) {
      esp_err_t err = esp_ota_set_boot_partition(nextPartition);
      if (err == ESP_OK) {
        WebSerial.printf("[OTA] Boot partition successfully set to: %s\n", nextPartition->label);
      } else {
        WebSerial.printf("[OTA] Warning: esp_ota_set_boot_partition error: 0x%X\n", err);
      }
    }

    WebSerial.println("[OTA] Rebooting into new firmware...");
    otaInProgress = false;
    // לא קוראים ל-performHardReset() כאן — נותנים למערכת לאתחל כחוק
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    unsigned int percent = progress / (total / 100);
    // הדפסה בדילוגים של 10% כדי לא להעמיס על החיץ בזמן כתיבה לפלאש
    if (percent % 10 == 0) {
      WebSerial.printf("[OTA] Progress: %u%%\r", percent);
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    WebSerial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) WebSerial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) WebSerial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) WebSerial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) WebSerial.println("Receive Failed");
    else if (error == OTA_END_ERROR) WebSerial.println("End Failed");
    otaInProgress = false;
  });

  ArduinoOTA.begin();
  WebSerial.println("[OTA] Service initialized and listening.");
}

void handleOTA() {
  ArduinoOTA.handle();
}