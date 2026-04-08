#include "input.h"
#include <Keypad.h>
#include "ui.h"
#include "storage.h"
#include "protocol.h"
#include "crypto.h"
#include "buzz.h"
#include "settings.h"
#include "power.h"
#include "lock.h"
#include "notify.h"
#include "history.h"
#include "btpair.h"

// ===== Keypad wiring (Heltec WiFi LoRa 32 V2) =====
static const byte ROWS = 4, COLS = 4;
static byte rowPins[ROWS] = {23, 22, 21, 17};
static byte colPins[COLS] = {13, 2, 32, 33};
static char keys[ROWS][COLS] = {
  {'1','2','3','U'},
  {'4','5','6','D'},
  {'7','8','9','E'},
  {'*','0','#','X'}
};
static Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ===== T9 state =====
static const char* T9_GROUPS[10] = {
  " ", ".,?!", "abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"
};
static String   composeBuffer = "";
static char     lastDigit     = 0;
static uint32_t lastTapMs     = 0;
static const uint16_t TAP_WINDOW_MS = 800;
static bool t9Uppercase = false;
static bool t9Numbers   = false;

// Lock PIN input buffer (separate from T9 compose)
static String lockPinBuf = "";

// ===== Expose compose buffer =====
const String& storageCompose()    { return composeBuffer; }
String&       storageComposeMut() { return composeBuffer; }
bool inputUppercase(){ return t9Uppercase; }
bool inputNumbers(){ return t9Numbers; }

const String& inputLockPin()     { return lockPinBuf; }
void          inputLockPinClear(){ lockPinBuf = ""; }

// ===== Contacts selection =====
static int contactsSel = 0;
int  storageContactsSel(){ return contactsSel; }
void storageContactsSelSet(int v){
  int cc = storageContactCount();
  if (cc == 0){ contactsSel = 0; return; }
  contactsSel = (v % cc + cc) % cc;
}

// ===== Config menu (9 items) =====
static int configSel = 0;
int  configSelGet(){ return configSel; }
void configSelSet(int v){ configSel = (v % CONFIG_ITEMS + CONFIG_ITEMS) % CONFIG_ITEMS; }

// ===== Confirm dialog =====
static int confirmSel = 0;
int  confirmSelGet()          { return confirmSel; }
void confirmSelSet(int v)     { confirmSel = (v <= 0) ? 0 : 1; }
void confirmSelToggle()       { confirmSel = 1 - confirmSel; }

// ===== Search selection =====
static int searchSel = 0;
int  searchSelGet(){ return searchSel; }
void searchSelSet(int v){
  extern int g_discCount;
  if (g_discCount <= 0){ searchSel = 0; return; }
  if (v < 0) v = 0;
  if (v > g_discCount-1) v = g_discCount-1;
  searchSel = v;
}

// ===== BT Pair page selection (0=Host, 1=Join) =====
static int btpairSel = 0;
int  btpairSelGet()          { return btpairSel; }
void btpairSelSet(int v)     { btpairSel = (v % 2 + 2) % 2; }

// ===== Empty contacts page selection (0=Pair via LoRa, 1=Pair via BT) =====
static int emptyContactsSel = 0;
int  emptyContactsSelGet()          { return emptyContactsSel; }
void emptyContactsSelSet(int v)     { emptyContactsSel = (v % 2 + 2) % 2; }

// ===== Settings sub-page selections =====
static int notifySel  = 0;
static int powerSel   = 0;
static int secSel     = 0;
static int msgSel     = 0;
static int sysSel     = 0;
static int detailSel  = 0;

int  settingsNotifySelGet()    { return notifySel; }
void settingsNotifySelSet(int v){ notifySel = (v % 4 + 4) % 4; }
int  settingsPowerSelGet()     { return powerSel; }
void settingsPowerSelSet(int v){ powerSel  = (v % 3 + 3) % 3; }
int  settingsSecSelGet()       { return secSel; }
void settingsSecSelSet(int v)  { secSel    = (v % 3 + 3) % 3; }
int  settingsMsgSelGet()       { return msgSel; }
void settingsMsgSelSet(int v)  { msgSel    = (v % 2 + 2) % 2; }
int  settingsSysSelGet()       { return sysSel; }
void settingsSysSelSet(int v)  { sysSel    = (v % 3 + 3) % 3; }
int  contactDetailSelGet()     { return detailSel; }
void contactDetailSelSet(int v){ detailSel = (v % 2 + 2) % 2; }

