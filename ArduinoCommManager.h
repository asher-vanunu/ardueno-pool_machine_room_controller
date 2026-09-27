#ifndef ARDUINO_COMM_MANAGER_H
#define ARDUINO_COMM_MANAGER_H

#include <Arduino.h>

void setupArduinoComm();
void handleArduinoComm();

void sendNanoMode(const char* mode);
void sendNanoSetSMax(float val);
void sendNanoSetTDEL(float val);
void sendNanoSetTON(float val);
void sendNanoSetTOFF(float val);
void sendNanoSetDTO(float val);
void sendNanoSetDTF(float val);
void sendNanoSave();

// פונקציות עבור צורב הנאנו (NanoFlasher)
void pauseArduinoComm();
void resumeArduinoComm();
HardwareSerial& getNanoSerial();

#endif