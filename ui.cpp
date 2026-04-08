#include "ui.h"
#include "bitmaps.h"
#include "storage.h"
#include "input.h"
#include "protocol.h"
#include "settings.h"
#include "power.h"
#include "lock.h"
#include "btpair.h"

#ifdef WIRELESS_STICK_V3
SSD1306Wire oled(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_64_32, RST_OLED);
#else
SSD1306Wire oled(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
#endif

Page page = PAGE_CONTACTS;

// ===== Layout constants =====
static const int SB_H        = 13;   // status bar height (12px + 1px separator)
static const int SEP_Y       = 51;   // separator between chat and compose
static const int CHAT_BOTTOM = 39;   // topmost y for last message (text y=39..48, sep at y=51)

// ===== Blink state =====
static uint32_t lastBlink      = 0;
static bool     blinkOn        = true;
static uint32_t lastForcedBlink = 0;

// ===== Invite state (definitions) =====
uint8_t  inviteeId           = 0;
uint32_t inviteCode          = 0;
uint8_t  inviterId           = 0;
char     inviterName[21]     = {0};
uint32_t inviterCodeExpected = 0;
uint8_t  inviterNonce8[8]    = {0};  // nonce received in INV_REQ

extern uint32_t dbg_rxCount;
extern int8_t   dbg_lastRssi;
extern uint8_t  dbg_lastType, dbg_lastFrom, dbg_lastTo, dbg_lastWhy;

// ===== Hardware init =====
void VextON()  { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW);  }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

static void bootSplash(const char* step) {
  oled.clear();
  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.drawString(0, 0,  "LoRaMessenger v2");
  oled.drawString(0, 14, step);
  oled.display();
}

void appInitHardware(){
  VextON(); delay(120);
  Wire.begin(SDA_OLED, SCL_OLED, 400000);
  oled.init();
  oled.displayOn();
  oled.setContrast(255);
  bootSplash("Starting...");
  oled.setContrast(g_settings.oledContrast);
}

void uiBootStep(const char* step) {
  bootSplash(step);
}

void uiEnterBootPage(){
  if (storageDeviceName().length() == 0){
    page = PAGE_NAME;
    storageComposeMut() = "";
    uiDrawNameEntry();
  } else if (lockIsLocked()){
    page = PAGE_LOCK;
    uiDrawLock();
  } else {
    page = PAGE_CONTACTS;
    uiDrawContacts();
  }
}

// ===== UI tick =====
void uiTick(){
  const uint32_t PERIOD = 500;
  uint32_t now = millis();
  if ((now - lastBlink) >= PERIOD){
    lastBlink = now;
    blinkOn   = !blinkOn;
    if (page == PAGE_CHAT || page == PAGE_BROADCAST){
      uiRedrawComposeBand(true);
    } else if (page == PAGE_NAME){
      uiDrawNameEntry();
    } else if (page == PAGE_INVITE_PROMPT){
      uiDrawInvitePrompt();
    } else if (page == PAGE_LOCK){
      uiDrawLock();
    } else if (page == PAGE_PIN_SETUP || page == PAGE_PIN_CHANGE){
      uiDrawPinSetup();
    }
  }
}

// ====== Text helpers ======
static void flush(){ oled.display(); }

static String fitTail(const String& s, int maxPixels, int &w){
  String t = s;
  while (oled.getStringWidth(t) > maxPixels && t.length() > 0) t.remove(0,1);
  w = oled.getStringWidth(t);
  return t;
}

