#ifndef NANO_FLASHER_H
#define NANO_FLASHER_H

#include <Arduino.h>

void setupNanoFlasher();
void handleNanoFlasher();

// פונקציה ראשית לצריבה מתוך זיכרון/סטרים
bool flashNanoHex(const uint8_t* hexData, size_t hexLen);

#endif