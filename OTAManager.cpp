#include "OTAManager.h"
#include <ArduinoOTA.h>
#include "NanoFlasher.h"

void setupOTA() {
  ArduinoOTA.setHostname("Pool-ESP32-Controller");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    WebSerial.println("\n[OTA] Update started: " + type);
  });

  ArduinoOTA.onEnd([]() {
    WebSerial.println("\n[OTA] Update finished successfully.");
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
  });

  ArduinoOTA.begin();
  WebSerial.println("[OTA] Service initialized and listening.");
}

void handleOTA() {
  ArduinoOTA.handle();
}