static void wrapLines(const String& text, int maxWidth, std::vector<String>& out){
  out.clear();
  int n = text.length(); String line = ""; int i = 0;
  auto fits = [&](const String& s){ return oled.getStringWidth(s) <= maxWidth; };
  while (i <= n){
    int sp = text.indexOf(' ', i);
    String word; bool last = false;
    if (sp < 0){ word = text.substring(i); last = true; }
    else { word = text.substring(i, sp+1); }
    String cand = line + word;
    if (fits(cand)){ line = cand; }
    else {
      if (line.length() > 0) out.push_back(line);
      String wleft = word;
      while (!fits(wleft) && wleft.length() > 0){
        int lo = 1, hi = wleft.length();
        while (lo < hi){
          int mid = (lo+hi)/2;
          String part = wleft.substring(0, mid);
          if (oled.getStringWidth(part) <= maxWidth) lo = mid+1; else hi = mid;
        }
        int take = max(1, lo-1);
        out.push_back(wleft.substring(0, take));
        wleft.remove(0, take);
      }
      line = wleft;
    }
    if (last) break;
    i = sp + 1;
  }
  if (line.length() > 0) out.push_back(line);
}

static void menuDivider(){
  for (int x = 0; x < 128; x += 3) oled.setPixel(x, SB_H+13);
}

// ====== Status Bar (12px top) ======
void uiDrawStatusBar(){
  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);

  // Lock indicator
  int x = 0;
  if (lockIsLocked()){
    oled.drawString(x, 0, "L ");
    x += oled.getStringWidth("L ");
  }

  // Device name (truncated to 8 chars)
  String nm = storageDeviceName();
  if (nm.length() > 8) nm = nm.substring(0, 8);
  oled.drawString(x, 0, nm);

  // Right side: RSSI + battery + charging
  oled.setTextAlignment(TEXT_ALIGN_RIGHT);
  String right = "";
  if (powerIsCharging()) right += "~";
  right += String(powerBatteryPct()) + "%";
  right += " " + String(dbg_lastRssi) + "dBm";
  oled.drawString(127, 0, right);

  // Separator line
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.drawLine(0, 12, 127, 12);
}

// Relative timestamp helper: returns "Xm" or "Xs" ago
static String relTime(uint32_t ts){
  uint32_t elapsed = (millis() - ts) / 1000;
  if (elapsed < 60)  return String(elapsed) + "s";
  if (elapsed < 3600) return String(elapsed/60) + "m";
  return String(elapsed/3600) + "h";
}

// ====== PAGE_NAME ======
void uiDrawNameEntry(){
  oled.clear();
  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);

  const String& typed = storageCompose();
  int used      = (int)typed.length();
  int remaining = max(0, DEVICE_NAME_MAX_LEN - used);

  oled.drawString(0, 0, "Device name " + String(used) + "/" + String(DEVICE_NAME_MAX_LEN));

  String badges = "";
  if (inputUppercase()) badges += "A";
  if (inputNumbers())   badges += "#";
  if (badges.length() > 0){
    oled.setTextAlignment(TEXT_ALIGN_RIGHT);
    oled.drawString(127, 0, badges);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
  }

  const int baseY = 18;
  int textW = 0;
  String toShow = fitTail(typed, 128, textW);
  oled.drawString(0, baseY, toShow);

  if (blinkOn){
    int caretX = min(textW, 122);
    oled.fillRect(caretX, baseY+8, 6, 2);
  }

  oled.drawString(0, 42, "E=Save  X=Delete  *=Case  #=Num");
  flush();
}

// ====== PAGE_CONTACTS ======
void uiDrawContacts(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);

  int cc  = storageContactCount();
  int sel = storageContactsSel();

  oled.drawString(0, SB_H, "Contacts " + String(cc) + "/10");
  menuDivider();

  if (cc == 0){
    oled.drawString(0, SB_H+14, "(none — E to search)");
    oled.drawString(0, SB_H+28, "X=Config");
  } else {
    int first = 0;
    if (sel >= 3) first = sel - 2;
    for (int i = 0; i < 3; i++){
      int idx = first + i;
      if (idx >= cc) break;
      int y = SB_H + 14 + i*12;
      const Contact& c = storageContactAt(idx);
      bool online = protocolIsOnline(c.id);
      int  unread = protocolUnreadCount(c.id);

      String prefix = (idx == sel) ? ">" : " ";
      String status = online ? "*" : " ";
      String badge  = (unread > 0) ? "(" + String(unread) + ")" : "";
      String rssi   = online ? " " + String(protocolContactRssi(c.id)) : "";

      String line = prefix + status + String(c.name) + badge + rssi;
      oled.drawString(0, y, line);
    }
  }
  flush();
}

