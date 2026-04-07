#include "protocol.h"
#include <SPI.h>
#if __has_include("lora/LoRa.h")
  #include "lora/LoRa.h"
#else
  #include <LoRa.h>
#endif
#include "ui.h"
#include "crypto.h"
#include "buzz.h"
#include "vib.h"
#include "input.h"

// ===== LoRa pins / radio config (Heltec WiFi LoRa 32 V2) =====
#define LORA_SCK   5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS   18
#define LORA_RST  14
#define LORA_DIO0 26
static const long    LORA_BAND       = 915E6;
static const uint8_t LORA_POWER_DBM  = 14;

// ===== Device ID (manual for now; set per board) =====
#ifndef DEVICE_ID
#define DEVICE_ID 1
#endif
uint8_t protocolDeviceId(){ return DEVICE_ID; }

// ===== Packet =====
struct Packet {
  uint8_t  sender;
  uint8_t  receiver;
  uint8_t  type;
  uint16_t seq;
  uint8_t  len;
  char     body[160];
  uint8_t  crc;
} __attribute__((packed));

static uint8_t crc8(const uint8_t* data, size_t len){
  uint8_t c=0;
  for (size_t i=0;i<len;i++){
    c ^= data[i];
    for (int b=0;b<8;b++) c = (c & 0x80) ? (uint8_t)((c<<1)^0x07) : (uint8_t)(c<<1);
  }
  return c;
}

// ===== Discovery cache (for Search page) =====
DiscEntry g_disc[MAX_DISC];
int       g_discCount = 0;

// ===== Debug counters (referenced from UI) =====
uint32_t dbg_rxCount = 0;
int8_t   dbg_lastRssi = 0;
uint8_t  dbg_lastType = 0, dbg_lastFrom = 0, dbg_lastTo = 0, dbg_lastWhy = 0;  // 0 ok, 1 short, 2 crc, 3 dst

// ===== Chat storage =====
static const int MAX_MSGS = 64;
static ChatMsg chatBuf[MAX_MSGS];
static int chatCount = 0;
static int scrollOffset = 0;
static uint16_t nextSeq = 1;

int  protocolChatCount(){ return chatCount; }
void protocolGetChat(int idx, ChatMsg& out){ out = chatBuf[idx]; }
int  protocolScrollOffset(){ return scrollOffset; }
void protocolScroll(int delta){ if (delta>0) scrollOffset += 1; else if (scrollOffset>0) scrollOffset -= 1; }

static void pushChat(uint8_t from, const String& text, MsgStatus st, uint16_t seq=0){
  if (chatCount < MAX_MSGS) chatBuf[chatCount++] = {from,text,st,seq};
  else {
    for (int i=1;i<MAX_MSGS;i++) chatBuf[i-1]=chatBuf[i];
    chatBuf[MAX_MSGS-1]={from,text,st,seq};
  }
}

void protocolSetChatStatus(int idx, MsgStatus st){
  if (idx < 0 || idx >= chatCount) return;
  chatBuf[idx].status = st;
}

// ===== Nearby (legacy helpers; simple capped list) =====
static NearbyItem nearby[10];
static int nearbyCount = 0;
static int nearbySel = 0;

int  protocolNearbyCount(){ return nearbyCount; }
NearbyItem protocolNearbyAt(int i){ return nearby[i]; }
void protocolNearbyClear(){ nearbyCount=0; nearbySel=0; g_discCount=0; }
void protocolNearbyMoveSel(int delta){
  if (nearbyCount==0){ nearbySel=0; return; }
  nearbySel += delta;
  if (nearbySel<0) nearbySel=0; if (nearbySel>=nearbyCount) nearbySel=nearbyCount-1;
}
int  protocolNearbySel(){ return nearbySel; }
static void addNearby(uint8_t id, const char* nm){
  if (id==DEVICE_ID) return;
  if (storageFindContact(id)>=0) return;
  for (int i=0;i<nearbyCount;i++) if (nearby[i].id==id) return;
  if (nearbyCount<10){
    nearby[nearbyCount].id=id;
    memset(nearby[nearbyCount].name,0,16);
    strncpy(nearby[nearbyCount].name,nm,15);
    nearbyCount++;
  }
}

