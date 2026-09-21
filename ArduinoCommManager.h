#ifndef ARDUINO_COMM_MANAGER_H
#define ARDUINO_COMM_MANAGER_H

#include <Arduino.h>

void setupArduinoComm();
void handleArduinoComm();

// שליחת מצב עבודה
void sendNanoMode(const char* mode);

// פונקציות להגדרת הפרמטרים הבודדים מתוך HeatControlMenu
void sendNanoSetSMax(float val);
void sendNanoSetTDEL(float val);
void sendNanoSetTON(float val);
void sendNanoSetTOFF(float val);
void sendNanoSetDTO(float val);
void sendNanoSetDTF(float val);

// פקודת שמירה ב-EEPROM של ה-Nano
void sendNanoSave();

#endif