// ====== PAGE_SEARCH ======
void uiDrawSearch(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.drawString(0, SB_H, "Searching...");

  int sel   = searchSelGet();
  int first = (sel >= 3) ? sel - 2 : 0;

  for (int row = 0; row < 3; ++row){
    int i = first + row;
    if (i >= g_discCount) break;
    String line = String(g_disc[i].id) + ": " + String(g_disc[i].name) +
                  "(" + String(g_disc[i].rssi) + ")";
    if (i == sel) line = ">" + line; else line = " " + line;
    oled.drawString(0, SB_H + 12 + row*13, line);
  }
  flush();
}

// ====== PAGE_INVITE_CODE ======
void uiDrawInviteCode(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, SB_H, "Share code with peer:");
  char buf[16]; snprintf(buf, sizeof(buf), "%06u", (unsigned)inviteCode);
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(0, SB_H+12, buf);
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, SB_H+30, "To ID: " + String(inviteeId));
  oled.drawString(0, SB_H+42, "X=Cancel");
  flush();
}

void uiShowInviteCode(uint8_t toId, uint32_t code6){
  inviteeId  = toId;
  inviteCode = code6;
}

// ====== PAGE_INVITE_PROMPT ======
void uiDrawInvitePrompt(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, SB_H,    "Invite from:");
  oled.drawString(0, SB_H+10, String(inviterName) + " (" + String(inviterId) + ")");
  oled.drawString(0, SB_H+22, "Enter 6-digit code:");

  const String& typed = storageCompose();
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(0, SB_H+33, typed);
  if (blinkOn){
    int w = oled.getStringWidth(typed);
    oled.fillRect(w, SB_H+43, 6, 2);
  }
  oled.setFont(ArialMT_Plain_10);
  flush();
}

void uiShowInvitePrompt(uint8_t fromId, const char* fromNm, uint32_t code6){
  inviterId           = fromId;
  strlcpy(inviterName, fromNm ? fromNm : "", sizeof(inviterName));
  inviterCodeExpected = code6;
  storageComposeMut() = "";
}

void inviteReset(){
  inviteeId           = 0;
  inviteCode          = 0;
  inviterId           = 0;
  inviterName[0]      = 0;
  inviterCodeExpected = 0;
  memset(inviterNonce8, 0, 8);
  storageComposeMut() = "";
}

bool inviteInProgress(){
  return (inviteeId != 0) || (inviterId != 0);
}

// ====== PAGE_CHAT ======
void uiDrawChat(){
  oled.clear();

  // Custom status bar for chat: show peer name instead of device name
  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  uint8_t currentPeerId = protocolCurrentPeer();
  int     peerIdx  = storageFindContact(currentPeerId);
  String  peerName = (peerIdx >= 0) ? String(storageContactAt(peerIdx).name) : String(currentPeerId);
  bool    peerOnline = protocolIsOnline(currentPeerId);
  String  peerLabel  = (peerOnline ? ">" : " ") + peerName;
  oled.drawString(0, 0, peerLabel);

  oled.setTextAlignment(TEXT_ALIGN_RIGHT);
  String right = "";
  if (powerIsCharging()) right += "~";
  right += String(powerBatteryPct()) + "%";
  right += " " + String(protocolContactRssi(currentPeerId)) + "dBm";
  oled.drawString(127, 0, right);
  oled.drawLine(0, 12, 127, 12);

  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.setColor(WHITE);
  oled.setFont(ArialMT_Plain_10);

  int y        = CHAT_BOTTOM;
  int rowSkip  = protocolScrollOffset();

  for (int idx = protocolChatCount()-1; idx >= 0 && y >= SB_H; --idx){
    ChatMsg m; protocolGetChat(idx, m);
    bool mine = (m.from == protocolDeviceId());

    String tag = "";
    if (mine){
      if (m.status == ST_FAILED)         tag = " /x";
      else if (m.status == ST_DELIVERED) tag = " //";
      else if (m.status == ST_SENT)      tag = " /";
    }
    if (m.priority == PRIORITY_HIGH) tag += "[!]";
    if (m.priority == PRIORITY_SOS)  tag += "[S]";

    // Relative timestamp suffix
    String ts = " " + relTime(m.timestamp);

    std::vector<String> lines;
    wrapLines(m.text + tag + ts, 120, lines);
    int rows = (int)lines.size();

    if (rowSkip >= rows){ rowSkip -= rows; continue; }
    int startLine = rows - 1 - rowSkip;
    rowSkip = 0;

    for (int li = startLine; li >= 0 && y >= SB_H; --li){
      int w = oled.getStringWidth(lines[li]);
      int x = mine ? (128 - w) : 0;
      oled.drawString(x, y, lines[li]);
      y -= 10;
    }
  }

  // Separator
  oled.drawLine(0, SEP_Y, 127, SEP_Y);

  if (protocolScrollOffset() > 0 && blinkOn){
    oled.setTextAlignment(TEXT_ALIGN_CENTER);
    oled.drawString(64, SEP_Y-10, "v");
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
  }

  uiRedrawComposeBand(false);
  flush();
}