// ===== Invite state kept in UI (we reference externs) =====
extern uint8_t  inviteeId;
extern uint32_t inviteCode;

extern uint8_t  inviterId;
extern char     inviterName[21];
extern uint32_t inviterCodeExpected;

extern void uiShowInvitePrompt(uint8_t fromId, const char* fromNm, uint32_t code6);
extern void uiShowInviteCode(uint8_t toId, uint32_t code6);
extern void inviteReset();

// ===== Current chat peer =====
static uint8_t currentPeerId = 0;
void protocolEnterChat(uint8_t peerId){ currentPeerId = peerId; scrollOffset=0; }

// ===== Radio helpers =====
static bool sendRaw(const Packet& p){
  Packet q=p; q.crc=0; q.crc=crc8((const uint8_t*)&q, sizeof(q)-1);
  LoRa.beginPacket();
  LoRa.write((const uint8_t*)&q, sizeof(q));
  bool ok = (LoRa.endPacket(true)==1);
  LoRa.receive();
  return ok;
}

void protocolSendDiscReq(){
  Packet p{}; 
  p.sender   = DEVICE_ID;
  p.receiver = BROADCAST_ID;
  p.type     = TYPE_DISC_REQ;
  p.seq      = 0;
  String nm  = storageDeviceName();
  p.len = (uint8_t)min((size_t)20, nm.length());
  memset(p.body, 0, sizeof(p.body));
  nm.substring(0,p.len).toCharArray(p.body, p.len+1);
  sendRaw(p);
}

static void sendDiscRsp(uint8_t to){
  Packet p{}; 
  p.sender   = DEVICE_ID;
  p.receiver = to;
  p.type     = TYPE_DISC_RSP;
  p.seq      = 0;
  String nm  = storageDeviceName();
  p.len = (uint8_t)min((size_t)20, nm.length());
  memset(p.body, 0, sizeof(p.body));
  nm.substring(0,p.len).toCharArray(p.body, p.len+1);
  sendRaw(p);
}

static inline void putU32BE(uint8_t* b, uint32_t v){
  b[0]=uint8_t(v>>24); b[1]=uint8_t(v>>16); b[2]=uint8_t(v>>8); b[3]=uint8_t(v);
}
static inline uint32_t getU32BE(const uint8_t* b){
  return (uint32_t)b[0]<<24 | (uint32_t)b[1]<<16 | (uint32_t)b[2]<<8 | (uint32_t)b[3];
}

bool protocolSendInviteRequest(uint8_t to, uint32_t code6){
  Packet p{}; 
  p.sender=DEVICE_ID; p.receiver=to; p.type=TYPE_INV_REQ; p.seq=0;
  memset(p.body,0,sizeof(p.body));
  putU32BE((uint8_t*)p.body, code6);
  String nm = storageDeviceName();
  nm.substring(0,20).toCharArray(p.body+4, 21); // up to 20 chars + NUL
  p.len = 24; // 4 (code) + 20 (name)
  return sendRaw(p);
}

bool protocolSendInviteAccept(uint8_t to, uint32_t code6){
  Packet p{}; 
  p.sender=DEVICE_ID; p.receiver=to; p.type=TYPE_INV_ACK; p.seq=0;
  memset(p.body,0,sizeof(p.body));
  putU32BE((uint8_t*)p.body, code6);
  String nm = storageDeviceName();
  nm.substring(0,20).toCharArray(p.body+4, 21);
  p.len = 24;
  return sendRaw(p);
}

// ===== ACK =====
static bool sendAck(uint8_t to, uint16_t seq){
  Packet p{}; p.sender=DEVICE_ID; p.receiver=to; p.type=TYPE_ACK; p.seq=seq; p.len=0;
  return sendRaw(p);
}

