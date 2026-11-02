#pragma once
#ifndef sensors_h
#define sensors_h
#include <Arduino.h>
#include <Wire.h>

extern TwoWire WireSensors; // Метео-шина на IO17/18

#include "strct.h"
#include <Adafruit_BME280.h> // <- Новый драйвер от Adafruit
#include "radSens1v2.h"
#include <SHT2x.h>                          
#include "SHT31.h"                          
#include "SparkFun_SCD30_Arduino_Library.h" 
#include <OneWire.h>           
#include "DallasTemperature.h" 

extern void scanI2CBus();
extern void startSens(stSens *vSensVal);
extern void getSensData(stSens *vSensVal); 
extern void resetActualSensVal(stSens *vSensVal);
extern void SCD30Calibration();
void triggerHeatCycle();
void processAsyncHeating();
#endif
