#include "MQTTManager.h"
#include "Config.h"
#include "ModbusManager.h"
#include "ArduinoCommManager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "NanoFlasher.h"

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

void publishPumpTelemetry(bool isRunning, uint16_t powerW, uint16_t flowM3H, float energyKWh) {
  // חסימת תקיעת TCP: אם הרשת למטה, אל תנסה לשלוח כלום
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) return;

  char strBuffer[16];
  mqttClient.publish("pool/pump/state", isRunning ? "ON" : "OFF");

  snprintf(strBuffer, sizeof(strBuffer), "%u", powerW);
  mqttClient.publish("pool/pump/power_watts", strBuffer);

  snprintf(strBuffer, sizeof(strBuffer), "%u", flowM3H);
  mqttClient.publish("pool/pump/flow_m3h", strBuffer);

  snprintf(strBuffer, sizeof(strBuffer), "%.2f", energyKWh);
  mqttClient.publish("pool/pump/energy_kwh", strBuffer);
}

void publishNanoTelemetry(float tCol, float tSt, float tFlw, bool pumpOn, int state) {
  // חסימת תקיעת TCP
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) return;

  char strBuffer[16];

  snprintf(strBuffer, sizeof(strBuffer), "%.1f", tCol);
  mqttClient.publish("pool/heating/tCOL", strBuffer);

  snprintf(strBuffer, sizeof(strBuffer), "%.1f", tSt);
  mqttClient.publish("pool/heating/tST", strBuffer);

  snprintf(strBuffer, sizeof(strBuffer), "%.1f", tFlw);
  mqttClient.publish("pool/heating/tFLW", strBuffer);

  mqttClient.publish("pool/heating/pump_state", pumpOn ? "ON" : "OFF");

  snprintf(strBuffer, sizeof(strBuffer), "%d", state);
  mqttClient.publish("pool/heating/state", strBuffer);

  const char* mode = "AUTO";
  if (state == 4) mode = "MAN_ON";
  else if (state == 5) mode = "MAN_OFF";
  mqttClient.publish("pool/heating/mode", mode, true);
}

void publishNanoConfig(float sMax, float tDel, float tOn, float tOff, float dtO, float dtF) {
  // ההדפסה הזו נשארת כאן כדי שתמיד תראה את הפולינג, גם באופליין
  Serial.printf("[NANO -> ESP32] Syncing Config: sMAX=%.1f, tDEL=%.1f, tON=%.1f, tOFF=%.1f, DT_O=%.1f, DT_F=%.1f\n", 
                sMax, tDel, tOn, tOff, dtO, dtF);

  // חסימת תקיעת TCP (מונע המתנה לאישור מהראוטר הכבוי)
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) return;

  mqttClient.publish("pool/heating/sMAX", String(sMax, 1).c_str(), true);
  mqttClient.publish("pool/heating/tDEL", String(tDel, 1).c_str(), true);
  mqttClient.publish("pool/heating/tON",  String(tOn, 1).c_str(), true);
  mqttClient.publish("pool/heating/tOFF", String(tOff, 1).c_str(), true);
  mqttClient.publish("pool/heating/DT_O", String(dtO, 1).c_str(), true);
  mqttClient.publish("pool/heating/DT_F", String(dtF, 1).c_str(), true);
}

void publishSystemMode(bool isOn) {
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) return;
  mqttClient.publish("pool/heating/system_mode", isOn ? "ON" : "OFF", true);
}

void setSystemPower(bool isOn) {
  digitalWrite(SYSTEM_POWER_RELAY_PIN, isOn ? HIGH : LOW);
  Serial.printf("[SYSTEM] Arduino power %s\n", isOn ? "ON" : "OFF");
  publishSystemMode(isOn);
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char message[32];
  if (length >= sizeof(message)) length = sizeof(message) - 1;
  memcpy(message, payload, length);
  message[length] = '\0';

  Serial.printf("[MQTT] Message arrived [%s]: %s\n", topic, message);

  float val = atof(message);

  if (strcmp(topic, "pool/pump/set_state") == 0) {
    setPumpPowerState(strcmp(message, "ON") == 0);
  } 
  else if (strcmp(topic, "pool/pump/set_flow") == 0) {
    uint16_t targetFlow = atoi(message);
    setPumpFlowRate(targetFlow);
  }
  else if (strcmp(topic, "pool/heating/set_mode") == 0) {
    sendNanoMode(message);
  }
  else if (strcmp(topic, "pool/heating/set_smax") == 0) {
    sendNanoSetSMax(val);
  }
  else if (strcmp(topic, "pool/heating/set_tdel") == 0) {
    sendNanoSetTDEL(val);
  }
  else if (strcmp(topic, "pool/heating/set_ton") == 0) {
    sendNanoSetTON(val);
  }
  else if (strcmp(topic, "pool/heating/set_toff") == 0) {
    sendNanoSetTOFF(val);
  }
  else if (strcmp(topic, "pool/heating/set_dto") == 0) {
    sendNanoSetDTO(val);
  }
  else if (strcmp(topic, "pool/heating/set_dtf") == 0) {
    sendNanoSetDTF(val);
  }
  else if (strcmp(topic, "pool/heating/save") == 0) {
    sendNanoSave();
  }
  else if (strcmp(topic, "pool/heating/reset_default") == 0) {
    sendNanoResetDefault();
  }
  else if (strcmp(topic, "pool/heating/set_system_mode") == 0) {
    setSystemPower(strcmp(message, "ON") == 0);
  }
  else if (strcmp(topic, "pool/control/nano_reset/set") == 0) {
    triggerNanoReset();
  } 
  else if (strcmp(topic, "pool/control/esp_reset/set") == 0) {
    delay(100);
    ESP.restart();
  }
}

static void reconnectMQTT() {
  static unsigned long lastReconnectAttempt = 0;
  unsigned long now = millis();

  if (now - lastReconnectAttempt > 5000) {
    lastReconnectAttempt = now;

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[MQTT] Attempting connection...");
      if (mqttClient.connect("Pool_ESP32_Master", "pool/status", 1, true, "offline")) {
        Serial.println(" Connected!");
        mqttClient.publish("pool/status", "online", true);

        mqttClient.subscribe("pool/pump/set_state");
        mqttClient.subscribe("pool/pump/set_flow");
        mqttClient.subscribe("pool/heating/set_mode");
        mqttClient.subscribe("pool/heating/set_smax");
        mqttClient.subscribe("pool/heating/set_tdel");
        mqttClient.subscribe("pool/heating/set_ton");
        mqttClient.subscribe("pool/heating/set_toff");
        mqttClient.subscribe("pool/heating/set_dto");
        mqttClient.subscribe("pool/heating/set_dtf");
        mqttClient.subscribe("pool/heating/save");
        mqttClient.subscribe("pool/heating/reset_default");
        mqttClient.subscribe("pool/heating/set_system_mode");
        mqttClient.subscribe("pool/control/nano_reset/set");
        mqttClient.subscribe("pool/control/esp_reset/set");

        publishSystemMode(digitalRead(SYSTEM_POWER_RELAY_PIN) == HIGH);
      } else {
        Serial.printf(" Failed, rc=%d\n", mqttClient.state());
      }
    }
  }
}

void setupMQTT() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void handleMQTT() {
  // הדילוג החשוב ביותר: אם הראוטר כבוי, אל תיכנס בכלל לפונקציות של הספרייה!
  if (WiFi.status() != WL_CONNECTED) {
    return; 
  }

  if (!mqttClient.connected()) {
    reconnectMQTT();
  } else {
    mqttClient.loop();
  }
}