// ===== Encrypted DATA =====
static bool sendEncrypted(uint8_t toId, const String& plaintext, uint16_t seq){
  int idx = storageFindContact(toId);
  if (idx<0) return false;
  const Contact& c = storageContactAt(idx);
  uint8_t nonce4[4]; for (int i=0;i<4;i++) nonce4[i]=(uint8_t)esp_random();

  uint8_t body[160]; size_t ptLen = min((size_t)155, plaintext.length());
  memcpy(body, nonce4, 4);
  memcpy(body+4, plaintext.c_str(), ptLen);
  keystreamXor(c.key, nonce4, body+4, ptLen);

  Packet p{};
  p.sender   = DEVICE_ID;
  p.receiver = toId;
  p.type     = TYPE_DATA;
  p.seq      = seq;
  p.len      = 4 + ptLen;
  memset(p.body,0,sizeof(p.body));
  memcpy(p.body, body, p.len);
  return sendRaw(p);
}

// ===== Pending (non-blocking retries) =====
struct PendingMsg {
  bool     active;
  uint8_t  to;
  uint16_t seq;
  String   text;
  uint8_t  retriesLeft;
  uint32_t nextDeadline;
};
static PendingMsg g_pend = {false, 0, 0, "", 0, 0};

static const uint16_t ACK_TIMEOUT_MS = 1200;
static const uint8_t  MAX_RETRIES    = 3;

void protocolStartSend(uint8_t to, uint16_t seq, const String& text){
  // First shot
  sendEncrypted(to, text, seq);
  // Mark SENT visually
  for (int i = protocolChatCount()-1; i >= 0; --i) {
    ChatMsg m; protocolGetChat(i, m);
    if (m.from == protocolDeviceId() && m.seq == seq) {
      protocolSetChatStatus(i, ST_SENT);
      break;
    }
  }
  g_pend = {true, to, seq, text, MAX_RETRIES, millis() + ACK_TIMEOUT_MS};
}

static void protocolClearPendingIf(uint8_t peer, uint16_t seq){
  if (g_pend.active && g_pend.to == peer && g_pend.seq == seq) {
    g_pend.active = false;
  }
}

void protocolOnAckFrom(uint8_t fromPeer, uint16_t seq){
  for (int i = protocolChatCount()-1; i >= 0; --i) {
    ChatMsg m; protocolGetChat(i, m);
    if (m.from == protocolDeviceId() && m.seq == seq) {
      protocolSetChatStatus(i, ST_DELIVERED);
      if (page == PAGE_CHAT) uiDrawChat();
      break;
    }
  }
  protocolClearPendingIf(fromPeer, seq);
}

void protocolPendingTick(){
  if (!g_pend.active) return;
  uint32_t now = millis();
  if (now < g_pend.nextDeadline) return;

  if (g_pend.retriesLeft == 0){
    // Mark FAILED
    for (int i = protocolChatCount()-1; i >= 0; --i) {
      ChatMsg m; protocolGetChat(i, m);
      if (m.from == protocolDeviceId() && m.seq == g_pend.seq) {
        protocolSetChatStatus(i, ST_FAILED);
        if (page == PAGE_CHAT) uiDrawChat();
        break;
      }
    }
    g_pend.active = false;
    return;
  }

  // Retry once
  sendEncrypted(g_pend.to, g_pend.text, g_pend.seq);
  g_pend.retriesLeft--;
  g_pend.nextDeadline = now + ACK_TIMEOUT_MS;
}

// ===== Public send API =====
void protocolSendChat(const String& text){
  // Allocate seq and push to chat
  uint16_t seq = nextSeq++;
  pushChat(DEVICE_ID, text, ST_QUEUED, seq);
  scrollOffset = 0;
  uiDrawChat();

  // Start non-blocking send/retry
  protocolStartSend(currentPeerId, seq, text);
}

// Broadcast (encrypt per contact; no ACK flow here)
void protocolBroadcast(const String& text){
  int cc = storageContactCount();
  for (int i=0;i<cc;i++){
    uint16_t seq = nextSeq++;
    sendEncrypted(storageContactAt(i).id, text, seq);
  }
}

// ===== Duplicate suppression =====
static uint16_t lastSeqSeen[256] = {0};

// Optional: suppress repeated invite codes
static uint32_t lastInviteReqCode[256] = {0};
static uint32_t lastInviteAckCode[256] = {0};

// Cancel any in-progress invite (requester side or receiver prompt)
void protocolCancelInvite(){
  // If we were inviting someone, clear the “seen” code so a new invite works
  extern uint8_t  inviteeId;
  if (inviteeId != 0) {
    // reset dedupe so a new invite with same code is handled again
    lastInviteReqCode[256];
    lastInviteReqCode[inviteeId] = 0;
  }

  // Fully reset UI-side invite state (ids, code, input buffer)
  extern void inviteReset();
  inviteReset();
}

