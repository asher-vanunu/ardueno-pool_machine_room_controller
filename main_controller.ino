#include "Config.h"
#include "NetworkManager.h"
#include "MQTTManager.h"
#include "ModbusManager.h"
#include "ArduinoCommManager.h"
#include <Arduino.h>

void print_board_info(void)
{
  Serial.println("\n--- ESP32 Hardware Info ---");

  // 1. גודל שבב ה-Flash הפיזי
  uint32_t flashSizeBytes = ESP.getFlashChipSize();
  float flashSizeMB = flashSizeBytes / (1024.0 * 1024.0);
  Serial.printf("Flash Chip Size: %u bytes (%.2f MB)\n", flashSizeBytes, flashSizeMB);

  // 2. בדיקת PSRAM
  if (psramFound()) {
    uint32_t psramSizeBytes = ESP.getPsramSize();
    uint32_t freePsramBytes = ESP.getFreePsram();
    Serial.printf("PSRAM Detected: %.2f MB (Total: %u bytes, Free: %u bytes)\n", 
                  psramSizeBytes / (1024.0 * 1024.0), psramSizeBytes, freePsramBytes);
  } else {
    Serial.println("PSRAM: Not detected or not enabled in IDE settings.");
  }

  // 3. זיכרון RAM פנימי רגיל (SRAM)
  Serial.printf("Internal Free Heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("PSRAM found: %s\n", psramFound() ? "YES" : "NO");
  Serial.println("---------------------------");
}

void setup() {
  Serial.begin(115200);

  print_board_info();

  pinMode(RELAY_PUMP_PIN, OUTPUT);
  digitalWrite(RELAY_PUMP_PIN, LOW);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // ממסר חשמל ה-Arduino - ברירת מחדל מערכת דלוקה (ON)
  pinMode(SYSTEM_POWER_RELAY_PIN, OUTPUT);
  digitalWrite(SYSTEM_POWER_RELAY_PIN, HIGH);

  setupNetwork();
  setupMQTT();
  setupModbus();
  setupArduinoComm();
}

void loop() {
  checkResetButton();
  handleMQTT();
  handleModbus();
  handleArduinoComm();
}