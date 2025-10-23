
#include "storage.h"
#include <Preferences.h>

static Preferences prefs;
static String deviceName;
static Contact contacts[10];
static int contactCount = 0;

void storageInit(){
  prefs.begin("loraim", false);
  deviceName = prefs.getString("name", "");
  contactCount = prefs.getUChar("cc", 0);
  if (contactCount<0 || contactCount>10) contactCount=0;
  for (int i=0;i<contactCount;i++){
    char keyn[8]; snprintf(keyn,sizeof(keyn),"c%02d",i);
    uint8_t buf[1+16+32]; size_t len=prefs.getBytes(keyn, buf, sizeof(buf));
    if (len==sizeof(buf)){
      contacts[i].id = buf[0];
      memcpy(contacts[i].name, buf+1, 16);
      memcpy(contacts[i].key,  buf+17, 32);
    }
  }
  prefs.end();
}

const String& storageDeviceName(){ return deviceName; }

void storageSaveName(const String& n){
  deviceName = n;
  prefs.begin("loraim", false);
  prefs.putString("name", deviceName);
  prefs.end();
}

int storageContactCount(){ return contactCount; }
const Contact& storageContactAt(int i){ return contacts[i]; }

int storageFindContact(uint8_t id){
  for (int i=0;i<contactCount;i++) if (contacts[i].id==id) return i;
  return -1;
}

bool storageAddContact(const Contact& c){
  int idx = storageFindContact(c.id);
  if (idx>=0) return true;
  if (contactCount>=10) return false;
  contacts[contactCount] = c;
  contactCount++;
  storageSaveContacts();
  return true;
}

void storageSaveContacts(){
  prefs.begin("loraim", false);
  prefs.putUChar("cc", contactCount);
  for (int i=0;i<contactCount;i++){
    char keyn[8]; snprintf(keyn,sizeof(keyn),"c%02d",i);
    uint8_t buf[1+16+32];
    buf[0]=contacts[i].id;
    memcpy(buf+1, contacts[i].name, 16);
    memcpy(buf+17, contacts[i].key, 32);
    prefs.putBytes(keyn, buf, sizeof(buf));
  }
  prefs.end();
}

void storageFactoryReset(){
  Preferences p; p.begin("loraim", false);
  p.clear(); p.end();
  deviceName = "";
  contactCount = 0;
}

void storageClearContacts(){
  Preferences p; p.begin("loraim", false);
  for (int i=0;i<10;i++){ char keyn[8]; snprintf(keyn,sizeof(keyn),"c%02d",i); p.remove(keyn); }
  p.putUChar("cc", 0);
  p.end();
  contactCount = 0;
}

void storageClearName(){
  Preferences p; p.begin("loraim", false);
  p.remove("name");
  p.end();
  deviceName = "";
}
