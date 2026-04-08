#include <Arduino.h>
#include "settings.h"
#include "ui.h"
#include "input.h"
#include "protocol.h"
#include "storage.h"
#include "buzz.h"
#include "vib.h"
#include "power.h"
#include "notify.h"
#include "lock.h"
#include "history.h"
#include "btpair.h"

void setup() {
  settingsInit();              // load NVS settings first (needed by appInitHardware)
  appInitHardware();           // Vext + OLED + splash "Starting..."

  uiBootStep("Storage...");
  storageInit();

  uiBootStep("LoRa radio...");
  protocolInit();

  uiBootStep("Buzz/LED...");
  buzzInit();
  // vibInit();                // uncomment if vibration motor connected
  notifyInit();

  uiBootStep("Power...");
  powerInit();

  uiBootStep("Lock...");
  lockInit();

  uiBootStep("History...");
  historyInit();

  powerActivity();             // reset idle timer after all inits
  uiEnterBootPage();           // show first page (contacts / name / lock)
}

void loop() {
  powerTick();             // screen timeout / wake management
  protocolPoll();          // LoRa receive + dispatch
  protocolPendingTick();   // non-blocking retry timer
  protocolSOSTick();       // SOS repeat every 30s
  // protocolSearchTick(); // optional: keep search alive
  inputPoll();             // keypad / T9 + page routing
  lockTick();              // auto-lock timer
  buzzTick();              // non-blocking buzzer
  vibTick();               // non-blocking vibration (optional)
  notifyTick();            // LED pattern stepper
  notifyHeartbeatTick();   // heartbeat LED pulse every 5s
  uiTick();                // blink caret / small animations
  btpairTick();            // BLE proximity pairing state machine
}