// Compose band (bottom of chat / broadcast)
void uiRedrawComposeBand(bool push){
  const int baseY           = SEP_Y + 2;
  const int SEND_ICON_RESERVE = 24;
  const int COMPOSE_MAX_PX  = 128 - SEND_ICON_RESERVE;

  oled.setColor(BLACK);
  oled.fillRect(0, SEP_Y+1, 128, 64-(SEP_Y+1));
  oled.setColor(WHITE);
  oled.drawLine(0, SEP_Y, 127, SEP_Y);

  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.setFont(ArialMT_Plain_10);
  int textW = 0;
  String toShow = fitTail(storageCompose(), COMPOSE_MAX_PX, textW);
  oled.drawString(0, baseY, toShow);

  int caretX = min(textW, COMPOSE_MAX_PX-2);
  if (blinkOn){
    oled.setColor(WHITE);
    oled.fillRect(caretX, baseY+8, 6, 2);
  } else {
    oled.setColor(BLACK);
    oled.fillRect(caretX, baseY+8, 6, 2);
    oled.setColor(WHITE);
  }

  // Priority indicator
  uint8_t pri = g_settings.defaultPriority;
  if (pri == 1){ oled.drawString(0, baseY, "[!]"); }
  if (pri == 2){ oled.drawString(0, baseY, "[S]"); }

  // Send icon (triangle)
  oled.drawLine(112, baseY,   122, baseY+5);
  oled.drawLine(122, baseY+5, 112, baseY+10);
  oled.drawLine(112, baseY+10,112, baseY);

  // Mode badges
  String mode = "";
  if (inputUppercase()) mode += "A";
  if (inputNumbers())   mode += "#";
  if (mode.length() > 0){
    oled.setTextAlignment(TEXT_ALIGN_RIGHT);
    oled.drawString(110, baseY, mode);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
  }

  if (push) flush();
}

// ====== PAGE_CONFIG (expanded to 6 items) ======
void uiDrawConfig(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.drawString(0, SB_H, "Config");
  menuDivider();

  int sel = configSelGet();

  const char* items[] = {
    "Notifications",
    "Power",
    "Security",
    "Messages",
    "Broadcast",
    "Contacts",
    "System",
    "Pair via BT"
  };
  const int N = 8;
  int first = 0;
  if (sel >= 3) first = sel - 2;

  for (int i = 0; i < 3; i++){
    int idx = first + i;
    if (idx >= N) break;
    String line = String((idx == sel) ? ">" : " ") + items[idx];
    oled.drawString(0, SB_H + 14 + i*12, line);
  }
  flush();
}

// ====== PAGE_CONFIRM_RESET ======
void uiDrawConfirmReset(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, SB_H,    "Factory reset?");
  oled.drawString(0, SB_H+12, "Erases name + contacts.");


  int sel = confirmSelGet();
  const char* opts[2] = {"No", "Yes"};
  for (int i = 0; i < 2; i++){
    String line = String((i == sel) ? ">" : " ") + opts[i];
    oled.drawString(0, SB_H+28 + i*12, line);
  }
  flush();
}