// ===== T9 helpers =====
static char applyCase(char c){
  if (!isalpha((int)c)) return c;
  return t9Uppercase ? (char)toupper((int)c) : c;
}
static void t9Backspace(){
  if (composeBuffer.length() > 0) composeBuffer.remove(composeBuffer.length()-1);
  lastDigit = 0;
}
static void t9InsertChar(char c){ composeBuffer += applyCase(c); }
static void t9InsertSpace(){ composeBuffer += ' '; lastDigit = 0; }
static void t9ToggleUpper(){ t9Uppercase = !t9Uppercase; if (t9Uppercase) t9Numbers = false; }
static void t9ToggleNumbers(){ t9Numbers = !t9Numbers; lastDigit = 0; if (t9Numbers) t9Uppercase = false; }

static void t9HandleDigit(char d, int maxLen){
  uint32_t now = millis();
  if (t9Numbers){
    if (d >= '0' && d <= '9'){
      if ((int)composeBuffer.length() < maxLen) composeBuffer += d;
      lastDigit = 0;
    }
    return;
  }
  if (d == '0'){
    if ((int)composeBuffer.length() < maxLen) t9InsertSpace();
    return;
  }
  int idx = d - '0';
  if (idx < 1 || idx > 9) return;
  const char* letters = T9_GROUPS[idx];
  size_t L = strlen(letters);

  if (lastDigit == d && (now - lastTapMs) < TAP_WINDOW_MS && composeBuffer.length() > 0){
    char cur = composeBuffer[composeBuffer.length()-1];
    char curLower = (char)tolower((int)cur);
    int pos = -1;
    for (size_t i = 0; i < L; i++) if (letters[i] == curLower){ pos = (int)i; break; }
    if (pos >= 0){
      composeBuffer.remove(composeBuffer.length()-1);
      char next = letters[(pos+1)%L];
      t9InsertChar(next);
    } else {
      if ((int)composeBuffer.length() < maxLen) t9InsertChar(letters[0]);
    }
  } else {
    if ((int)composeBuffer.length() < maxLen) t9InsertChar(letters[0]);
  }
  lastDigit = d;
  lastTapMs = now;
}

// ===== Long-press state for * key (SOS / Lock) =====
static uint32_t starPressMs  = 0;
static bool     starHoldFired = false;

// ===== SOS cancel hold tracking =====
static uint32_t sosHoldMs    = 0;

// ===== Power settings cycle helpers =====
static void cycleScreenTimeout(){
  const uint16_t opts[] = {10, 30, 60, 300, 0};
  const int N = 5;
  for (int i = 0; i < N; i++){
    if (g_settings.screenTimeoutSec == opts[i]){
      g_settings.screenTimeoutSec = opts[(i+1)%N];
      settingsSave(); return;
    }
  }
  g_settings.screenTimeoutSec = 60; settingsSave();
}
static void cycleSleepPoll(){
  const uint16_t opts[] = {500, 1000, 5000};
  const int N = 3;
  for (int i = 0; i < N; i++){
    if (g_settings.sleepPollMs == opts[i]){
      g_settings.sleepPollMs = opts[(i+1)%N];
      settingsSave(); return;
    }
  }
  g_settings.sleepPollMs = 1000; settingsSave();
}
static void cycleContrast(){
  const uint8_t opts[] = {64, 128, 200, 255};
  const int N = 4;
  for (int i = 0; i < N; i++){
    if (g_settings.oledContrast == opts[i]){
      g_settings.oledContrast = opts[(i+1)%N];
      oled.setContrast(g_settings.oledContrast);
      settingsSave(); return;
    }
  }
  g_settings.oledContrast = 200;
  oled.setContrast(200); settingsSave();
}
static void cycleLockTimeout(){
  const uint8_t opts[] = {0, 1, 5, 15, 60};
  const int N = 5;
  for (int i = 0; i < N; i++){
    if (g_settings.lockTimeoutMin == opts[i]){
      g_settings.lockTimeoutMin = opts[(i+1)%N];
      settingsSave(); return;
    }
  }
  g_settings.lockTimeoutMin = 5; settingsSave();
}
static void cycleDefaultPriority(){
  g_settings.defaultPriority = (g_settings.defaultPriority + 1) % 3;
  settingsSave();
}
static void cycleHistoryDays(){
  const uint8_t opts[] = {0, 1, 7, 30};
  const int N = 4;
  for (int i = 0; i < N; i++){
    if (g_settings.historyDays == opts[i]){
      g_settings.historyDays = opts[(i+1)%N];
      settingsSave(); return;
    }
  }
  g_settings.historyDays = 7; settingsSave();
}

