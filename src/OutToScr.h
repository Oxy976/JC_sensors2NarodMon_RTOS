#ifndef OutToScr_h
#define OutToScr_h

#include <Arduino.h>
#include "strct.h"

void OutToScr(stSens *vSensVal);
void ShowTime();
void processLcdAnimate();
void ScreenOn();
void ScreenOff();
bool isScreenFullyActive();

#endif
