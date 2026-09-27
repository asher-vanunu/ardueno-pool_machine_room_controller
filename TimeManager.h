#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>

void setupRTC();
void syncSystemTimeWithRTC();
void updateRTCFromNTP();
void printCurrentTime();
void handleTime();

#endif