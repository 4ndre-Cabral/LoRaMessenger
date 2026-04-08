#pragma once
#include <Arduino.h>

// Proximity BLE pairing: both devices must be within ~10m.
// HOST advertises a random 32-byte key via BLE GATT.
// JOIN scans, connects, reads the key and sends its own id+name back.
// After pairing both sides call storageAddContact() with the shared key
// and communicate via LoRa normally.

enum BTPairState : uint8_t {
  BTPAIR_IDLE,
  BTPAIR_HOST,       // advertising, waiting for JOIN to connect
  BTPAIR_JOIN,       // scanning / connecting to HOST
  BTPAIR_DONE_OK,
  BTPAIR_DONE_FAIL,
};

void        btpairStartHost();   // start BLE server (advertise key)
void        btpairStartJoin();   // start BLE client scan
void        btpairStop();        // stop BLE activity
void        btpairTick();        // call from loop()
void        btpairReset();       // return to IDLE (safe to call anytime)

BTPairState btpairState();
const char* btpairStatus();      // human-readable status string
