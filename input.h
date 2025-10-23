
#pragma once
#include <Arduino.h>

void inputPoll();

// Compose buffer access for UI
const String& storageCompose();
String&       storageComposeMut();

// Mode flags for UI indicators
bool inputUppercase();
bool inputNumbers();

// Contacts page selection exposure
int  storageContactsSel();
void storageContactsSelSet(int v);

int  configSelGet();
void configSelSet(int v);

// Confirm dialog (0 = No, 1 = Yes)
int  confirmSelGet();
void confirmSelSet(int v);
void confirmSelToggle();

int  searchSelGet();
void searchSelSet(int v);
