#ifndef OUT_TO_SCR_H
#define OUT_TO_SCR_H

#include "sensors.h"

void init_lvgl_ui();
void OutToScr(stSens *vSensVal);
void ScreenOn();
void ScreenOff();
void processLcdAnimate();
bool isScreenFullyActive();
void ShowTime();
void reset_screen_timeout();

#endif
