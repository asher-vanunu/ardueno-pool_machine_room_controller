#ifndef MODBUS_MANAGER_H
#define MODBUS_MANAGER_H

#include <Arduino.h>

void setupModbus();
void handleModbus();

bool setPumpFlowRate(uint16_t flowM3H);
bool setPumpPowerState(bool turnOn);

#endif