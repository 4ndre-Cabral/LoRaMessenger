
#include "ui.h"
#include "bitmaps.h"
#include "storage.h"
#include "input.h"
#include "protocol.h"

#ifdef WIRELESS_STICK_V3
SSD1306Wire oled(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_64_32, RST_OLED);
#else
SSD1306Wire oled(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
#endif

Page page = PAGE_CONTACTS;

// Layout constants for chat
static const int SEP_Y       = 50;
static const int CHAT_BOTTOM = SEP_Y - 12;

// Blink state
static uint32_t lastBlink = 0;
static bool blinkOn = true;

static uint32_t lastForcedBlink = 0;

// ===== Invite state (definitions) =====
uint8_t  inviteeId = 0;
uint32_t inviteCode = 0;

uint8_t  inviterId = 0;
char     inviterName[21] = {0};
uint32_t inviterCodeExpected = 0;

extern uint32_t dbg_rxCount;
extern int8_t   dbg_lastRssi;
extern uint8_t  dbg_lastType, dbg_lastFrom, dbg_lastTo, dbg_lastWhy;

void VextON()  { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW);  }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

void appInitHardware(){
  VextON(); delay(120);
  Wire.begin(SDA_OLED, SCL_OLED, 400000);
  oled.init();
  oled.displayOn();
  oled.setContrast(255);

  // Smoke test: draw something now so you know it’s alive
  oled.clear();
  oled.drawString(0, 0, "OLED up!");
  oled.drawRect(0, 12, 128, 16);
  oled.display();
}

void uiEnterBootPage(){
  if (storageDeviceName().length() == 0) {
    page = PAGE_NAME;
    storageComposeMut() = ""; // start empty
    uiDrawNameEntry();
  } else {
    page = PAGE_CONTACTS;
    uiDrawContacts();
  }
}

void uiTick(){
  const uint32_t PERIOD = 500;
  uint32_t now = millis();
  if ((now - lastBlink) >= PERIOD) {
    lastBlink = now;
    blinkOn = !blinkOn;

    if (page == PAGE_CHAT || page == PAGE_BROADCAST) {
      // only repaint the bottom band where the caret lives
      uiRedrawComposeBand(true);
    } else if (page == PAGE_NAME) {
      uiDrawNameEntry();
    } else if (page == PAGE_INVITE_PROMPT) {
      uiDrawInvitePrompt();
    }
  }
}

// ====== Small text helpers ======
static void flush(){ oled.display(); }

// Measure-fitted compose tail
static String fitTail(const String& s, int maxPixels, int &w){
  String t = s;
  while (oled.getStringWidth(t) > maxPixels && t.length()>0) t.remove(0,1);
  w = oled.getStringWidth(t);
  return t;
}

// Simple word wrap (first-line prefix support disabled here for brevity)
static void wrapLines(const String& text, int maxWidth, std::vector<String> &out){
  out.clear();
  int n = text.length(); String line=""; int i=0;
  auto fits = [&](const String& s){ return oled.getStringWidth(s) <= maxWidth; };
  while (i<=n){
    int sp = text.indexOf(' ', i);
    String word; bool last=false;
    if (sp<0){ word=text.substring(i); last=true; } else { word=text.substring(i, sp+1); }
    String cand = line + word;
    if (fits(cand)) { line = cand; }
    else {
      if (line.length()>0) out.push_back(line);
      // hard split long word
      String wleft = word;
      while (!fits(wleft) && wleft.length()>0){
        int lo=1, hi=wleft.length();
        while (lo<hi){
          int mid=(lo+hi)/2;
          String part=wleft.substring(0,mid);
          if (oled.getStringWidth(part)<=maxWidth) lo=mid+1; else hi=mid;
        }
        int take = max(1, lo-1);
        out.push_back(wleft.substring(0,take));
        wleft.remove(0,take);
      }
      line = wleft;
    }
    if (last) break;
    i = sp + 1;
  }
  if (line.length()>0) out.push_back(line);
}

