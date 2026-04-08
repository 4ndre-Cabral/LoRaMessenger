#include "settings.h"
#include <Preferences.h>

AppSettings g_settings;

static const AppSettings DEFAULTS = {
  60,      // screenTimeoutSec
  1000,    // sleepPollMs
  200,     // oledContrast
  0x0B,    // notifyMask: LED + buzz + screen_flash (bits 0,1,3)
  false,   // lockEnabled
  5,       // lockTimeoutMin
  {0},     // pinHash
  {0},     // pinSalt
  0,       // defaultPriority (normal)
  true,    // buzzEnabled
  false,   // vibEnabled
  true,    // ledEnabled
  7        // historyDays
};

void settingsInit() {
  Preferences p;
  p.begin("cfg", true);
  bool initialized = p.getBool("init", false);
  p.end();

  if (!initialized) {
    g_settings = DEFAULTS;
    settingsSave();
    return;
  }

  p.begin("cfg", true);
  g_settings.screenTimeoutSec = p.getUShort("stout", DEFAULTS.screenTimeoutSec);
  g_settings.sleepPollMs      = p.getUShort("spoll", DEFAULTS.sleepPollMs);
  g_settings.oledContrast     = p.getUChar("ocon",   DEFAULTS.oledContrast);
  g_settings.notifyMask       = p.getUChar("nmask",  DEFAULTS.notifyMask);
  g_settings.lockEnabled      = p.getBool("locken",  DEFAULTS.lockEnabled);
  g_settings.lockTimeoutMin   = p.getUChar("lockto", DEFAULTS.lockTimeoutMin);
  p.getBytes("phash", g_settings.pinHash, 32);
  p.getBytes("psalt", g_settings.pinSalt, 16);
  g_settings.defaultPriority  = p.getUChar("dprio",  DEFAULTS.defaultPriority);
  g_settings.buzzEnabled      = p.getBool("buzzen",  DEFAULTS.buzzEnabled);
  g_settings.vibEnabled       = p.getBool("viben",   DEFAULTS.vibEnabled);
  g_settings.ledEnabled       = p.getBool("leden",   DEFAULTS.ledEnabled);
  g_settings.historyDays      = p.getUChar("hdays",  DEFAULTS.historyDays);
  p.end();
}

void settingsSave() {
  Preferences p;
  p.begin("cfg", false);
  p.putBool("init",    true);
  p.putUShort("stout", g_settings.screenTimeoutSec);
  p.putUShort("spoll", g_settings.sleepPollMs);
  p.putUChar("ocon",   g_settings.oledContrast);
  p.putUChar("nmask",  g_settings.notifyMask);
  p.putBool("locken",  g_settings.lockEnabled);
  p.putUChar("lockto", g_settings.lockTimeoutMin);
  p.putBytes("phash",  g_settings.pinHash, 32);
  p.putBytes("psalt",  g_settings.pinSalt, 16);
  p.putUChar("dprio",  g_settings.defaultPriority);
  p.putBool("buzzen",  g_settings.buzzEnabled);
  p.putBool("viben",   g_settings.vibEnabled);
  p.putBool("leden",   g_settings.ledEnabled);
  p.putUChar("hdays",  g_settings.historyDays);
  p.end();
}

void settingsReset() {
  g_settings = DEFAULTS;
  Preferences p;
  p.begin("cfg", false);
  p.clear();
  p.end();
}
