#include "ModbusManager.h"
#include "Config.h"
#include "NanoFlasher.h"

// Forward declaration
extern void publishPumpTelemetry(bool isRunning, uint16_t capacityPct, uint16_t powerW, uint16_t flowM3H, float energyKWh, const char* modeStr);

#ifndef MODBUS_MOCK_MODE
  #include <HardwareSerial.h>
  #include <ModbusMaster.h>

  static ModbusMaster node;
  static HardwareSerial SerialRS485(2);

  static void preTransmission() {
    if (RS485_DIR_PIN >= 0) digitalWrite(RS485_DIR_PIN, HIGH);
  }

  static void postTransmission() {
    if (RS485_DIR_PIN >= 0) digitalWrite(RS485_DIR_PIN, LOW);
  }
#else
  static bool mockRunning = false;
  static uint16_t mockCapacity = 0;
  static uint16_t mockFlow = 0;
  static uint16_t mockPower = 0;
  static float mockEnergy = 10.50f;
#endif

static unsigned long lastModbusPoll = 0;
static uint16_t pendingTargetVal = 0;
static uint8_t pendingCommandType = 0; // 0 = none, 1 = flow, 2 = capacity
static bool commandChangePending = false;

static bool modbusCommunicationEnabled = true;

// משתנים לשמירת מצב המשאבה והערכים האחרונים
static PumpMode currentPumpMode = MODE_MANUAL;
static uint16_t savedFlow = 15;
static uint16_t savedCapacity = 80;
static bool isPumpRunning = false;

void setupModbus() {
#ifndef MODBUS_MOCK_MODE
  if (RS485_DIR_PIN >= 0) {
    pinMode(RS485_DIR_PIN, OUTPUT);
    digitalWrite(RS485_DIR_PIN, LOW);
  }

  SerialRS485.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  node.begin(MODBUS_SLAVE_ID, SerialRS485);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  WebSerial.println("[Modbus] REAL Hardware Mode active. Aquagem driver ready.");
#else
  WebSerial.println("[Modbus] *** MOCK MODE ACTIVE ***");
#endif
}

void setModbusEnabled(bool enabled) {
  if (!enabled && modbusCommunicationEnabled) {
    #ifndef MODBUS_MOCK_MODE
      // שחרור המשאבה בחזרה לאפליקציה המקומית
      uint8_t result = node.writeSingleRegister(REG_WRITE_CAP, 0);
      if (result == node.ku8MBSuccess) {
        WebSerial.println("[Modbus] Safe OFF command (0) sent successfully to pump.");
      } else {
        WebSerial.printf("[Modbus] Safe OFF command failed: 0x%02X\n", result);
      }
    #endif
  }
  
  modbusCommunicationEnabled = enabled;
  WebSerial.printf("[Modbus] Communication bridge is now %s\n", enabled ? "ENABLED" : "DISABLED (Released to App)");
}

bool isModbusEnabled() {
  return modbusCommunicationEnabled;
}

void setPumpMode(PumpMode mode) {
  currentPumpMode = mode;
  // אם המשאבה עובדת, שינוי המצב שולח מיד את הפקודה התואמת
  if (isPumpRunning) {
    if (mode == MODE_AUTO) {
      requestPumpFlowChange(savedFlow);
    } else {
      requestPumpCapacityChange(savedCapacity);
    }
  }
  WebSerial.printf("[Modbus] Pump mode changed to %s\n", (mode == MODE_AUTO) ? "AUTO" : "MANUAL");
}

PumpMode getPumpMode() {
  return currentPumpMode;
}

void requestPumpFlowChange(uint16_t flowM3H) {
  if (!modbusCommunicationEnabled) return;
  currentPumpMode = MODE_AUTO; // עדכון פנימי אוטומטי למצב
  pendingTargetVal = flowM3H;
  pendingCommandType = 1; // Flow
  commandChangePending = true;
}

void requestPumpCapacityChange(uint16_t capacityPercent) {
  if (!modbusCommunicationEnabled) return;
  currentPumpMode = MODE_MANUAL; // עדכון פנימי אוטומטי למצב
  pendingTargetVal = capacityPercent;
  pendingCommandType = 2; // Capacity %
  commandChangePending = true;
}

bool setPumpCapacity(uint16_t capacityPercent) {
  if (!modbusCommunicationEnabled) return false;

  if (capacityPercent != 0 && (capacityPercent < 30 || capacityPercent > 120)) {
    WebSerial.printf("[Modbus] Error: Capacity %d%% out of range!\n", capacityPercent);
    return false;
  }
  
  if (capacityPercent != 0) savedCapacity = capacityPercent;

#ifndef MODBUS_MOCK_MODE
  uint8_t result = node.writeSingleRegister(REG_WRITE_CAP, capacityPercent);
  if (result == node.ku8MBSuccess) {
    WebSerial.printf("[Modbus] Capacity set to %d%%\n", capacityPercent);
    return true;
  }
  WebSerial.printf("[Modbus] Write capacity error: 0x%02X\n", result);
  return false;
#else
  mockCapacity = capacityPercent;
  mockRunning = (capacityPercent != 0 && capacityPercent > 0);
  mockFlow = mockRunning ? 15 : 0;
  mockPower = mockRunning ? (capacityPercent * 12) : 0; 
  WebSerial.printf("[Mock Modbus] Set capacity to %d%%\n", mockCapacity);
  return true;
#endif
}

