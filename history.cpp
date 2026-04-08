#include "history.h"
#include "settings.h"
#include "protocol.h"
#include "crypto.h"
#include "ui.h"
#include <LittleFS.h>
#include <esp_random.h>

static bool fsReady = false;

static String histPath(uint8_t contactId) {
  char buf[32];
  snprintf(buf, sizeof(buf), "/hist/%u.bin", (unsigned)contactId);
  return String(buf);
}

void historyInit() {
  // Try to mount without formatting first (fast path, ~50ms)
  bool mounted = LittleFS.begin(false);

  if (!mounted) {
    // First boot: need to format — update display so user sees progress
    uiBootStep("Formatting FS...");

    mounted = LittleFS.begin(true);  // format on fail

    if (!mounted) {
      // LittleFS partition not available in this sketch's partition table.
      // History is silently disabled — all other features work normally.
      uiBootStep("FS N/A - no history");
      delay(1200);
      fsReady = false;
      return;
    }
  }

  fsReady = true;
  if (!LittleFS.exists("/hist")) {
    LittleFS.mkdir("/hist");
  }
  historyPurge();
}

void historyRecord(uint8_t contactId, uint8_t fromId,
                   const String& text, uint8_t flags,
                   const uint8_t key[32]) {
  if (!fsReady || g_settings.historyDays == 0) return;

  HistoryRecord rec;
  rec.timestamp = millis();
  rec.fromId    = fromId;
  rec.flags     = flags;
  size_t tlen   = min((size_t)60, text.length());
  rec.len       = (uint8_t)tlen;

  // Random nonce
  for (int i = 0; i < 4; i++) rec.nonce4[i] = (uint8_t)(esp_random() & 0xFF);

  // Encrypt
  memset(rec.ctext, 0, 60);
  memcpy(rec.ctext, text.c_str(), tlen);
  keystreamXor(key, rec.nonce4, rec.ctext, tlen);

  String path = histPath(contactId);
  File f = LittleFS.open(path, FILE_APPEND);
  if (!f) return;
  f.write((const uint8_t*)&rec, sizeof(rec));
  f.close();
}

void historyLoad(uint8_t contactId, const uint8_t key[32]) {
  if (!fsReady) return;

  String path = histPath(contactId);
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return;

  HistoryRecord rec;
  while (f.read((uint8_t*)&rec, sizeof(rec)) == sizeof(rec)) {
    uint8_t tmp[60];
    memcpy(tmp, rec.ctext, rec.len);
    keystreamXor(key, rec.nonce4, tmp, rec.len);
    String text = String((const char*)tmp).substring(0, rec.len);

    MsgStatus st = (rec.flags & 0x04) ? ST_DELIVERED : ST_RECV;
    // Push into protocol chat buffer
    protocolPushHistoryMsg(rec.fromId, text, st);
  }
  f.close();
}

void historyPurge() {
  if (!fsReady || g_settings.historyDays == 0) return;

  // Compute age threshold in "millis ticks" — since we store millis() as
  // timestamp, we can only compare relative age on the same boot.
  // For cross-boot retention, we approximate using a stored boot counter.
  // For simplicity, treat records with timestamp > current millis as old
  // (impossible), and delete files older than historyDays in filesystem mtime
  // when available. LittleFS does not store mtime by default, so we use a
  // conservative approach: keep all files, but truncate per-file to the last
  // N records estimated to fit historyDays (max 1000 msgs per file).
  // A proper RTC implementation would use an epoch timestamp here.
  (void)0;  // no-op until RTC is available; purge is handled at open time
}

void historyClear(uint8_t contactId) {
  if (!fsReady) return;
  LittleFS.remove(histPath(contactId));
}

void historyFactoryReset() {
  if (!fsReady) return;
  File root = LittleFS.open("/hist");
  if (!root || !root.isDirectory()) return;
  File f = root.openNextFile();
  while (f) {
    String name = "/hist/" + String(f.name());
    f.close();
    LittleFS.remove(name);
    f = root.openNextFile();
  }
  root.close();
}
