#include "ArduinoCommManager.h"
#include "Config.h"
#include <HardwareSerial.h>
#include <ArduinoJson.h>

extern void publishNanoTelemetry(float tCol, float tSt, float tFlw, bool pumpOn, int state);
extern void publishNanoConfig(float sMax, float tDel, float tOn, float tOff, float dtO, float dtF);

static HardwareSerial SerialNano(NANO_UART_NUM);
static unsigned long lastPollTime = 0;
static bool commPaused = false;

// משתנים עבור בקרת לוג
static bool logEnabled = false;
static bool logPendingSend = false;
static unsigned long logScheduledTime = 0;
static String latestLogBuffer = "";
static bool isFirstLogFrame = true; // מבטיח הדפסת Device Set_up בכל פעם שהלוג מופעל

// שמירת ערכי הקונפיגורציה מהסטטוס האחרון
static float current_sMax = 35.0f;
static float current_tDel = 1.0f;
static float current_tOn  = 8.0f;
static float current_tOff = 3.0f;
static float current_dtO  = 8.0f;
static float current_dtF  = 2.0f;

void setNanoLogEnabled(bool enabled) {
  logEnabled = enabled;
  Serial.printf("[ArduinoComm] Log state changed to: %s\n", enabled ? "ENABLED" : "DISABLED");
  if (enabled) {
    isFirstLogFrame = true; // איפוס כדי שידפיס את ה-Setup ברשומה הראשונה
  } else {
    logPendingSend = false;
  }
}

bool isNanoLogEnabled() {
  return logEnabled;
}

String getLatestLogText() {
  String out = latestLogBuffer;
  latestLogBuffer = "";
  return out;
}

void clearLogBuffer() {
  latestLogBuffer = "";
}

void pauseArduinoComm() {
  commPaused = true;
}

void resumeArduinoComm() {
  commPaused = false;
  SerialNano.begin(NANO_BAUDRATE, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
}

HardwareSerial& getNanoSerial() {
  return SerialNano;
}

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

void sendNanoResetDefault() {
  SerialNano.println("CMD:DEFAULT");
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
    current_sMax = doc["sMAX"] | 35.0f;
    current_tDel = doc["tDEL"] | 1.0f;
    current_tOn  = doc["tON"]  | 8.0f;
    current_tOff = doc["tOFF"] | 3.0f;
    current_dtO  = doc["DT_O"] | 8.0f;
    current_dtF  = doc["DT_F"] | 2.0f;

    publishNanoConfig(current_sMax, current_tDel, current_tOn, current_tOff, current_dtO, current_dtF);
  }
}

