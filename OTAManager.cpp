#include "OTAManager.h"
#include <ArduinoOTA.h>

void setupOTA() {
  // הגדרת שם זיהוי בולט שיופיע ברשימת הפורטים בארדואינו
  ArduinoOTA.setHostname("Pool-ESP32-Controller");

  // סיסמת הגנה אופציונלית לצריבה אלחוטית (מומלץ לבטל הערה אם נדרש אבטחה)
  // ArduinoOTA.setPassword("admin123");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("\n[OTA] Update started: " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update finished successfully. Rebooting...");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

//  ArduinoOTA.setPassword("admin123");
  
  ArduinoOTA.begin();
  Serial.println("[OTA] Service initialized and listening.");
}

void handleOTA() {
  ArduinoOTA.handle();
}