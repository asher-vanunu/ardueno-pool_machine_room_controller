#include "ModbusManager.h"
#include "Config.h"

// Forward declaration
extern void publishPumpTelemetry(bool isRunning, uint16_t powerW, uint16_t flowM3H, float energyKWh);

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
  // משתני דימוי עבור מצב MOCK
  static bool mockRunning = false;
  static uint16_t mockFlow = 0;
  static uint16_t mockPower = 0;
  static float mockEnergy = 10.50f;
#endif

static unsigned long lastModbusPoll = 0;

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

  Serial.println("[Modbus] REAL Hardware Mode active. Aquagem driver ready.");
#else
  Serial.println("[Modbus] *** MOCK MODE ACTIVE *** (Skipping hardware pins to prevent crashes)");
#endif
}

bool setPumpFlowRate(uint16_t flowM3H) {
  if (flowM3H != 0 && (flowM3H < 8 || flowM3H > 25)) {
    Serial.printf("[Modbus] Error: Flow rate %d m3/h out of range (8-25)!\n", flowM3H);
    return false;
  }

#ifndef MODBUS_MOCK_MODE
  uint8_t result = node.writeSingleRegister(REG_WRITE_FLOW, flowM3H);
  if (result == node.ku8MBSuccess) {
    Serial.printf("[Modbus] Target flow rate set to %d m3/h\n", flowM3H);
    return true;
  }
  Serial.printf("[Modbus] Write flow error: 0x%02X\n", result);
  return false;
#else
  mockFlow = flowM3H;
  mockRunning = (flowM3H > 0);
  mockPower = mockRunning ? (flowM3H * 35) : 0; // דימוי צריכת הספק לפי ספיקה
  Serial.printf("[Mock Modbus] Set flow to %d m3/h (Simulated Power: %dW)\n", mockFlow, mockPower);
  return true;
#endif
}

bool setPumpPowerState(bool turnOn) {
  return setPumpFlowRate(turnOn ? 15 : 0);
}

void handleModbus() {
  unsigned long now = millis();
  if (now - lastModbusPoll > 3000) { // Poll telemetry every 3 seconds
    lastModbusPoll = now;

#ifndef MODBUS_MOCK_MODE
    // Read 6 registers starting at 2002 (0x07D2)
    uint8_t result = node.readHoldingRegisters(REG_READ_STATE, 6);
    if (result == node.ku8MBSuccess) {
      uint16_t rawState  = node.getResponseBuffer(0x07D2 - REG_READ_STATE);
      uint16_t powerW    = node.getResponseBuffer(0x07D4 - REG_READ_STATE);
      uint16_t flowM3H   = node.getResponseBuffer(0x07D5 - REG_READ_STATE);
      uint16_t rawEnergy = node.getResponseBuffer(0x07D7 - REG_READ_STATE);

      bool isRunning = (rawState & 0x01);
      float energyKWh = rawEnergy / 1000.0f;

      publishPumpTelemetry(isRunning, powerW, flowM3H, energyKWh);
    } else {
      Serial.printf("[Modbus] Read telemetry error: 0x%02X\n", result);
    }
#else
    if (mockRunning) mockEnergy += 0.001f;
    publishPumpTelemetry(mockRunning, mockPower, mockFlow, mockEnergy);
#endif
  }
}