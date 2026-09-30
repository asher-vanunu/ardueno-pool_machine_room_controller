#ifndef MODBUS_MANAGER_H
#define MODBUS_MANAGER_H

#include <Arduino.h>

enum PumpMode { MODE_AUTO, MODE_MANUAL };

void setupModbus();
void handleModbus();
bool setPumpCapacity(uint16_t capacityPercent);
bool setPumpFlowRate(uint16_t flowM3H);
bool setPumpPowerState(bool turnOn);
void requestPumpCapacityChange(uint16_t capacityPercent);
void requestPumpFlowChange(uint16_t flowM3H);
void setPumpMode(PumpMode mode);
PumpMode getPumpMode();
void setModbusEnabled(bool enabled);
bool isModbusEnabled();

#endif