// ====== Pages ======
void uiDrawNameEntry(){
  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.setFont(ArialMT_Plain_10);

  // Header with remaining counter (remaining/total)
  const String &typed = storageCompose();   // current name buffer
  int remaining = max(0, DEVICE_NAME_MAX_LEN - (int)typed.length());
  int used = DEVICE_NAME_MAX_LEN - remaining;

  String header = "Set device name  " + String(used) + "/" + String(DEVICE_NAME_MAX_LEN);
  oled.drawString(0, 0, header);

  // Show mode badges (right side): A for Capital Case, # for Numbers
  String badges = "";
  if (inputUppercase()) badges += "A";
  if (inputNumbers())   badges += "#";
  if (badges.length() > 0) {
    oled.setTextAlignment(TEXT_ALIGN_RIGHT);
    oled.drawString(127, 0, badges);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
  }

  // Compose line with tail-fit + blinking caret
  const int baseY = 16;
  const int MAX_PX = 128;   // full width for name input

  int textW = 0;
  String toShow = fitTail(typed, MAX_PX, textW);   // your existing tail fitter
  oled.drawString(0, baseY, toShow);

  // robust blinking caret (block under baseline)
  if (blinkOn) {
    int caretX = min(textW, MAX_PX - 2);
    oled.fillRect(caretX, baseY + 8, 6, 2);
  }

  oled.drawString(0, 40, "Enter=Save    ESC=Delete");
  oled.display();
}


void uiDrawContacts(){
  oled.clear();
  oled.drawString(0,0,"Contacts");
  int cc = storageContactCount();
  if (cc==0){
    oled.drawString(0,16,"(none)");
    oled.drawString(0,28,"Enter: Search nearby");
    oled.drawString(0,40,"ESC: Broadcast");
  } else {
    int sel = storageContactsSel();
    for (int i=0;i<cc && i<5;i++){
      int y = 12 + i*10;
      const Contact& c = storageContactAt(i);
      String line = String((i==sel)?"> ":"  ") + String(c.name) + " (" + String(c.id) + ")";
      oled.drawString(0,y,line);
    }
    oled.drawString(0,58,"Enter: Chat   ESC: Broadcast");
  }
  flush();
}

void uiDrawSearch(){
  oled.clear();
  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.drawString(0,0,"Searching nearby...");

  extern DiscEntry g_disc[];
  extern int g_discCount;
  extern int searchSelGet();
  int sel = searchSelGet();

  // Simple scroll if more than 4 rows
  int first = 0;
  if (sel >= 4) first = sel - 3;

  for (int row=0; row<4; ++row){
    int i = first + row;
    if (i >= g_discCount) break;
    String line = String(g_disc[i].id) + ": " + String(g_disc[i].name) +
                  " (" + String(g_disc[i].rssi) + "dBm)";
    if (i == sel) line = "> " + line; else line = "  " + line;
    oled.drawString(0, 14 + row*12, line);
  }

  oled.drawString(0, 56, "U/D=Select  Enter=Invite  X=Back");
  oled.display();
}

void uiDrawInviteCode(){
  oled.clear();
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0,0,"Share this code with peer:");
  char buf[16]; snprintf(buf,sizeof(buf), "%06u", (unsigned)inviteCode);
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(0,18, buf);
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0,40, String("To ID: ") + String(inviteeId));
  oled.drawString(0,56,"Waiting for accept...  X=Back");
  oled.display();
}

void uiShowInviteCode(uint8_t toId, uint32_t code6){
  inviteeId = toId;
  inviteCode = code6;
}


void uiDrawInvitePrompt(){
  oled.clear();
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0,0,"Invite received");
  oled.drawString(0,12, String("From: ") + String(inviterName) + " (" + String(inviterId) + ")");
  oled.drawString(0,24,"Enter 6-digit code:");

  const String &typed = storageCompose();
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(0,38, typed);
  if (blinkOn) {
    int w = oled.getStringWidth(typed);
    oled.fillRect(w, 38+10, 6, 2); // block caret
  }
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0,56,"Enter=OK  X=Back");
  oled.display();
}