static void parseNanoLogJSON(const String& jsonStr) {
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) {
    Serial.printf("[ArduinoComm] Log JSON Error: %s\n", error.c_str());
    latestLogBuffer += "[RAW LOG] " + jsonStr + "\r\n";
    return;
  }

  int frame         = doc["frame"] | 0;
  float tCol        = doc["tCOL"] | 0.0f;
  float tSt         = doc["tST"] | 0.0f;
  float tFlw        = doc["tFLW"] | 0.0f;
  float tCol_last   = doc["tCOL_last"] | 0.0f;
  int state         = doc["state"] | 0;
  bool pump         = (doc["pump"] | 0) == 1;
  uint32_t on_time  = doc["on_time"] | 0;
  uint32_t off_time = doc["off_time"] | 0;
  bool needHeat     = (doc["needHeat"] | 0) == 1;
  int needHeatHist  = doc["needHeatHist"] | 0;
  bool needStop     = (doc["needStop"] | 0) == 1;
  int needStopHist  = doc["needStopHist"] | 0;
  float prevtCOL    = doc["prevtCOL"] | 0.0f;
  float difftCOL    = doc["difftCOL"] | 0.0f;
  float maxdifftCOL = doc["maxdifftCOL"] | 0.0f;
  float diff_tCOL_TH= doc["diff_tCOL_TH"] | 0.0f;

  // עדכון ערכים אם קיימים בחבילת הלוג
  if (doc.containsKey("sMAX")) current_sMax = doc["sMAX"] | current_sMax;
  if (doc.containsKey("tDEL")) current_tDel = doc["tDEL"] | current_tDel;
  if (doc.containsKey("tON"))  current_tOn  = doc["tON"]  | current_tOn;
  if (doc.containsKey("tOFF")) current_tOff = doc["tOFF"] | current_tOff;
  if (doc.containsKey("DT_O")) current_dtO  = doc["DT_O"] | current_dtO;
  if (doc.containsKey("DT_F")) current_dtF  = doc["DT_F"] | current_dtF;

  String entry = "";

  // הדפסת פרמטרי ה-Setup פעם אחת בכל תחילת הפעלת לוג
  if (isFirstLogFrame) {
    isFirstLogFrame = false;
    entry += "######## Device Set_up ##########\r\n";
    entry += "sMAX: " + String(current_sMax, 1) + "\r\n";
    entry += "tDEL: " + String(current_tDel, 1) + "\r\n";
    entry += "tON:  " + String(current_tOn, 1)  + "\r\n";
    entry += "tOFF: " + String(current_tOff, 1) + "\r\n";
    entry += "DT_O: " + String(current_dtO, 1)  + "\r\n";
    entry += "DT_F: " + String(current_dtF, 1)  + "\r\n";
    entry += "####################################\r\n\r\n";
  }

  entry += "Frame:        " + String(frame) + "\r\n";
  entry += "tCOL:         " + String(tCol, 2) + "\r\n";
  entry += "tST:          " + String(tSt, 2) + "\r\n";
  entry += "tFLW:         " + String(tFlw, 2) + "\r\n";
  entry += "tCOL_last:    " + String(tCol_last, 2) + "\r\n";
  entry += "State:        " + String(state) + "\r\n";
  entry += "Pump State:   " + String(pump ? "1" : "0") + "\r\n";
  entry += "ON time:      " + String(on_time) + "\r\n";
  entry += "OFF time:     " + String(off_time) + "\r\n";
  entry += "Need Heat:    " + String(needHeat ? "1" : "0") + "\r\n";
  entry += "NeedHeatHist: " + String(needHeatHist) + "\r\n";
  entry += "Need Stop:    " + String(needStop ? "1" : "0") + "\r\n";
  entry += "NeedStopHist: " + String(needStopHist) + "\r\n";
  entry += "PrevtCOL:     " + String(prevtCOL, 2) + "\r\n";
  entry += "DifftCOL:     " + String(difftCOL, 2) + "\r\n";
  entry += "maxDifftCOL:  " + String(maxdifftCOL, 2) + "\r\n";
  entry += "diff_tCOL_TH: " + String(diff_tCOL_TH, 2) + "\r\n";
  entry += "----------------------------------\r\n";

  latestLogBuffer += entry;
}

void handleArduinoComm() {
  if (commPaused) return;

  unsigned long now = millis();

  // שליחת בקשת נתונים כל 3 שניות
  if (now - lastPollTime > 3000) {
    lastPollTime = now;
    SerialNano.println("GET:STATUS");
    
    // אם הלוג מופעל, מתזמנים שליחת נתוני לוג
    if (logEnabled) {
      logPendingSend = true;
      logScheduledTime = now + 400;
    }
  }

  // שליחת פקודת בקשת לוג
  if (logPendingSend && now >= logScheduledTime) {
    logPendingSend = false;
    SerialNano.println("GET:LOG");
  }

  static String inputBuffer = "";
  while (SerialNano.available()) {
    char c = (char)SerialNano.read();
    if (c == '\n' || c == '\r') {
      inputBuffer.trim();
      if (inputBuffer.length() > 0) {
        if (inputBuffer.startsWith("{") && inputBuffer.endsWith("}")) {
          if (inputBuffer.indexOf("\"type\":\"log\"") != -1) {
            parseNanoLogJSON(inputBuffer);
          } else {
            parseNanoJSON(inputBuffer);
          }
        }
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
      // חגורת בטיחות: איפוס הזיכרון אם נוצרת מחרוזת ארוכה מדי עקב רעש או חוסר בתו ירידת שורה
      if (inputBuffer.length() > 1024) {
        inputBuffer = "";
      }
    }
  }
}