// ====== PAGE_LOCK ======
void uiDrawLock(){
  oled.clear();
  oled.setFont(ArialMT_Plain_10);

  oled.drawString(0, 0, "=== LOCKED ===");
  oled.drawString(0, 14, "Enter PIN:");

  const String& typed = inputLockPin();
  String stars = "";
  for (size_t i = 0; i < typed.length(); i++) stars += "*";
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(0, 26, stars);
  if (blinkOn){
    int w = oled.getStringWidth(stars);
    oled.fillRect(w, 36, 6, 2);
  }
  oled.setFont(ArialMT_Plain_10);

  // Lockout message
  uint32_t lo = lockLockoutUntil();
  if (lo > millis()){
    uint32_t remaining = (lo - millis()) / 1000;
    oled.drawString(0, 46, "Wait " + String(remaining) + "s...");
  } else if (lockFailCount() > 0){
    oled.drawString(0, 46, "Wrong PIN (" + String(lockFailCount()) + "x)");
  }

  oled.drawString(0, 55, "E=Unlock  X=Delete");
  flush();
}

// ====== PAGE_SLEEP_STATUS ======
void uiDrawSleepStatus(){
  oled.clear();
  oled.setFont(ArialMT_Plain_10);

  oled.drawString(0, 0, storageDeviceName());

  uint8_t pct = powerBatteryPct();
  String  batt = String(pct) + "%";
  if (powerIsCharging()) batt += " ~CHG";
  oled.drawString(0, 14, "Battery: " + batt);

  oled.drawString(0, 26, "RSSI: " + String(dbg_lastRssi) + " dBm");

  // Count pending notifications
  int totalUnread = 0;
  int cc = storageContactCount();
  for (int i = 0; i < cc; i++){
    totalUnread += protocolUnreadCount(storageContactAt(i).id);
  }
  if (totalUnread > 0){
    oled.drawString(0, 38, String(totalUnread) + " unread message(s)");
  }

  oled.drawString(0, 52, "Any key = wake / unlock");
  flush();
}

// ====== PAGE_SOS ======
void uiDrawSOS(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_CENTER);

  if (blinkOn){
    oled.drawString(64, SB_H, "*** SOS ***");
  } else {
    oled.drawString(64, SB_H, "           ");
  }
  oled.setTextAlignment(TEXT_ALIGN_LEFT);



  oled.drawString(0, SB_H+14, "Sent: " + String(g_sosSentCount));
  oled.drawString(0, SB_H+26, "To all contacts");

  // Show which contacts ACK'd
  int cc = storageContactCount();
  for (int i = 0; i < cc && i < 2; i++){
    const Contact& c = storageContactAt(i);
    bool online = protocolIsOnline(c.id);
    oled.drawString(0, SB_H+38+i*10, String(c.name) + (online ? " OK" : " ..."));
  }

  oled.drawString(0, 55, "Hold any key 3s = Cancel");
  flush();
}

// ====== PAGE_SETTINGS_NOTIFY ======
void uiDrawSettingsNotify(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, SB_H, "Notifications");
  menuDivider();

  uint8_t mask = g_settings.notifyMask;
  int sel = settingsNotifySelGet();

  const char* items[] = {"LED", "Buzz", "Vibration", "Wake screen"};
  bool vals[] = {
    (bool)(mask & 0x01),
    (bool)(mask & 0x02),
    (bool)(mask & 0x04),
    (bool)(mask & 0x08)
  };
  int first = (sel >= 3) ? sel - 2 : 0;
  for (int i = 0; i < 3; i++){
    int idx = first + i;
    if (idx >= 4) break;
    String line = String((idx == sel) ? ">" : " ");
    line += String(vals[idx] ? "[x] " : "[ ] ");
    line += items[idx];
    oled.drawString(0, SB_H+14+i*13, line);
  }
  flush();
}