// ===== Poll receive / dispatch =====
void protocolPoll(){
  // keep periodic discovery while on Search page
  static uint32_t lastDisc = 0;
  if (page == PAGE_SEARCH && millis() - lastDisc > 1000) {
    lastDisc = millis();
    protocolSendDiscReq();
  }

  int p = LoRa.parsePacket();
  if (p <= 0) return;

  dbg_lastRssi = (int8_t)LoRa.packetRssi();

  if (p < (int)sizeof(Packet)) {
    dbg_lastWhy = 1;  // short
    while (LoRa.available()) LoRa.read();
    dbg_rxCount++;
    return;
  }

  // Read exactly once
  Packet r{};
  LoRa.readBytes((uint8_t*)&r, sizeof(r));

  dbg_lastType = r.type;
  dbg_lastFrom = r.sender;
  dbg_lastTo   = r.receiver;

  uint8_t saved = r.crc; r.crc=0;
  uint8_t calc  = crc8((uint8_t*)&r, sizeof(r)-1);
  if (calc != saved){
    dbg_lastWhy = 2;  // bad CRC
    dbg_rxCount++;
    return;
  }

  bool forMe = (r.receiver == DEVICE_ID);
  bool isBc  = (r.receiver == BROADCAST_ID);
  if (!forMe && !isBc){
    dbg_lastWhy = 3;  // wrong dst
    dbg_rxCount++;
    return;
  }

  dbg_lastWhy = 0;  // OK
  dbg_rxCount++;

  // ===== Discovery =====
  if (r.type == TYPE_DISC_REQ && (isBc || forMe)) {
    sendDiscRsp(r.sender);

    char nm[21]={0};
    if (r.len > 0) { memcpy(nm, r.body, min((int)r.len, 20)); nm[20]=0; }
    else { snprintf(nm, sizeof(nm), "ID-%u", r.sender); }
    discUpsert(r.sender, nm, (int8_t)LoRa.packetRssi());

    // legacy nearby too
    addNearby(r.sender, nm);

    if (page == PAGE_SEARCH) uiDrawSearch();
    return;
  }

  if (r.type == TYPE_DISC_RSP && forMe) {
    char nm[21]={0};
    if (r.len > 0) { memcpy(nm, r.body, min((int)r.len, 20)); nm[20]=0; }
    else { snprintf(nm, sizeof(nm), "ID-%u", r.sender); }
    discUpsert(r.sender, nm, (int8_t)LoRa.packetRssi());
    addNearby(r.sender, nm);
    if (page == PAGE_SEARCH) uiDrawSearch();
    return;
  }

  // ===== Invite request =====
  if (r.type == TYPE_INV_REQ && forMe){
    if (r.len >= 4){
      uint32_t code6 = getU32BE((const uint8_t*)r.body);
      if (lastInviteReqCode[r.sender] == code6) return; // ignore repeat
      lastInviteReqCode[r.sender] = code6;

      char fromNm[21]={0};
      if (r.len >= 24) memcpy(fromNm, r.body+4, 20);
      else snprintf(fromNm, sizeof(fromNm), "ID-%u", r.sender);

      discUpsert(r.sender, fromNm, (int8_t)LoRa.packetRssi());

      uiShowInvitePrompt(r.sender, fromNm, code6);
      page = PAGE_INVITE_PROMPT;
      uiForceBlinkRestart();
      uiDrawInvitePrompt();
    }
    return;
  }

  // ===== Invite accept =====
  if (r.type == TYPE_INV_ACK && forMe){
    if (r.len >= 4){
      uint32_t code6 = getU32BE((const uint8_t*)r.body);
      if (lastInviteAckCode[r.sender] == code6) return; // ignore repeat
      lastInviteAckCode[r.sender] = code6;

      char peerNm[21]={0};
      if (r.len >= 24) memcpy(peerNm, r.body+4, 20);
      else snprintf(peerNm, sizeof(peerNm), "ID-%u", r.sender);

      // Only accept if matches current invite
      if (r.sender == inviteeId && code6 == inviteCode){
        Contact c{};
        c.id = r.sender;
        strlcpy(c.name, peerNm, sizeof(c.name));
        memset(c.key, 0, sizeof(c.key)); // TODO: derive a key if you wish
        storageAddContact(c);

        inviteReset();
        page = PAGE_CONTACTS;
        uiDrawContacts();
      }
    }
    return;
  }

  // ===== DATA (chat) =====
  if (r.type == TYPE_DATA && forMe){
    // Duplicate suppression by (sender, seq)
    if (r.seq == lastSeqSeen[r.sender]){
      // Re-ACK so peer stops retrying
      sendAck(r.sender, r.seq);
      return;
    }
    lastSeqSeen[r.sender] = r.seq;

    int cidx = storageFindContact(r.sender);
    if (cidx >= 0 && r.len >= 4){
      uint8_t nonce4[4] = { (uint8_t)r.body[0], (uint8_t)r.body[1], (uint8_t)r.body[2], (uint8_t)r.body[3] };
      int ctLen = r.len - 4;
      uint8_t tmp[160]; memcpy(tmp, r.body+4, ctLen);
      keystreamXor(storageContactAt(cidx).key, nonce4, tmp, ctLen);
      String text = String((const char*)tmp).substring(0, ctLen);

      pushChat(r.sender, text, ST_RECV, r.seq);
      sendAck(r.sender, r.seq);

      buzzIncoming(); vibIncoming();
      if (protocolScrollOffset()==0 && page==PAGE_CHAT) uiDrawChat();
    } else {
      // Unknown contact/key; still ACK to stop sender retries
      sendAck(r.sender, r.seq);
    }
    return;
  }

  // ===== ACK =====
  if (r.type == TYPE_ACK && forMe){
    protocolOnAckFrom(r.sender, r.seq);
    return;
  }
}