void inviteReset(){
  inviteeId = 0;
  inviteCode = 0;

  inviterId = 0;
  inviterName[0] = 0;
  inviterCodeExpected = 0;

  storageComposeMut() = "";   // clear any typed code
}

bool inviteInProgress(){
  return (inviteeId != 0) || (inviterId != 0);
}

void uiShowInvitePrompt(uint8_t fromId, const char* fromNm, uint32_t code6){
  inviterId = fromId;
  strlcpy(inviterName, (fromNm?fromNm:""), sizeof(inviterName));
  inviterCodeExpected = code6;
  storageComposeMut() = "";   // clear 6-digit input buffer
}

void uiDrawChat(){
  oled.clear();
  oled.setColor(WHITE);
  oled.setFont(ArialMT_Plain_10);
  int y = CHAT_BOTTOM;
  int rowSkip = protocolScrollOffset();

  // render messages from newest to oldest
  for (int idx = protocolChatCount()-1; idx>=0 && y >= 0; --idx){
    ChatMsg m; protocolGetChat(idx, m);
    bool mine = (m.from == protocolDeviceId());
    String tag="";
    if (mine){
      if (m.status==ST_FAILED) tag=" /x";
      else if (m.status==ST_DELIVERED) tag=" //";
    }
    std::vector<String> lines;
    wrapLines(m.text + tag, 128, lines);
    int rows = (int)lines.size();
    if (rowSkip >= rows) { rowSkip -= rows; continue; }
    int startLine = rows - 1 - rowSkip;
    rowSkip = 0;
    for (int li=startLine; li>=0 && y>=0; --li){
      int w = oled.getStringWidth(lines[li]);
      int x = mine ? (128 - w) : 0;
      oled.drawString(x, y, lines[li]);
      y -= 10;
    }
  }

  // separator
  oled.drawLine(0, SEP_Y, 127, SEP_Y);

  // show 'v' blinking if scrolled up
  if (protocolScrollOffset() > 0 && blinkOn){
    oled.setTextAlignment(TEXT_ALIGN_CENTER);
    oled.drawString(64, SEP_Y - 10, "v");
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
  }

  // // compose line with caret + send icon
  // oled.setTextAlignment(TEXT_ALIGN_LEFT);
  // oled.setFont(ArialMT_Plain_10);

  // const int baseY = SEP_Y + 2;
  // const int SEND_ICON_RESERVE = 24;
  // const int COMPOSE_MAX_PX = 128 - SEND_ICON_RESERVE;

  // int textW = 0;
  // String toShow = fitTail(storageCompose(), COMPOSE_MAX_PX, textW);

  // // draw text
  // oled.drawString(0, baseY, toShow);

  // // robust blinking caret as a small block *below* the text
  // int caretX = textW;
  // if (caretX > COMPOSE_MAX_PX - 2) caretX = COMPOSE_MAX_PX - 2;  // clamp before send icon
  // // For ArialMT_Plain_10, baseline thickness ~2px at y+8..9 looks good
  // if (blinkOn) {
  //   oled.fillRect(caretX, baseY + 8, 6, 2);   // width 6px, height 2px
  // } else {
  //   // optional hollow caret when "off", keeps visual stability
  //   oled.drawRect(caretX, baseY + 8, 6, 2);
  // }
  // // send icon
  // oled.drawLine(112, baseY,   122, baseY+5);
  // oled.drawLine(122, baseY+5, 112, baseY+10);
  // oled.drawLine(112, baseY+10,112, baseY);

  // // tiny indicators
  // String mode="";
  // if (inputUppercase()) mode+="A";
  // if (inputNumbers())   mode+="#";
  // if (mode.length()>0){
  //   oled.setTextAlignment(TEXT_ALIGN_RIGHT);
  //   oled.drawString(110, SEP_Y+2, mode);
  //   oled.setTextAlignment(TEXT_ALIGN_LEFT);
  // }

  // flush();

  uiRedrawComposeBand(false);
  flush();
}

