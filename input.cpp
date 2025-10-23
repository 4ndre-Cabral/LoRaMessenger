
#include "input.h"
#include <Keypad.h>
#include "ui.h"
#include "storage.h"
#include "protocol.h"
#include "buzz.h"

// Keypad wiring (adjust to your board pins)
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

// T9 state
static const char* T9_GROUPS[10] = {
  " ", ".,?!", "abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"
};
static String composeBuffer = "";
static char      lastDigit = 0;
static uint32_t  lastTapMs = 0;
static const uint16_t TAP_WINDOW_MS = 800;
static bool t9Uppercase = false;
static bool t9Numbers   = false;

// expose to UI
const String& storageCompose(){ return composeBuffer; }
String&       storageComposeMut(){ return composeBuffer; }
bool inputUppercase(){ return t9Uppercase; }
bool inputNumbers(){ return t9Numbers; }

// Contacts selection index
static int contactsSel = 0;
int  storageContactsSel(){ return contactsSel; }
void storageContactsSelSet(int v){
  int cc = storageContactCount();
  if (cc==0){ contactsSel=0; return; }
  if (v<0) v=0; if (v>=cc) v=cc-1;
  contactsSel = v;
}

// ----- Config menu selection (wraps 0..2) -----
static int configSel = 0;
static constexpr int CONFIG_ITEMS = 3;

int  configSelGet(){ return configSel; }
void configSelSet(int v){
  configSel = (v % CONFIG_ITEMS + CONFIG_ITEMS) % CONFIG_ITEMS;
}

// ----- Confirm dialog selection: 0=No, 1=Yes -----
static int confirmSel = 0;
int  confirmSelGet(){ return confirmSel; }
void confirmSelSet(int v){ confirmSel = (v <= 0 ? 0 : 1); }
void confirmSelToggle(){ confirmSel = 1 - confirmSel; }

// Searching selection
static int searchSel = 0;
int  searchSelGet(){ return searchSel; }
void searchSelSet(int v){
  extern int g_discCount;         // from protocol.cpp
  if (g_discCount <= 0) { searchSel = 0; return; }
  if (v < 0) v = 0;
  if (v > g_discCount-1) v = g_discCount-1;
  searchSel = v;
}

// helpers
static char applyCase(char c){
  if (!isalpha((int)c)) return c;
  return t9Uppercase ? (char)toupper((int)c) : c;
}
static void t9Backspace(){
  if (composeBuffer.length()>0) composeBuffer.remove(composeBuffer.length()-1);
  lastDigit=0;
}
static void t9InsertChar(char c){ composeBuffer += applyCase(c); }
static void t9InsertSpace(){ composeBuffer += ' '; lastDigit=0; }
static void t9ToggleUpper(){ t9Uppercase = !t9Uppercase; if (t9Uppercase) t9Numbers=false; }
static void t9ToggleNumbers(){ t9Numbers = !t9Numbers; lastDigit=0; if (t9Numbers) t9Uppercase=false; }

static void t9HandleDigit(char d, int maxLen){
  uint32_t now = millis();

  // Numbers mode: digits go in directly (respect maxLen)
  if (t9Numbers) {
    if (d >= '0' && d <= '9') {
      if ((int)composeBuffer.length() < maxLen) {
        composeBuffer += d;
        lastDigit = 0;
      }
      return;
    }
  }

  if (d=='0') { // handled outside normally; keep guard
    if ((int)composeBuffer.length() < maxLen) t9InsertSpace();
    return;
  }

  int idx = d - '0';                // 1..9
  if (idx < 1 || idx > 9) return;
  const char* letters = T9_GROUPS[idx];
  size_t L = strlen(letters);

  if (lastDigit == d && (now - lastTapMs) < TAP_WINDOW_MS && composeBuffer.length()>0){
    // cycle last character (does not change length, so always allowed)
    char cur = composeBuffer[composeBuffer.length()-1];
    char curLower = (char)tolower((int)cur);
    int pos = -1;
    for (size_t i=0;i<L;i++) if (letters[i]==curLower){ pos=(int)i; break; }
    if (pos >= 0){
      composeBuffer.remove(composeBuffer.length()-1);
      char next = letters[(pos+1)%L];
      t9InsertChar(next);
    } else {
      // last char wasn’t in this group; append first if under limit
      if ((int)composeBuffer.length() < maxLen) t9InsertChar(letters[0]);
    }
  } else {
    // new group or window expired -> append first letter (respect limit)
    if ((int)composeBuffer.length() < maxLen) t9InsertChar(letters[0]);
  }

  lastDigit = d;
  lastTapMs = now;
}

