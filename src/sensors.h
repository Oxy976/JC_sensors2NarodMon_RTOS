#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "strct.h" 

// Убедитесь, что константа SensUnit определена в strct.h (минимум 17 для текущей конфигурации)
extern SemaphoreHandle_t xLcdMutex;
extern SemaphoreHandle_t xSensorsMutex;

void startSens(stSens *vSensVal);
void getSensData(stSens *vSensVal);
void resetActualSensVal(stSens *vSensVal);
void switchToMeteoBus();
void switchToTouchBus();
void SCD30Calibration(); 
void triggerHeatCycle();
void processAsyncHeating();
void logAllSensorsDebug(stSens *vSensVal);

#endif
