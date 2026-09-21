#include "Config.h"
#include "NetworkManager.h"
#include "MQTTManager.h"
#include "ModbusManager.h"
#include "ArduinoCommManager.h"
#include <Arduino.h>

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PUMP_PIN, OUTPUT);
  digitalWrite(RELAY_PUMP_PIN, LOW);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

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