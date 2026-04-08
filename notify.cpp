#include "notify.h"
#include "settings.h"
#include "buzz.h"
#include "vib.h"

// GPIO4 = SDA_OLED on Heltec WiFi LoRa 32 V2 — cannot be used as LED.
// Set to a free GPIO when you add an external LED (e.g. a dedicated breakout pin).
// Use -1 to disable the LED indicator entirely.
#define LED_PIN (-1)

struct LedStep {
  uint16_t durationMs;
  bool     on;
};

// Maximum pattern length (SOS needs 9 steps: 3×short + 3×long + 3×short)
static const int MAX_STEPS = 20;
static LedStep  pattern[MAX_STEPS];
static int      patternLen   = 0;
static int      patternStep  = 0;
static uint32_t stepDeadline = 0;
static bool     patternRunning = false;

// Heartbeat state
static uint32_t lastHeartbeatMs = 0;

static inline bool ledAvailable() { return LED_PIN >= 0; }

void notifyInit() {
  if (ledAvailable()) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
  }
}

static void startPattern(const LedStep* steps, int len) {
  if (!ledAvailable() || len <= 0 || len > MAX_STEPS) return;
  memcpy(pattern, steps, len * sizeof(LedStep));
  patternLen     = len;
  patternStep    = 0;
  patternRunning = true;
  stepDeadline   = millis() + steps[0].durationMs;
  digitalWrite(LED_PIN, steps[0].on ? HIGH : LOW);
}

void notifyTick() {
  if (!ledAvailable() || !patternRunning) return;
  uint32_t now = millis();
  if (now < stepDeadline) return;

  patternStep++;
  if (patternStep >= patternLen) {
    patternRunning = false;
    digitalWrite(LED_PIN, LOW);
    return;
  }

  digitalWrite(LED_PIN, pattern[patternStep].on ? HIGH : LOW);
  stepDeadline = now + pattern[patternStep].durationMs;
}

void notifyHeartbeatTick() {
  if (!ledAvailable() || !g_settings.ledEnabled) return;
  uint32_t now = millis();
  if (now - lastHeartbeatMs < 5000) return;
  lastHeartbeatMs = now;

  static const LedStep pulse[] = {{80, true}, {80, false}};
  startPattern(pulse, 2);
}

void notifyIncoming(NotifyType type) {
  if (type == NOTIFY_SOS) {
    if (g_settings.ledEnabled && (g_settings.notifyMask & 0x01)) {
      // SOS Morse: 3 short · · · 3 long — — — 3 short · · ·
      static const LedStep sos[] = {
        {100, true}, {100, false},
        {100, true}, {100, false},
        {100, true}, {300, false},
        {300, true}, {100, false},
        {300, true}, {100, false},
        {300, true}, {300, false},
        {100, true}, {100, false},
        {100, true}, {100, false},
        {100, true}, {200, false},
      };
      startPattern(sos, 18);
    }
    if (g_settings.buzzEnabled && (g_settings.notifyMask & 0x02)) buzzIncoming();
    if (g_settings.vibEnabled  && (g_settings.notifyMask & 0x04)) vibIncoming();
  } else if (type == NOTIFY_INVITE) {
    if (g_settings.ledEnabled && (g_settings.notifyMask & 0x01)) {
      static const LedStep inv[] = {
        {150, true}, {100, false},
        {150, true}, {100, false},
        {150, true}, {200, false},
      };
      startPattern(inv, 6);
    }
    if (g_settings.buzzEnabled && (g_settings.notifyMask & 0x02)) buzzIncoming();
    if (g_settings.vibEnabled  && (g_settings.notifyMask & 0x04)) vibIncoming();
  } else {
    // Normal MSG or DISC
    if (g_settings.ledEnabled && (g_settings.notifyMask & 0x01)) {
      static const LedStep msg[] = {
        {200, true}, {200, false},
        {200, true}, {200, false},
      };
      startPattern(msg, 4);
    }
    if (g_settings.buzzEnabled && (g_settings.notifyMask & 0x02)) buzzIncoming();
    if (g_settings.vibEnabled  && (g_settings.notifyMask & 0x04)) vibIncoming();
  }
}
