#include <Arduino.h>

// ===== Config you can tweak =====
#define VIB_PIN   12          // Hardware: GPIO12 → 100 Ω → MOSFET gate, 100 kΩ gate→GND. (Keep the diode across the motor and a 100 µF cap on Vext
static const int VIB_BITS = 10;   // PWM resolution
static const int VIB_FREQ = 200;  // 200 Hz is good for ERM coin motors

// Detect Arduino-ESP32 core major version (defaults to 2 if undefined)
#ifndef ESP_ARDUINO_VERSION_MAJOR
  #define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// ---- Use LEDC HAL on all cores (API differs between 2.x and 3.x) ----
#include "esp32-hal-ledc.h"

// Pick a channel (0..7 typically safe)
static const int VIB_CH = 1;

// ===== Pattern =====
struct VibSeg { uint8_t intensity; uint16_t ms; };
// intensity: 0..255 mapped to PWM duty
static VibSeg PATTERN_INCOMING[] = {
  {255,120},{0,80},{255,120},{0,0}
};

static bool     vibActive   = false;
static uint32_t segStartMs  = 0;
static int      segIndex    = 0;

// ===== Low-level driver =====
static inline uint32_t dutyFromIntensity(uint8_t intensity) {
  const uint32_t maxDuty = (1U << VIB_BITS) - 1U;
  return (uint32_t)intensity * maxDuty / 255U;
}

static void vibSet(uint8_t intensity) {
  ledcWrite(VIB_CH, dutyFromIntensity(intensity));
}

void vibInit() {
  pinMode(VIB_PIN, OUTPUT);

#if (ESP_ARDUINO_VERSION_MAJOR >= 3)
  // Newer core: single-call attach (sets freq + resolution + pin + channel)
  // Signature: ledcAttachChannel(pin, freqHz, resolution_bits, channel)
  ledcAttachChannel(VIB_PIN, VIB_FREQ, VIB_BITS, VIB_CH);
  ledcWrite(VIB_CH, 0);
#else
  // Older core: classic setup + attachPin
  ledcSetup(VIB_CH, VIB_FREQ, VIB_BITS);
  ledcAttachPin(VIB_PIN, VIB_CH);
  ledcWrite(VIB_CH, 0);
#endif
}

// ===== Public API =====
void vibIncoming() {
  vibActive  = true;
  segIndex   = 0;
  segStartMs = millis();
  vibSet(PATTERN_INCOMING[0].intensity);
}

void vibTick() {
  if (!vibActive) return;
  VibSeg &seg = PATTERN_INCOMING[segIndex];
  if (seg.ms == 0 && seg.intensity == 0) {
    vibActive = false; vibSet(0); return;
  }
  if (millis() - segStartMs >= seg.ms) {
    segIndex++;
    VibSeg &n = PATTERN_INCOMING[segIndex];
    if (n.ms == 0 && n.intensity == 0) { vibActive = false; vibSet(0); return; }
    vibSet(n.intensity);
    segStartMs = millis();
  }
}
