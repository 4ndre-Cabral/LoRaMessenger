
#include <Arduino.h>
#include "ui.h"
#include "input.h"
#include "protocol.h"
#include "storage.h"
#include "buzz.h"
#include "vib.h"

void setup() {
  appInitHardware();     // Vext + Display
  storageInit();         // Preferences (device name + contacts)
  protocolInit();        // LoRa pins + begin + receive
  buzzInit();
  // vibInit();
  uiEnterBootPage();     // decide PAGE_NAME or PAGE_CONTACTS
}

void loop() {
  protocolPoll();        // LoRa parse + dispatch
  protocolSearchTick();
  inputPoll();           // keypad/T9 + page routing
  buzzTick();            // non-blocking beeps
  vibTick();             // non-blocking vibration (optional)
  uiTick();              // cursor blink / small animations
}
