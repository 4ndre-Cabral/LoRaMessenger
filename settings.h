#pragma once
#include <Arduino.h>

struct AppSettings {
  // Power
  uint16_t screenTimeoutSec;   // 0=never, 10, 30, 60, 300
  uint16_t sleepPollMs;        // recv interval in sleep mode (500–5000ms)
  uint8_t  oledContrast;       // 0–255

  // Notifications (bitmask)
  uint8_t  notifyMask;         // bit0=LED, bit1=buzz, bit2=vib, bit3=screen_flash

  // Security
  bool     lockEnabled;
  uint8_t  lockTimeoutMin;     // 0=immediate, 1, 5, 15, 60
  uint8_t  pinHash[32];        // PBKDF2-SHA256
  uint8_t  pinSalt[16];

  // Messages
  uint8_t  defaultPriority;    // 0=normal(3 retry), 1=high(7), 2=sos(15+repeat)
  bool     buzzEnabled;
  bool     vibEnabled;
  bool     ledEnabled;

  // History
  uint8_t  historyDays;        // 0=off, 1, 7, 30
};

extern AppSettings g_settings;

void settingsInit();
void settingsSave();
void settingsReset();