// ===== Input poll =====
void inputPoll(){
  // Any key wakes screen
  bool wasOff = !powerIsScreenOn();

  // Long-press check for * (SOS / instant lock)
  if (starPressMs != 0 && !starHoldFired){
    if (millis() - starPressMs >= 2000 && keypad.isPressed('*')){
      starHoldFired = true;
      if (page != PAGE_SOS && page != PAGE_LOCK){
        // Trigger SOS if contacts exist, else instant lock
        if (g_settings.lockEnabled && lockHasPinSet()){
          lockNow();
          page = PAGE_LOCK;
          inputLockPinClear();
          uiDrawLock();
        } else {
          protocolStartSOS();
          page = PAGE_SOS;
          uiDrawSOS();
        }
      }
    }
  }

  // SOS auto-cancel check (any key held 3s while on SOS page)
  if (page == PAGE_SOS && sosHoldMs != 0){
    if (millis() - sosHoldMs >= 3000){
      protocolStopSOS();
      sosHoldMs = 0;
      page = PAGE_CONTACTS;
      uiDrawContacts();
      return;
    }
  }

  char k = keypad.getKey();
  if (!k) return;

  // Wake on keypress
  powerActivity();
  lockActivity();

  // If screen was off and now waking, show sleep/lock status and consume key
  if (wasOff){
    if (lockIsLocked()){
      page = PAGE_LOCK;
      inputLockPinClear();
      uiDrawLock();
    } else {
      page = PAGE_SLEEP_STATUS;
      uiDrawSleepStatus();
    }
    return;
  }

  // Track * press for long-press
  if (k == '*'){
    starPressMs   = millis();
    starHoldFired = false;
  }

  // ===== PAGE_LOCK =====
  if (page == PAGE_LOCK){
    if (k >= '0' && k <= '9'){
      if (lockPinBuf.length() < 8) lockPinBuf += k;
      uiDrawLock(); return;
    }
    if (k == 'X'){
      if (lockPinBuf.length() > 0){ lockPinBuf.remove(lockPinBuf.length()-1); }
      uiDrawLock(); return;
    }
    if (k == 'E'){
      if (lockPinBuf.length() >= 4){
        if (lockCheckPin(lockPinBuf)){
          lockPinBuf = "";
          page = PAGE_CONTACTS;
          uiDrawContacts();
        } else {
          lockPinBuf = "";
          uiDrawLock();
        }
      }
      return;
    }
    return;
  }

  // ===== PAGE_SLEEP_STATUS =====
  if (page == PAGE_SLEEP_STATUS){
    page = PAGE_CONTACTS;
    uiDrawContacts();
    return;
  }

  // ===== PAGE_SOS =====
  if (page == PAGE_SOS){
    if (sosHoldMs == 0) sosHoldMs = millis();
    return;  // wait for 3s hold to cancel (checked at top)
  }

  // ===== PAGE_NAME =====
  if (page == PAGE_NAME){
    if (k == 'E'){
      String name = storageCompose(); name.trim();
      if (name.length() == 0){ uiForceBlinkRestart(); uiDrawNameEntry(); return; }
      storageSaveName(name);
      storageComposeMut() = "";
      page = PAGE_CONTACTS;
      uiDrawContacts(); return;
    }
    if (k == 'X'){ t9Backspace(); uiDrawNameEntry(); return; }
    if (k >= '0' && k <= '9') t9HandleDigit(k, DEVICE_NAME_MAX_LEN);
    else if (k == '*') t9ToggleUpper();
    else if (k == '#') t9ToggleNumbers();
    uiDrawNameEntry(); return;
  }

  // ===== PAGE_CONTACTS =====
  if (page == PAGE_CONTACTS){
    if (storageContactCount() == 0){
      // Empty list: U/D navigate between pairing options
      if (k == 'U'){ emptyContactsSelSet(emptyContactsSel - 1); uiDrawContacts(); return; }
      if (k == 'D'){ emptyContactsSelSet(emptyContactsSel + 1); uiDrawContacts(); return; }
      if (k == 'E'){
        if (emptyContactsSel == 0){
          protocolNearbyClear();
          page = PAGE_SEARCH; uiDrawSearch(); protocolSendDiscReq();
        } else {
          btpairReset();
          btpairSel = 0;
          page = PAGE_BT_PAIR;
          uiDrawBTPair();
        }
        return;
      }
      if (k == 'X'){ configSelSet(0); page = PAGE_CONFIG; uiDrawConfig(); return; }
      return;
    }
    if (k == 'U'){ storageContactsSelSet(contactsSel-1); uiDrawContacts(); return; }
    if (k == 'D'){ storageContactsSelSet(contactsSel+1); uiDrawContacts(); return; }
    if (k == 'E'){
      {
        // Enter chat directly
        int idx = storageContactsSel();
        const Contact& c = storageContactAt(idx);
        protocolClearChat();
        historyLoad(c.id, c.key);   // load persisted history first
        protocolEnterChat(c.id);
        page = PAGE_CHAT;
        uiForceBlinkRestart();
        uiDrawChat();
      }
      return;
    }
    if (k == 'X'){
      configSelSet(0);
      page = PAGE_CONFIG;
      uiDrawConfig(); return;
    }
    if (k == '*' && !starHoldFired){
      // Short * on contacts: go to contact detail
      if (storageContactCount() > 0){
        detailSel = 0;
        page = PAGE_CONTACT_DETAIL;
        uiDrawContactDetail();
      }
      return;
    }
    return;
  }

  // ===== PAGE_CONTACT_DETAIL =====
  if (page == PAGE_CONTACT_DETAIL){
    if (k == 'U'){ contactDetailSelSet(detailSel-1); uiDrawContactDetail(); return; }
    if (k == 'D'){ contactDetailSelSet(detailSel+1); uiDrawContactDetail(); return; }
    if (k == 'X'){ page = PAGE_CONTACTS; uiDrawContacts(); return; }
    if (k == 'E'){
      int idx = storageContactsSel();
      if (idx < 0 || idx >= storageContactCount()){ page = PAGE_CONTACTS; uiDrawContacts(); return; }
      const Contact& c = storageContactAt(idx);
      if (detailSel == 0){
        // Chat
        protocolClearChat();
        historyLoad(c.id, c.key);
        protocolEnterChat(c.id);
        page = PAGE_CHAT;
        uiForceBlinkRestart();
        uiDrawChat();
      } else {
        // Delete contact + its history
        historyClear(c.id);
        storageDeleteContact(c.id);
        page = PAGE_CONTACTS;
        uiDrawContacts();
      }
      return;
    }
    return;
  }

  // ===== PAGE_CONFIG =====
  if (page == PAGE_CONFIG){
    if (k == 'U'){ configSelSet(configSelGet()-1); uiDrawConfig(); return; }
    if (k == 'D'){ configSelSet(configSelGet()+1); uiDrawConfig(); return; }
    if (k == 'X'){ page = PAGE_CONTACTS; uiDrawContacts(); return; }
    if (k == 'E'){
      int sel = configSelGet();
      if (sel == 0){
        protocolNearbyClear();
        page = PAGE_SEARCH; uiDrawSearch(); protocolSendDiscReq();
      }
      else if (sel == 1){
        btpairReset();
        btpairSel = 0;
        page = PAGE_BT_PAIR;
        uiDrawBTPair();
      }
      else if (sel == 2){ notifySel = 0; page = PAGE_SETTINGS_NOTIFY;   uiDrawSettingsNotify(); }
      else if (sel == 3){ powerSel = 0; page = PAGE_SETTINGS_POWER;    uiDrawSettingsPower(); }
      else if (sel == 4){ secSel = 0;   page = PAGE_SETTINGS_SECURITY; uiDrawSettingsSecurity(); }
      else if (sel == 5){ msgSel = 0;   page = PAGE_SETTINGS_MESSAGES; uiDrawSettingsMessages(); }
      else if (sel == 6){
        storageComposeMut() = "";
        page = PAGE_BROADCAST;
        uiForceBlinkRestart();
        uiDrawChat();
      }
      else if (sel == 7){ page = PAGE_CONTACTS; uiDrawContacts(); }
      else if (sel == 8){ sysSel = 0;   page = PAGE_SETTINGS_SYSTEM;  uiDrawSettingsSystem(); }
      return;
    }
    return;
  }

  // ===== PAGE_BT_PAIR =====
  if (page == PAGE_BT_PAIR){
    BTPairState st = btpairState();
    // When done or idle: X returns to config
    if (k == 'X'){
      btpairReset();
      page = PAGE_CONFIG;
      uiDrawConfig();
      return;
    }
    // Only allow navigation/start when IDLE
    if (st == BTPAIR_IDLE){
      if (k == 'U'){ btpairSelSet(btpairSel - 1); uiDrawBTPair(); return; }
      if (k == 'D'){ btpairSelSet(btpairSel + 1); uiDrawBTPair(); return; }
      if (k == 'E'){
        if (btpairSel == 0) btpairStartHost();
        else                btpairStartJoin();
        uiDrawBTPair();
        return;
      }
    }
    return;
  }

  // ===== PAGE_CONFIRM_RESET =====
  if (page == PAGE_CONFIRM_RESET){
    if (k == 'U' || k == 'D'){ confirmSelToggle(); uiDrawConfirmReset(); return; }
    if (k == 'X'){ page = PAGE_CONFIG; uiDrawConfig(); return; }
    if (k == 'E'){
      if (confirmSelGet() == 1){
        storageFactoryReset();
        settingsReset();
        historyFactoryReset();
        uiEnterBootPage();
      } else {
        page = PAGE_CONFIG; uiDrawConfig();
      }
      return;
    }
    return;
  }

  // ===== PAGE_SEARCH =====
  if (page == PAGE_SEARCH){
    if (k == 'U'){ searchSelSet(searchSelGet()-1); uiDrawSearch(); return; }
    if (k == 'D'){ searchSelSet(searchSelGet()+1); uiDrawSearch(); return; }
    if (k == 'X'){ page = PAGE_CONTACTS; uiDrawContacts(); return; }
    if (k == 'E'){
      extern int g_discCount;
      if (g_discCount <= 0) return;
      inviteReset();
      uint8_t toId   = g_disc[searchSelGet()].id;
      uint32_t code6 = (uint32_t)random(100000, 1000000);
      protocolSendInviteRequest(toId, code6);
      uiShowInviteCode(toId, code6);
      page = PAGE_INVITE_CODE;
      uiDrawInviteCode();
      return;
    }
    return;
  }

  // ===== PAGE_INVITE_CODE =====
  if (page == PAGE_INVITE_CODE){
    if (k == 'X'){ inviteReset(); protocolCancelInvite(); page = PAGE_CONTACTS; uiDrawContacts(); }
    return;
  }

  // ===== PAGE_INVITE_PROMPT =====
  if (page == PAGE_INVITE_PROMPT){
    if (k >= '0' && k <= '9'){
      String& buf = storageComposeMut();
      if (buf.length() < 6){ buf += k; uiForceBlinkRestart(); uiDrawInvitePrompt(); }
      return;
    }
    if (k == 'X'){
      String& buf = storageComposeMut();
      if (buf.length() > 0){ buf.remove(buf.length()-1); uiForceBlinkRestart(); uiDrawInvitePrompt(); }
      else { inviteReset(); page = PAGE_CONTACTS; uiDrawContacts(); }
      return;
    }
    if (k == 'E'){
      String s = storageCompose();
      if (s.length() != 6){ uiForceBlinkRestart(); uiDrawInvitePrompt(); return; }
      uint32_t code = (uint32_t)s.toInt();
      if (code == inviterCodeExpected){
        // Derive shared key
        Contact c{};
        c.id = inviterId;
        strlcpy(c.name, inviterName, sizeof(c.name));
        String myName   = storageDeviceName();
        String peerName = String(inviterName);
        derivePairKey(myName, peerName, code, inviterNonce8, c.key);
        storageAddContact(c);
        protocolSendInviteAccept(inviterId, code);
        storageComposeMut() = "";
        inviteReset();
        page = PAGE_CONTACTS;
        uiDrawContacts();
      } else {
        storageComposeMut() = "";
        uiForceBlinkRestart(); uiDrawInvitePrompt();
      }
      return;
    }
    return;
  }

  // ===== PAGE_CHAT =====
  if (page == PAGE_CHAT){
    if (k >= '1' && k <= '9'){ t9HandleDigit(k, CHAT_MSG_MAX_LEN); uiForceBlinkRestart(); uiDrawChat(); return; }
    if (k == '0'){
      if (t9Numbers) composeBuffer += '0'; else t9InsertSpace();
      uiForceBlinkRestart(); uiDrawChat(); return;
    }
    if (k == '*'){ t9ToggleUpper(); uiForceBlinkRestart(); uiDrawChat(); return; }
    if (k == '#'){ t9ToggleNumbers(); uiForceBlinkRestart(); uiDrawChat(); return; }
    if (k == 'X'){
      if (composeBuffer.length() == 0){ page = PAGE_CONTACTS; uiDrawContacts(); return; }
      t9Backspace(); uiForceBlinkRestart(); uiDrawChat(); return;
    }
    if (k == 'U'){ protocolScroll(+1); uiDrawChat(); return; }
    if (k == 'D'){ protocolScroll(-1); uiDrawChat(); return; }
    if (k == 'E'){
      if (composeBuffer.length() == 0) return;
      String text = composeBuffer; composeBuffer = ""; lastDigit = 0;
      MsgPriority pri = (MsgPriority)g_settings.defaultPriority;
      protocolSendChat(text, pri);
      uiDrawChat(); return;
    }
    return;
  }

  // ===== PAGE_BROADCAST =====
  if (page == PAGE_BROADCAST){
    if (k >= '1' && k <= '9'){ t9HandleDigit(k, CHAT_MSG_MAX_LEN); uiDrawChat(); return; }
    if (k == '0'){
      if (t9Numbers) composeBuffer += '0'; else t9InsertSpace();
      uiDrawChat(); return;
    }
    if (k == '*'){ t9ToggleUpper(); uiDrawChat(); return; }
    if (k == '#'){ t9ToggleNumbers(); uiDrawChat(); return; }
    if (k == 'X'){
      if (composeBuffer.length() == 0){ page = PAGE_CONFIG; uiDrawConfig(); return; }
      t9Backspace(); uiDrawChat(); return;
    }
    if (k == 'E'){
      if (composeBuffer.length() == 0) return;
      String text = composeBuffer; composeBuffer = "";
      protocolBroadcast(text);
      page = PAGE_CONTACTS; uiDrawContacts(); return;
    }
    return;
  }

  // ===== PAGE_SETTINGS_NOTIFY =====
  if (page == PAGE_SETTINGS_NOTIFY){
    if (k == 'U'){ settingsNotifySelSet(notifySel-1); uiDrawSettingsNotify(); return; }
    if (k == 'D'){ settingsNotifySelSet(notifySel+1); uiDrawSettingsNotify(); return; }
    if (k == 'X'){ page = PAGE_CONFIG; uiDrawConfig(); return; }
    if (k == 'E'){
      uint8_t bit = 1 << notifySel;
      g_settings.notifyMask ^= bit;
      // Sync individual flags
      g_settings.ledEnabled  = (bool)(g_settings.notifyMask & 0x01);
      g_settings.buzzEnabled = (bool)(g_settings.notifyMask & 0x02);
      g_settings.vibEnabled  = (bool)(g_settings.notifyMask & 0x04);
      settingsSave();
      uiDrawSettingsNotify();
      return;
    }
    return;
  }

  // ===== PAGE_SETTINGS_POWER =====
  if (page == PAGE_SETTINGS_POWER){
    if (k == 'U'){ settingsPowerSelSet(powerSel-1); uiDrawSettingsPower(); return; }
    if (k == 'D'){ settingsPowerSelSet(powerSel+1); uiDrawSettingsPower(); return; }
    if (k == 'X'){ page = PAGE_CONFIG; uiDrawConfig(); return; }
    if (k == 'E'){
      if (powerSel == 0) cycleScreenTimeout();
      else if (powerSel == 1) cycleSleepPoll();
      else if (powerSel == 2) cycleContrast();
      uiDrawSettingsPower();
      return;
    }
    return;
  }

  // ===== PAGE_SETTINGS_SECURITY =====
  if (page == PAGE_SETTINGS_SECURITY){
    if (k == 'U'){ settingsSecSelSet(secSel-1); uiDrawSettingsSecurity(); return; }
    if (k == 'D'){ settingsSecSelSet(secSel+1); uiDrawSettingsSecurity(); return; }
    if (k == 'X'){ page = PAGE_CONFIG; uiDrawConfig(); return; }
    if (k == 'E'){
      if (secSel == 0){
        g_settings.lockEnabled = !g_settings.lockEnabled;
        settingsSave();
        uiDrawSettingsSecurity();
      } else if (secSel == 1){
        cycleLockTimeout();
        uiDrawSettingsSecurity();
      } else if (secSel == 2){
        lockPinBuf = "";
        page = lockHasPinSet() ? PAGE_PIN_CHANGE : PAGE_PIN_SETUP;
        uiDrawPinSetup();
      }
      return;
    }
    return;
  }

  // ===== PAGE_PIN_SETUP / PAGE_PIN_CHANGE =====
  if (page == PAGE_PIN_SETUP || page == PAGE_PIN_CHANGE){
    if (k >= '0' && k <= '9'){
      if (lockPinBuf.length() < 8) lockPinBuf += k;
      uiForceBlinkRestart(); uiDrawPinSetup(); return;
    }
    if (k == 'X'){
      if (lockPinBuf.length() > 0){ lockPinBuf.remove(lockPinBuf.length()-1); uiForceBlinkRestart(); uiDrawPinSetup(); }
      else { lockPinBuf = ""; page = PAGE_SETTINGS_SECURITY; uiDrawSettingsSecurity(); }
      return;
    }
    if (k == 'E'){
      if (lockPinBuf.length() >= 4){
        lockSetPin(lockPinBuf);
        lockPinBuf = "";
        uiToast("PIN saved!");
        page = PAGE_SETTINGS_SECURITY;
        uiDrawSettingsSecurity();
      }
      return;
    }
    return;
  }

  // ===== PAGE_SETTINGS_MESSAGES =====
  if (page == PAGE_SETTINGS_MESSAGES){
    if (k == 'U'){ settingsMsgSelSet(msgSel-1); uiDrawSettingsMessages(); return; }
    if (k == 'D'){ settingsMsgSelSet(msgSel+1); uiDrawSettingsMessages(); return; }
    if (k == 'X'){ page = PAGE_CONFIG; uiDrawConfig(); return; }
    if (k == 'E'){
      if (msgSel == 0) cycleDefaultPriority();
      else if (msgSel == 1) cycleHistoryDays();
      uiDrawSettingsMessages();
      return;
    }
    return;
  }

  // ===== PAGE_SETTINGS_SYSTEM =====
  if (page == PAGE_SETTINGS_SYSTEM){
    if (k == 'U'){ settingsSysSelSet(sysSel-1); uiDrawSettingsSystem(); return; }
    if (k == 'D'){ settingsSysSelSet(sysSel+1); uiDrawSettingsSystem(); return; }
    if (k == 'X'){ page = PAGE_CONFIG; uiDrawConfig(); return; }
    if (k == 'E'){
      if (sysSel == 2){
        confirmSelSet(0);
        page = PAGE_CONFIRM_RESET;
        uiDrawConfirmReset();
      }
      // sysSel 0,1 are info-only
      return;
    }
    return;
  }
}
