#pragma once
#include <Arduino.h>

void inputPoll();

// Compose buffer access for UI
const String& storageCompose();
String&       storageComposeMut();

// Mode flags for UI indicators
bool inputUppercase();
bool inputNumbers();

// Contacts page selection
int  storageContactsSel();
void storageContactsSelSet(int v);

// Config menu (9 items)
static constexpr int CONFIG_ITEMS = 9;
int  configSelGet();
void configSelSet(int v);

// Confirm dialog (0=No, 1=Yes)
int  confirmSelGet();
void confirmSelSet(int v);
void confirmSelToggle();

// Search selection
int  searchSelGet();
void searchSelSet(int v);

// Settings sub-page selections
int  settingsNotifySelGet();
void settingsNotifySelSet(int v);
int  settingsPowerSelGet();
void settingsPowerSelSet(int v);
int  settingsSecSelGet();
void settingsSecSelSet(int v);
int  settingsMsgSelGet();
void settingsMsgSelSet(int v);
int  settingsSysSelGet();
void settingsSysSelSet(int v);

// Contact detail selection (0=Chat, 1=Delete)
int  contactDetailSelGet();
void contactDetailSelSet(int v);

// BT Pair page selection (0=Host, 1=Join)
int  btpairSelGet();
void btpairSelSet(int v);

// Empty contacts page selection (0=Pair via LoRa, 1=Pair via BT)
int  emptyContactsSelGet();
void emptyContactsSelSet(int v);

// Lock PIN input buffer (separate from compose)
const String& inputLockPin();
void          inputLockPinClear();