// ====== PAGE_SETTINGS_POWER ======
void uiDrawSettingsPower(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, SB_H, "Power");
  menuDivider();

  int sel = settingsPowerSelGet();

  // Screen timeout options: 0=never,10,30,60,300
  String tout;
  switch(g_settings.screenTimeoutSec){
    case 0:   tout = "Never"; break;
    case 10:  tout = "10s";   break;
    case 30:  tout = "30s";   break;
    case 60:  tout = "1min";  break;
    case 300: tout = "5min";  break;
    default:  tout = String(g_settings.screenTimeoutSec) + "s";
  }
  // Poll interval: 500, 1000, 5000
  String poll;
  switch(g_settings.sleepPollMs){
    case 500:  poll = "500ms"; break;
    case 1000: poll = "1s";    break;
    case 5000: poll = "5s";    break;
    default:   poll = String(g_settings.sleepPollMs) + "ms";
  }
  // Contrast -> brightness %
  int bright = (g_settings.oledContrast * 100) / 255;

  const char* labels[] = {"Screen timeout", "Sleep poll", "Brightness"};
  String vals[] = {tout, poll, String(bright) + "%"};
  for (int i = 0; i < 3; i++){
    String line = String((i == sel) ? ">" : " ") + labels[i] + ": " + vals[i];
    oled.drawString(0, SB_H+14+i*13, line);
  }
  flush();
}

// ====== PAGE_SETTINGS_SECURITY ======
void uiDrawSettingsSecurity(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, SB_H, "Security");
  menuDivider();

  int sel = settingsSecSelGet();

  String lock_str  = g_settings.lockEnabled ? "[x] Lock" : "[ ] Lock";
  String tout_str;
  switch(g_settings.lockTimeoutMin){
    case 0:  tout_str = "Immediate"; break;
    case 1:  tout_str = "1 min";     break;
    case 5:  tout_str = "5 min";     break;
    case 15: tout_str = "15 min";    break;
    case 60: tout_str = "60 min";    break;
    default: tout_str = String(g_settings.lockTimeoutMin) + "m";
  }

  const char* items[] = {"Lock on/off", "Lock timeout", "Set PIN"};
  String vals[] = {lock_str, tout_str, lockHasPinSet() ? "change" : "setup"};
  for (int i = 0; i < 3; i++){
    String line = String((i == sel) ? ">" : " ") + items[i];
    if (i == 0) line = String((i == sel) ? ">" : " ") + vals[0];
    else        line += ": " + vals[i];
    oled.drawString(0, SB_H+14+i*13, line);
  }
  flush();
}

// ====== PAGE_SETTINGS_MESSAGES ======
void uiDrawSettingsMessages(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, SB_H, "Messages");
  menuDivider();

  int sel = settingsMsgSelGet();

  const char* prioLabel[] = {"Normal", "High", "SOS"};
  String prio = prioLabel[min((int)g_settings.defaultPriority, 2)];

  String hist;
  switch(g_settings.historyDays){
    case 0:  hist = "Off";    break;
    case 1:  hist = "1 day";  break;
    case 7:  hist = "7 days"; break;
    case 30: hist = "30 days";break;
    default: hist = String(g_settings.historyDays) + "d";
  }

  const char* labels[] = {"Default priority", "History"};
  String vals[] = {prio, hist};
  for (int i = 0; i < 2; i++){
    String line = String((i == sel) ? ">" : " ") + labels[i] + ": " + vals[i];
    oled.drawString(0, SB_H+14+i*13, line);
  }
  flush();
}

// ====== PAGE_SETTINGS_SYSTEM ======
void uiDrawSettingsSystem(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, SB_H, "System");
  menuDivider();

  int sel = settingsSysSelGet();

  String devId  = "ID: " + String(protocolDeviceId());
  String freq   = "Freq: 915MHz";
  const char* items[] = {"Device ID", "LoRa Freq", "Factory Reset"};
  String vals[] = {String(protocolDeviceId()), "915MHz", ""};

  for (int i = 0; i < 3; i++){
    String line = String((i == sel) ? ">" : " ") + items[i];
    if (i < 2) line += ": " + vals[i];
    oled.drawString(0, SB_H+14+i*13, line);
  }
  flush();
}

