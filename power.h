#pragma once
#include <Arduino.h>

void    powerInit();
void    powerTick();
void    powerWake();
void    powerActivity();      // reset idle timer + wake if screen off
uint8_t powerBatteryPct();
bool    powerIsCharging();
bool    powerIsScreenOn();
