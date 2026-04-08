#pragma once
#include <Arduino.h>

// Fixed-size record stored in LittleFS per contact
struct __attribute__((packed)) HistoryRecord {
  uint32_t timestamp;   // millis() approximation (epoch if RTC available later)
  uint8_t  fromId;
  uint8_t  flags;       // bit0=priority_high, bit1=priority_sos, bit2=mine
  uint8_t  len;         // plaintext length (≤ 60)
  uint8_t  nonce4[4];   // XOR cipher nonce
  uint8_t  ctext[60];   // encrypted payload
};

static_assert(sizeof(HistoryRecord) == 71, "HistoryRecord size mismatch");

void historyInit();                          // mount LittleFS, purge old records
void historyRecord(uint8_t contactId,        // save a message
                   uint8_t fromId,
                   const String& text,
                   uint8_t flags,
                   const uint8_t key[32]);
void historyLoad(uint8_t contactId,          // load into chat buffer
                 const uint8_t key[32]);
void historyPurge();                         // delete records older than historyDays
void historyClear(uint8_t contactId);        // delete one contact's file
void historyFactoryReset();                  // delete /hist directory