// ====== PAGE_CONTACT_DETAIL ======
void uiDrawContactDetail(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);

  int sel   = storageContactsSel();
  int cc    = storageContactCount();
  if (sel < 0 || sel >= cc){ page = PAGE_CONTACTS; uiDrawContacts(); return; }

  const Contact& c = storageContactAt(sel);
  oled.drawString(0, SB_H, String(c.name) + " (" + String(c.id) + ")");
  menuDivider();
  oled.drawString(0, SB_H+14, protocolIsOnline(c.id) ? "* Online" : "  Offline");

  int dsel = contactDetailSelGet();
  const char* opts[] = {"Chat", "Delete"};
  for (int i = 0; i < 2; i++){
    oled.drawString(0, SB_H+30+i*12, String((i == dsel) ? ">" : " ") + opts[i]);
  }
  flush();
}

// ====== PAGE_PIN_SETUP / PAGE_PIN_CHANGE ======
void uiDrawPinSetup(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  bool isChange = (page == PAGE_PIN_CHANGE);
  oled.drawString(0, SB_H,    isChange ? "Change PIN" : "Set PIN");
  oled.drawString(0, SB_H+12, "Enter 4-8 digits:");

  const String& typed = inputLockPin();
  String stars = "";
  for (size_t i = 0; i < typed.length(); i++) stars += "*";
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(0, SB_H+24, stars);
  if (blinkOn){
    int w = oled.getStringWidth(stars);
    oled.fillRect(w, SB_H+34, 6, 2);
  }
  oled.setFont(ArialMT_Plain_10);
  flush();
}

// ====== PAGE_BT_PAIR ======
void uiDrawBTPair(){
  oled.clear();
  uiDrawStatusBar();

  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.drawString(0, SB_H, "Pair via BT");
  menuDivider();

  BTPairState st = btpairState();

  if (st == BTPAIR_IDLE){
    extern int btpairSelGet();
    int sel = btpairSelGet();
    oled.drawString(0, SB_H+14, String(sel == 0 ? ">" : " ") + "Host (share key)");
    oled.drawString(0, SB_H+26, String(sel == 1 ? ">" : " ") + "Join (scan)");
    oled.drawString(0, SB_H+40, "U/D=Sel  E=Start  X=Back");
  } else if (st == BTPAIR_DONE_OK){
    oled.drawString(0, SB_H+14, "Done!");
    oled.drawString(0, SB_H+26, btpairStatus());
    oled.drawString(0, SB_H+40, "X=Back");
  } else if (st == BTPAIR_DONE_FAIL){
    oled.drawString(0, SB_H+14, "Failed.");
    oled.drawString(0, SB_H+26, btpairStatus());
    oled.drawString(0, SB_H+40, "X=Back");
  } else {
    String mode = (st == BTPAIR_HOST) ? "[Host]" : "[Join]";
    oled.drawString(0, SB_H+14, mode);
    oled.drawString(0, SB_H+26, btpairStatus());
    oled.drawString(0, SB_H+40, "X=Cancel");
  }
  flush();
}

// ====== Helpers ======
void uiForceBlinkRestart(){
  uint32_t now = millis();
  if (now - lastForcedBlink < 120) return;
  lastForcedBlink = now;
  lastBlink = now;
  blinkOn   = true;
}

void uiToast(const String& m){
  oled.fillRect(0, 52, 128, 12);
  oled.setColor(BLACK);
  oled.drawString(2, 52, m);
  oled.setColor(WHITE);
  flush();
}

void uiDebugBlinkOverlay(){
  const int x = 0, y = 0, sz = 4;
  if (blinkOn){
    oled.setColor(WHITE); oled.fillRect(x, y, sz, sz);
  } else {
    oled.setColor(BLACK); oled.fillRect(x, y, sz, sz);
    oled.setColor(WHITE);
  }
  flush();
}




