#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <SparkFunBME280.h>
#include "strct.h" // Подключаем оригинальные структуры проекта

// Мьютекс для изоляции I2C шины (физическое переключение пинов)
extern SemaphoreHandle_t xLcdMutex;

// Мьютекс для защиты данных датчиков и ОТА (требуется для wifi_server.cpp)
extern SemaphoreHandle_t xSensorsMutex;

void startSens(stSens *vSensVal);
void getSensData(stSens *vSensVal);
void resetActualSensVal(stSens *vSensVal);
void switchToMeteoBus();
void switchToTouchBus();
void SCD30Calibration(); // Объявление для веб-сервера

#endif
