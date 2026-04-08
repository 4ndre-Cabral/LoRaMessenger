#pragma once
#include <Arduino.h>

void     lockInit();
void     lockTick();           // check auto-lock timer
bool     lockIsLocked();
void     lockActivity();       // reset idle timer
void     lockNow();            // immediately lock

bool     lockCheckPin(const String& pin);   // returns true if PIN matches
void     lockSetPin(const String& pin);     // hash + salt + save

int      lockFailCount();
uint32_t lockLockoutUntil();   // millis() timestamp; 0 = not locked out
bool     lockHasPinSet();
