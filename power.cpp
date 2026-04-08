#include "power.h"
#include "settings.h"
#include "ui.h"

#define BATTERY_ADC_PIN 35
// GPIO connected to TP4054 CHRG pin (active-low) — set to -1 if not wired
#define CHARGING_PIN    -1

static uint32_t lastActivityMs = 0;
static bool screenOn = true;

// Battery ADC smoothing
static int   adcSamples[8] = {0};
static int   adcIdx        = 0;
static bool  adcReady      = false;

void powerInit() {
  analogReadResolution(12);
  // Set attenuation only for the battery pin (GPIO35), not all channels
  // Uses analogSetPinAttenuation for ESP32 Arduino 2.x/3.x compatibility
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  lastActivityMs = millis();
  screenOn = true;

  if (CHARGING_PIN >= 0) {
    pinMode(CHARGING_PIN, INPUT_PULLUP);
  }
}

void powerWake() {
  if (!screenOn) {
    screenOn = true;
    oled.displayOn();
    oled.setContrast(g_settings.oledContrast);
  }
  lastActivityMs = millis();
}

void powerActivity() {
  lastActivityMs = millis();
  if (!screenOn) {
    powerWake();
  }
}

bool powerIsScreenOn() {
  return screenOn;
}

void powerTick() {
  // Update ADC sample ring buffer
  adcSamples[adcIdx] = analogRead(BATTERY_ADC_PIN);
  adcIdx = (adcIdx + 1) % 8;
  if (adcIdx == 0) adcReady = true;

  if (g_settings.screenTimeoutSec == 0 || !screenOn) return;

  uint32_t now = millis();
  uint32_t timeoutMs = (uint32_t)g_settings.screenTimeoutSec * 1000UL;
  if (now - lastActivityMs >= timeoutMs) {
    screenOn = false;
    oled.displayOff();
  }
}

uint8_t powerBatteryPct() {
  // Average ADC samples for stability
  long sum = 0;
  int count = adcReady ? 8 : adcIdx;
  if (count == 0) count = 1;
  for (int i = 0; i < count; i++) sum += adcSamples[i];
  int raw = (int)(sum / count);

  // Heltec V2: GPIO35 with 1:2 voltage divider, 3.3V ADC ref, 12-bit
  // Actual battery voltage = (raw / 4095.0) * 3.3 * 2
  float voltage = ((float)raw / 4095.0f) * 3.3f * 2.0f;

  // LiPo: 3.0V = 0%, 4.2V = 100%
  int pct = (int)(((voltage - 3.0f) / 1.2f) * 100.0f);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return (uint8_t)pct;
}

bool powerIsCharging() {
  if (CHARGING_PIN < 0) return false;
  return (digitalRead(CHARGING_PIN) == LOW);  // TP4054 CHRG is active-low
}
