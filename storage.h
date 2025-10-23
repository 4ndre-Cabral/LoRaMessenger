
#pragma once
#include <Arduino.h>

struct Contact {
  uint8_t id;
  char    name[16];
  uint8_t key[32];
};

void storageInit();

const String& storageDeviceName();
void storageSaveName(const String& n);

int  storageContactCount();
const Contact& storageContactAt(int i);
int  storageFindContact(uint8_t id);
bool storageAddContact(const Contact& c);
void storageSaveContacts();

void storageFactoryReset();   // wipe name + contacts
void storageClearContacts();  // wipe contacts only
void storageClearName();      // wipe name only
