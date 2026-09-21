#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <WiFi.h>
#include <PubSubClient.h>

void setupMQTT();
void handleMQTT();

void publishPumpTelemetry(bool isRunning, uint16_t powerW, uint16_t flowM3H, float energyKWh);
void publishNanoTelemetry(float tCol, float tSt, float tFlw, bool pumpOn, int state);

// פונקציה חדשה לפרסום הגדרות החימום ל-MQTT
void publishNanoConfig(float sMax, float tDel, float tOn, float tOff, float dtO, float dtF);

#endif