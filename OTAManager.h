#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>

void setupOTA();
void handleOTA();

// Hard-reset יסודי של ה-ESP32 באמצעות Deep-Sleep + Wake Timer.
// יותר יסודי מ-esp_restart()/ESP.restart() (soft reset) — מכבה ומדליק מחדש
// את ה-CPU, רדיו WiFi/BLE, וכל ההיקפים הפנימיים. ההתעוררות מתבצעת כבוט רגיל דרך setup().
// משמש את OTA onEnd, כפתור "אתחל ESP32" בפורטל, ו-MQTT pool/control/esp_reset/set.
void performHardReset();

// דגל גלובלי שמציין שעדכון OTA מתבצע כעת. כשהוא true, loop() מדלג על משימות כבדות
// (MQTT, Modbus, ArduinoComm, NanoFlasher, Time) כדי לתת ל-OTA נתיב CPU נקי
// ולמנוע timeout של חבילות OTA (WinError 10053).
extern bool otaInProgress;

#endif