// ===== Discovery upsert =====
void discUpsert(uint8_t id, const char* nm, int8_t rssi){
  // update if exists
  for (int i=0;i<g_discCount;i++){
    if (g_disc[i].id == id){
      strlcpy(g_disc[i].name, nm, sizeof(g_disc[i].name));
      g_disc[i].rssi = rssi;
      g_disc[i].lastSeen = millis();
      return;
    }
  }
  // append if space
  if (g_discCount < MAX_DISC){
    g_disc[g_discCount].id = id;
    strlcpy(g_disc[g_discCount].name, nm, sizeof(g_disc[g_discCount].name));
    g_disc[g_discCount].rssi = rssi;
    g_disc[g_discCount].lastSeen = millis();
    g_discCount++;
  } else {
    // replace oldest
    int oldest = 0;
    for (int i=1;i<MAX_DISC;i++){
      if (g_disc[i].lastSeen < g_disc[oldest].lastSeen) oldest = i;
    }
    g_disc[oldest].id = id;
    strlcpy(g_disc[oldest].name, nm, sizeof(g_disc[oldest].name));
    g_disc[oldest].rssi = rssi;
    g_disc[oldest].lastSeen = millis();
  }
}

// ===== Keep Search alive & expire old entries =====
void protocolSearchTick(){
  static uint32_t lastTx=0;
  if (page != PAGE_SEARCH) return;
  uint32_t now = millis();
  if (now - lastTx >= 1000) {  // 1 Hz broadcast
    protocolSendDiscReq();
    lastTx = now;
  }
  // expire entries older than 8s
  for (int i=0;i<g_discCount;){
    if (now - g_disc[i].lastSeen > 8000) {
      g_disc[i] = g_disc[g_discCount-1];
      g_discCount--;
    } else i++;
  }
}

// ===== Init radio =====
void protocolInit(){
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_BAND, true)) {
    oled.clear(); oled.drawString(0,0,"LoRa init fail"); oled.display();
    while(true) delay(1000);
  }
  #if defined(PA_OUTPUT_PA_BOOST_PIN)
    LoRa.setTxPower(LORA_POWER_DBM, PA_OUTPUT_PA_BOOST_PIN);
  #else
    LoRa.setTxPower(LORA_POWER_DBM, 1);
  #endif
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.receive();
}