// Repaint only the compose band (bottom area) so the caret can blink
void uiRedrawComposeBand(bool push = true) {
  const int SEP_Y = 50;
  const int baseY = SEP_Y + 2;
  const int SEND_ICON_RESERVE = 24;
  const int COMPOSE_MAX_PX    = 128 - SEND_ICON_RESERVE;

  // 1) CLEAR band to BLACK
  oled.setColor(BLACK);
  oled.fillRect(0, SEP_Y + 1, 128, 64 - (SEP_Y + 1));

  // 2) Draw all UI in WHITE
  oled.setColor(WHITE);

  // separator
  oled.drawLine(0, SEP_Y, 127, SEP_Y);

  // compose text (tail fit)
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.setFont(ArialMT_Plain_10);
  int textW = 0;
  String toShow = fitTail(storageCompose(), COMPOSE_MAX_PX, textW);
  oled.drawString(0, baseY, toShow);

  // caret (block when on, hollow when off)
  int caretX = textW;
  if (caretX > COMPOSE_MAX_PX - 2) caretX = COMPOSE_MAX_PX - 2;
  if (blinkOn) {
    oled.fillRect(caretX, baseY + 8, 6, 2);
  } else {
    oled.drawRect(caretX, baseY + 8, 6, 2);
  }

  // send icon
  oled.drawLine(112, baseY,   122, baseY+5);
  oled.drawLine(122, baseY+5, 112, baseY+10);
  oled.drawLine(112, baseY+10,112, baseY);

  // small mode badges (A/#) just left of the send icon
  String mode="";
  if (inputUppercase()) mode += "A";
  if (inputNumbers())   mode += "#";
  if (mode.length()>0) {
    oled.setTextAlignment(TEXT_ALIGN_RIGHT);
    oled.drawString(110, baseY, mode);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
  }

  if (push) oled.display();
}

void uiDrawConfig(){
  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.setFont(ArialMT_Plain_10);

  oled.drawString(0, 0, "Config");

  extern int configSelGet();
  int sel = configSelGet();

  const char* items[3] = {"Broadcast", "Contact List", "Factory reset"};
  for (int i=0; i<3; ++i){
    String line = String((i==sel)?"> ":"  ") + items[i];
    oled.drawString(0, 14 + i*12, line);
  }

  oled.drawString(0, 56, "U/D=Move  Enter=Select  ESC=Back");
  oled.display();
}

void uiDrawConfirmReset(){
  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.setFont(ArialMT_Plain_10);

  oled.drawString(0, 0, "Factory reset?");
  oled.drawString(0, 14, "This erases device name");
  oled.drawString(0, 26, "and all contacts.");

  extern int confirmSelGet();
  int sel = confirmSelGet();

  const char* opts[2] = {"No", "Yes"};
  // Put choices at the bottom for a consistent look
  for (int i=0; i<2; ++i){
    String line = String((i==sel)?"> ":"  ") + opts[i];
    oled.drawString(0, 46 + i*10, line);
  }

  oled.display();
}

void uiForceBlinkRestart(){
   // prevent spam resets faster than every 120ms
  uint32_t now = millis();
  if (now - lastForcedBlink < 120) return;
  lastForcedBlink = now;

  lastBlink = millis();    // so next uiTick waits a full period
  blinkOn   = true;        // caret immediately visible on next draw
}

void uiToast(const String& m){
  oled.fillRect(0,52,128,12);
  oled.setColor(BLACK);
  oled.drawString(2,52,m);
  oled.setColor(WHITE);
  flush();
}

void uiDrawRadioDebugOverlay(){
  // draw a tiny single-line status at the very bottom row
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.setFont(ArialMT_Plain_10);
  char line[40];
  // why: 0 ok, 1 short, 2 crc, 3 dst
  snprintf(line, sizeof(line), "RX:%lu T:%u F:%u->%u L? RSSI:%d W:%u",
           (unsigned long)dbg_rxCount, dbg_lastType, dbg_lastFrom, dbg_lastTo, dbg_lastRssi, dbg_lastWhy);
  oled.drawString(0, 54, line); // adjust Y if your footer uses 56
}
