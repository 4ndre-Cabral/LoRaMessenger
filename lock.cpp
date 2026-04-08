#include "lock.h"
#include "settings.h"
#include "mbedtls/pkcs5.h"
#include <esp_random.h>

static bool     locked         = false;
static uint32_t lastActivityMs = 0;
static int      failCount      = 0;
static uint32_t lockoutUntil   = 0;

void lockInit() {
  lastActivityMs = millis();
  // Lock on startup if lock is enabled and a PIN has been set
  if (g_settings.lockEnabled && lockHasPinSet()) {
    locked = true;
  }
}

bool lockIsLocked() {
  return locked;
}

bool lockHasPinSet() {
  // Check if pinHash is non-zero (i.e., a PIN was set)
  for (int i = 0; i < 32; i++) {
    if (g_settings.pinHash[i] != 0) return true;
  }
  return false;
}

void lockActivity() {
  lastActivityMs = millis();
}

void lockNow() {
  if (g_settings.lockEnabled && lockHasPinSet()) {
    locked = true;
  }
}

void lockTick() {
  if (!g_settings.lockEnabled || locked || !lockHasPinSet()) return;
  if (g_settings.lockTimeoutMin == 0) return;

  uint32_t now       = millis();
  uint32_t timeoutMs = (uint32_t)g_settings.lockTimeoutMin * 60000UL;
  if (now - lastActivityMs >= timeoutMs) {
    locked = true;
  }
}

int      lockFailCount()     { return failCount; }
uint32_t lockLockoutUntil()  { return lockoutUntil; }

static void computePbkdf2(const String& pin, const uint8_t salt[16], uint8_t out[32]) {
  mbedtls_pkcs5_pbkdf2_hmac_ext(
    MBEDTLS_MD_SHA256,
    (const unsigned char*)pin.c_str(), pin.length(),
    (const unsigned char*)salt, 16,
    20000,
    32, out
  );
}

bool lockCheckPin(const String& pin) {
  uint32_t now = millis();
  if (lockoutUntil > 0 && now < lockoutUntil) return false;

  uint8_t computed[32];
  computePbkdf2(pin, g_settings.pinSalt, computed);

  bool match = (memcmp(computed, g_settings.pinHash, 32) == 0);
  if (match) {
    locked       = false;
    failCount    = 0;
    lockoutUntil = 0;
    lastActivityMs = millis();
  } else {
    failCount++;
    if (failCount >= 5) {
      // Progressive lockout: 30s × 2^(extra failures)
      int extra = failCount - 5;
      if (extra > 4) extra = 4;   // cap at 30 × 16 = 480s
      uint32_t lockSec = 30UL * (1UL << extra);
      lockoutUntil = now + lockSec * 1000UL;
    }
  }
  return match;
}

void lockSetPin(const String& pin) {
  // New random 16-byte salt
  for (int i = 0; i < 16; i++) {
    g_settings.pinSalt[i] = (uint8_t)(esp_random() & 0xFF);
  }
  computePbkdf2(pin, g_settings.pinSalt, g_settings.pinHash);
  settingsSave();
}