void inputPoll(){
  char k = keypad.getKey();
  if (!k) return;

  if (page == PAGE_NAME){
    if (k=='E'){
      String name = storageCompose();
      name.trim();
      if (name.length() == 0) {      // ignore empty
        uiForceBlinkRestart(); uiDrawNameEntry();
        return;
      }
      storageSaveName(name);          // your existing save
      storageComposeMut() = "";
      page = PAGE_CONTACTS;
      uiDrawContacts();
      return;
    }
    if (k=='X'){ t9Backspace(); uiDrawNameEntry(); return; }
    if ((k>='0'&&k<='9')||k=='*'||k=='#'){
      if (k>='0'&&k<='9') t9HandleDigit(k, DEVICE_NAME_MAX_LEN);
      else if (k=='*') t9ToggleUpper();
      else if (k=='#') t9ToggleNumbers();
      uiDrawNameEntry(); return;
    }
    return;
  }

  if (page == PAGE_CONTACTS){
    if (k=='U'){ storageContactsSelSet(contactsSel-1); uiDrawContacts(); return; }
    if (k=='D'){ storageContactsSelSet(contactsSel+1); uiDrawContacts(); return; }
    if (k=='E'){
      if (storageContactCount()==0){
        protocolNearbyClear();
        page=PAGE_SEARCH; uiDrawSearch(); protocolSendDiscReq();
      } else {
        protocolEnterChat(storageContactAt(contactsSel).id);
        page=PAGE_CHAT;
        uiForceBlinkRestart();
        uiDrawChat();
      }
      return;
    }
    if (k=='X'){ // broadcast
      configSelSet(0);
      page=PAGE_CONFIG;
      uiDrawConfig();
      return;
    }
    return;
  }

  if (page == PAGE_CONFIG){
    if (k=='U'){ configSelSet(configSelGet()-1); uiDrawConfig(); return; }
    if (k=='D'){ configSelSet(configSelGet()+1); uiDrawConfig(); return; }
    if (k=='X'){ page=PAGE_CONTACTS; uiDrawContacts(); return; }  // ESC = back

    if (k=='E'){  // ENTER = select
      int sel = configSelGet();
      if (sel == 0){
        // Broadcast
        storageComposeMut() = "";
        page = PAGE_BROADCAST;
        uiForceBlinkRestart();
        uiDrawChat();
        return;
      } else if (sel == 1){
        // Contact List (back to main page)
        page = PAGE_CONTACTS;
        uiDrawContacts();
        return;
      } else {
        // Factory reset
        confirmSelSet(0);       // default to "No"
        page = PAGE_CONFIRM_RESET;
        uiDrawConfirmReset();
        return;
      }
    }
    return;
  }

  if (page == PAGE_CONFIRM_RESET){
    if (k=='U' || k=='D'){ confirmSelToggle(); uiDrawConfirmReset(); return; } // toggle Yes/No
    if (k=='X'){ // ESC = cancel
      page = PAGE_CONFIG;
      uiDrawConfig();
      return;
    }
    if (k=='E'){
      if (confirmSelGet() == 1){
        // YES -> wipe and go to name entry
        storageFactoryReset();      // keeps your 3 methods available
        uiEnterBootPage();          // your existing helper that shows name screen
      } else {
        // NO -> back to config
        page = PAGE_CONFIG;
        uiDrawConfig();
      }
      return;
    }
    return;
  }

  if (page == PAGE_SEARCH){
    if (k=='U'){ searchSelSet(searchSelGet()-1); uiDrawSearch(); return; }
    if (k=='D'){ searchSelSet(searchSelGet()+1); uiDrawSearch(); return; }
    if (k=='X'){ page = PAGE_CONTACTS; uiDrawContacts(); return; }

    if (k=='E'){
      if (g_discCount <= 0) return;

      inviteReset();

      uint8_t toId = g_disc[searchSelGet()].id;
      uint32_t code6 = (uint32_t)random(100000, 1000000);

      // send request + show our code
      protocolSendInviteRequest(toId, code6);
      uiShowInviteCode(toId, code6);

      page = PAGE_INVITE_CODE;
      uiDrawInviteCode();
      return;
    }
    return;
  }

  if (page == PAGE_INVITE_CODE){
    if (k=='X'){
      inviteReset();
      protocolCancelInvite();
      page=PAGE_CONTACTS;
      uiDrawContacts();
    }
    return;
  }

  if (page == PAGE_INVITE_PROMPT){
    if (k >= '0' && k <= '9'){
      String &buf = storageComposeMut();
      if (buf.length() < 6){
        buf += k;
        uiForceBlinkRestart(); uiDrawInvitePrompt();
      }
      return;
    }
    if (k == 'X'){
      String &buf = storageComposeMut();
      if (buf.length() > 0){
        buf.remove(buf.length()-1);        // delete last digit
        uiForceBlinkRestart(); uiDrawInvitePrompt();
      } else {
        inviteReset();                      // leave prompt only when empty
        page = PAGE_CONTACTS;
        uiDrawContacts();
      }
      return;
    }
    if (k == 'E'){
      String s = storageCompose();
      if (s.length() != 6) { uiForceBlinkRestart(); uiDrawInvitePrompt(); return; }
      uint32_t code = (uint32_t)s.toInt();

      if (code == inviterCodeExpected){
        // add contact (by Contact&)
        Contact c{};
        c.id = inviterId;
        strlcpy(c.name, inviterName, sizeof(c.name));
        storageAddContact(c);

        // notify inviter
        protocolSendInviteAccept(inviterId, code);

        // done
        storageComposeMut() = "";
        inviteReset();
        page = PAGE_CONTACTS;
        uiDrawContacts();
      } else {
        // wrong code -> clear and retry
        storageComposeMut() = "";
        uiForceBlinkRestart(); uiDrawInvitePrompt();
      }
      return;
    }
    return;
  }

  if (page == PAGE_CHAT){
    if (k>='1' && k<='9'){ t9HandleDigit(k, CHAT_MSG_MAX_LEN); uiForceBlinkRestart(); uiDrawChat(); return; }
    if (k=='0'){ if (t9Numbers) composeBuffer+='0'; else t9InsertSpace(); uiForceBlinkRestart(); uiDrawChat(); return; }
    if (k=='*'){ t9ToggleUpper(); uiForceBlinkRestart(); uiDrawChat(); return; }
    if (k=='#'){ t9ToggleNumbers(); uiForceBlinkRestart(); uiDrawChat(); return; }
    if (k=='X'){
      if (composeBuffer.length()==0){
        page = PAGE_CONTACTS;
        uiDrawContacts();
        return;
      };
      t9Backspace();
      uiForceBlinkRestart();
      uiDrawChat();
      return;
    }
    if (k=='U'){ protocolScroll(+1); uiDrawChat(); return; }
    if (k=='D'){ protocolScroll(-1); uiDrawChat(); return; }
    if (k=='E'){
      if (composeBuffer.length()==0) return;
      String text = composeBuffer; composeBuffer=""; lastDigit=0;
      protocolSendChat(text); // handles queue/sent/ack/fail + pushChat
      uiDrawChat();
      return;
    }
    return;
  }

  if (page == PAGE_BROADCAST){
    if (k>='1' && k<='9'){ t9HandleDigit(k, CHAT_MSG_MAX_LEN); uiDrawChat(); return; }
    if (k=='0'){ if (t9Numbers) composeBuffer+='0'; else t9InsertSpace(); uiDrawChat(); return; }
    if (k=='*'){ t9ToggleUpper(); uiDrawChat(); return; }
    if (k=='#'){ t9ToggleNumbers(); uiDrawChat(); return; }
    if (k=='X'){
      if (composeBuffer.length()==0){
        page = PAGE_CONFIG;
        uiDrawConfig();
        return;
      }
      t9Backspace(); uiDrawChat(); return;
    }
    if (k=='E'){
      if (composeBuffer.length()==0) return;
      String text = composeBuffer; composeBuffer="";
      protocolBroadcast(text);
      page=PAGE_CONTACTS; uiDrawContacts();
      return;
    }
    return;
  }
}
