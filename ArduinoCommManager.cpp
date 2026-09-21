#include "ArduinoCommManager.h"
#include "Config.h"
#include <HardwareSerial.h>
#include <ArduinoJson.h>

extern void publishNanoTelemetry(float tCol, float tSt, float tFlw, bool pumpOn, int state);
extern void publishNanoConfig(float sMax, float tDel, float tOn, float tOff, float dtO, float dtF);

static HardwareSerial SerialNano(NANO_UART_NUM);
static unsigned long lastPollTime = 0;

void setupArduinoComm() {
  SerialNano.begin(NANO_BAUDRATE, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
  Serial.println("[ArduinoComm] Serial channel to Arduino Nano initialized.");
}

void sendNanoMode(const char* mode) {
  if (strcmp(mode, "AUTO") == 0) {
    SerialNano.println("SET:MODE:AUTO");
  } else if (strcmp(mode, "MAN_ON") == 0) {
    SerialNano.println("SET:MODE:MAN_ON");
  } else if (strcmp(mode, "MAN_OFF") == 0) {
    SerialNano.println("SET:MODE:MAN_OFF");
  }
}

void sendNanoSetSMax(float val) { SerialNano.printf("SET:CFG:sMAX:%.1f\r\n", val); }
void sendNanoSetTDEL(float val) { SerialNano.printf("SET:CFG:tDEL:%.1f\r\n", val); }
void sendNanoSetTON(float val)  { SerialNano.printf("SET:CFG:tON:%.1f\r\n", val); }
void sendNanoSetTOFF(float val) { SerialNano.printf("SET:CFG:tOFF:%.1f\r\n", val); }
void sendNanoSetDTO(float val)  { SerialNano.printf("SET:CFG:DT_O:%.1f\r\n", val); }
void sendNanoSetDTF(float val)  { SerialNano.printf("SET:CFG:DT_F:%.1f\r\n", val); }

void sendNanoSave() {
  SerialNano.println("CMD:SAVE");
}

static void parseNanoJSON(const String& jsonStr) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) {
    return;
  }

  float tCol = doc["tCOL"] | 0.0f;
  float tSt  = doc["tST"]  | 0.0f;
  float tFlw = doc["tFLW"] | 0.0f;
  bool  pump = (doc["pump"] | 0) == 1;
  int  state = doc["state"] | 0;

  publishNanoTelemetry(tCol, tSt, tFlw, pump, state);

  if (doc.containsKey("sMAX")) {
    float sMax = doc["sMAX"] | 35.0f;
    float tDel = doc["tDEL"] | 1.0f;
    float tOn  = doc["tON"]  | 8.0f;
    float tOff = doc["tOFF"] | 3.0f;
    float dtO  = doc["DT_O"] | 8.0f;
    float dtF  = doc["DT_F"] | 2.0f;

    publishNanoConfig(sMax, tDel, tOn, tOff, dtO, dtF);
  }
}

void handleArduinoComm() {
  unsigned long now = millis();
  if (now - lastPollTime > 3000) {
    lastPollTime = now;
    SerialNano.println("GET:STATUS");
  }

  static String inputBuffer = "";
  while (SerialNano.available()) {
    char c = (char)SerialNano.read();
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        if (inputBuffer.startsWith("{") && inputBuffer.endsWith("}")) {
          parseNanoJSON(inputBuffer);
        }
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }
  }
}