bool setPumpFlowRate(uint16_t flowM3H) {
  if (!modbusCommunicationEnabled) return false;

  if (flowM3H != 0 && (flowM3H < 8 || flowM3H > 25)) {
    WebSerial.printf("[Modbus] Error: Flow %d m3/h out of range!\n", flowM3H);
    return false;
  }

  if (flowM3H != 0) savedFlow = flowM3H;

#ifndef MODBUS_MOCK_MODE
  uint8_t result = node.writeSingleRegister(REG_WRITE_FLOW, flowM3H);
  if (result == node.ku8MBSuccess) {
    WebSerial.printf("[Modbus] Target flow rate set to %d m3/h\n", flowM3H);
    return true;
  }
  WebSerial.printf("[Modbus] Write flow error: 0x%02X\n", result);
  return false;
#else
  mockFlow = flowM3H;
  mockRunning = (flowM3H != 0 && flowM3H > 0);
  mockCapacity = 75;
  mockPower = mockRunning ? 800 : 0; 
  WebSerial.printf("[Mock Modbus] Set flow to %d m3/h\n", mockFlow);
  return true;
#endif
}

bool setPumpPowerState(bool turnOn) {
  if (!modbusCommunicationEnabled) return false;
  
  if (turnOn) {
    // בהדלקה, שלח את הנתון בהתאם למצב השמור
    return (currentPumpMode == MODE_AUTO) ? setPumpFlowRate(savedFlow) : setPumpCapacity(savedCapacity);
  } else {
    // בכיבוי, שלח 0 לכתובת התואמת למצב
    return (currentPumpMode == MODE_AUTO) ? setPumpFlowRate(0) : setPumpCapacity(0);
  }
}

void handleModbus() {
  if (!modbusCommunicationEnabled) {
    return;
  }

  if (commandChangePending) {
    commandChangePending = false;
    uint8_t cmdType = pendingCommandType;
    uint16_t val = pendingTargetVal;
    pendingCommandType = 0;

    if (cmdType == 1) {
      setPumpFlowRate(val);
    } else if (cmdType == 2) {
      setPumpCapacity(val);
    }
    delay(50); 
  }

  unsigned long now = millis();
  if (now - lastModbusPoll > 3000) { 
    lastModbusPoll = now;

#ifndef MODBUS_MOCK_MODE
    // נבקש 8 רגיסטרים כדי לכלול גם את גרסת התוכנה ב-0x07D9
    uint8_t result = node.readHoldingRegisters(REG_READ_STATE, 8);
    if (result == node.ku8MBSuccess) {
      uint16_t rawState    = node.getResponseBuffer(0); // 0x07D2
      uint16_t capacityPct = node.getResponseBuffer(1); // 0x07D3
      uint16_t powerW      = node.getResponseBuffer(2); // 0x07D4
      uint16_t flowM3H     = node.getResponseBuffer(3); // 0x07D5
      uint16_t pressure    = node.getResponseBuffer(4); // 0x07D6
      uint16_t rawEnergy   = node.getResponseBuffer(5); // 0x07D7
      uint16_t modeCode    = node.getResponseBuffer(6); // 0x07D8
      uint16_t swVersion   = node.getResponseBuffer(7); // 0x07D9

      isPumpRunning = (rawState & 0x01);
      float energyKWh = rawEnergy / 1000.0f;
      
      // הדפסה רגילה
      WebSerial.printf("[Modbus] SUCCESS: Pump=%s, Cap=%d%%, Flow=%d m3/h, Power=%d W, Energy=%.2f kWh, ModeCode=%d\n", 
                       (isPumpRunning ? "ON" : "OFF"), capacityPct, flowM3H, powerW, energyKWh, modeCode);

      // --- הדפסת דיאגנוסטיקה גולמית לטרמינל ---
      WebSerial.printf("[DEBUG RAW] 0x07D2:%d, 0x07D3:%d, 0x07D4:%d, 0x07D5:%d, 0x07D6:%d, 0x07D7:%d, 0x07D8:%d, 0x07D9:%d\n",
                       rawState, capacityPct, powerW, flowM3H, pressure, rawEnergy, modeCode, swVersion);

      const char* modeStr = (currentPumpMode == MODE_AUTO) ? "Auto Inverter" : "Manual Inverter";
      publishPumpTelemetry(isPumpRunning, capacityPct, powerW, flowM3H, energyKWh, modeStr);
    } else {
      WebSerial.printf("[Modbus] Read telemetry error: 0x%02X\n", result);
    }
#else
    if (mockRunning) mockEnergy += 0.001f;
    isPumpRunning = mockRunning;
    uint16_t mockModeCode = (currentPumpMode == MODE_AUTO) ? 20 : 30;
    WebSerial.printf("[Mock Modbus] SUCCESS: Pump=%s, Cap=%d%%, Flow=%d m3/h, Power=%d W, Energy=%.2f kWh, ModeCode=%d\n", 
                     (isPumpRunning ? "ON" : "OFF"), mockCapacity, mockFlow, mockPower, mockEnergy, mockModeCode);

    const char* modeStr = (currentPumpMode == MODE_AUTO) ? "Auto Inverter" : "Manual Inverter";
    publishPumpTelemetry(mockRunning, mockCapacity, mockPower, mockFlow, mockEnergy, modeStr);
#endif
  }
}