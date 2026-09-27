#ifndef NANO_FLASHER_H
#define NANO_FLASHER_H

#include <Arduino.h>

void setupNanoFlasher();
void handleNanoFlasher();
void triggerNanoReset(); // <--- ודא ששורה זו קיימת בקובץ ה-.h

bool flashNanoHex(const uint8_t* hexData, size_t hexLen);

#endif