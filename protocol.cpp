#include "protocol.h"
#include <SPI.h>
#include <esp_random.h>
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
#include "notify.h"
#include "history.h"
#include "settings.h"

// ===== LoRa pins / radio config (Heltec WiFi LoRa 32 V2) =====
#define LORA_SCK   5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS   18
#define LORA_RST  14
#define LORA_DIO0 26
static const long    LORA_BAND       = 915E6;
static const uint8_t LORA_POWER_DBM  = 14;

#ifndef DEVICE_ID
#define DEVICE_ID 1
#endif
uint8_t protocolDeviceId(){ return DEVICE_ID; }

// ===== Packet (v3: CRC8→CRC16, flags byte for priority) =====
struct Packet {
  uint8_t  sender;
  uint8_t  receiver;
  uint8_t  type;
  uint8_t  flags;    // bit0-1: MsgPriority; bit2: SOS repeat marker
  uint16_t seq;
  uint8_t  len;
  char     body[160];
  uint16_t crc;      // CRC-16/CCITT over all preceding bytes
} __attribute__((packed));

// CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)
static uint16_t crc16(const uint8_t* data, size_t len){
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < len; i++){
    c ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c<<1)^0x1021) : (uint16_t)(c<<1);
  }
  return c;
}

// ===== Discovery cache =====
DiscEntry g_disc[MAX_DISC];
int       g_discCount = 0;

// ===== Debug counters =====
uint32_t dbg_rxCount  = 0;
int8_t   dbg_lastRssi = 0;
uint8_t  dbg_lastType = 0, dbg_lastFrom = 0, dbg_lastTo = 0, dbg_lastWhy = 0;

// ===== SOS state =====
bool g_sosActive    = false;
int  g_sosSentCount = 0;
static uint32_t sosLastSentMs = 0;

// ===== Chat storage =====
static const int MAX_MSGS = 64;
static ChatMsg   chatBuf[MAX_MSGS];
static int       chatCount    = 0;
static int       scrollOffset = 0;
static uint16_t  nextSeq      = 1;

// Unread per contact (index by contact id, 0..255)
static uint8_t unreadCount[256] = {0};

int  protocolChatCount(){ return chatCount; }
void protocolGetChat(int idx, ChatMsg& out){ out = chatBuf[idx]; }
int  protocolScrollOffset(){ return scrollOffset; }
void protocolScroll(int delta){
  if (delta > 0) scrollOffset += 1;
  else if (scrollOffset > 0) scrollOffset -= 1;
}
void protocolClearChat(){
  chatCount    = 0;
  scrollOffset = 0;
}

int protocolUnreadCount(uint8_t id){ return unreadCount[id]; }
void protocolMarkRead(uint8_t id){ unreadCount[id] = 0; }

static void pushChat(uint8_t from, const String& text, MsgStatus st, uint16_t seq=0,
                     MsgPriority pri=PRIORITY_NORMAL){
  ChatMsg m;
  m.from      = from;
  m.text      = text;
  m.status    = st;
  m.seq       = seq;
  m.priority  = pri;
  m.timestamp = millis();

  if (chatCount < MAX_MSGS) {
    chatBuf[chatCount++] = m;
  } else {
    for (int i = 1; i < MAX_MSGS; i++) chatBuf[i-1] = chatBuf[i];
    chatBuf[MAX_MSGS-1] = m;
  }
}

void protocolPushHistoryMsg(uint8_t from, const String& txt, MsgStatus s){
  pushChat(from, txt, s, 0, PRIORITY_NORMAL);
}

void protocolSetChatStatus(int idx, MsgStatus st){
  if (idx < 0 || idx >= chatCount) return;
  chatBuf[idx].status = st;
}

// ===== Nearby (legacy helpers) =====
static NearbyItem nearby[10];
static int nearbyCount = 0;
static int nearbySel   = 0;

int  protocolNearbyCount(){ return nearbyCount; }
NearbyItem protocolNearbyAt(int i){ return nearby[i]; }
void protocolNearbyClear(){ nearbyCount = 0; nearbySel = 0; g_discCount = 0; }
void protocolNearbyMoveSel(int delta){
  if (nearbyCount == 0){ nearbySel = 0; return; }
  nearbySel += delta;
  if (nearbySel < 0) nearbySel = 0;
  if (nearbySel >= nearbyCount) nearbySel = nearbyCount-1;
}
int protocolNearbySel(){ return nearbySel; }

static void addNearby(uint8_t id, const char* nm){
  if (id == DEVICE_ID) return;
  if (storageFindContact(id) >= 0) return;
  for (int i = 0; i < nearbyCount; i++) if (nearby[i].id == id) return;
  if (nearbyCount < 10){
    nearby[nearbyCount].id = id;
    memset(nearby[nearbyCount].name, 0, 16);
    strncpy(nearby[nearbyCount].name, nm, 15);
    nearbyCount++;
  }
}

bool protocolIsOnline(uint8_t id){
  for (int i = 0; i < g_discCount; i++){
    if (g_disc[i].id == id && (millis() - g_disc[i].lastSeen) < 30000) return true;
  }
  return false;
}

int8_t protocolContactRssi(uint8_t id){
  for (int i = 0; i < g_discCount; i++){
    if (g_disc[i].id == id) return g_disc[i].rssi;
  }
  return 0;
}

// ===== Invite state (externs from ui.cpp) =====
extern uint8_t  inviteeId;
extern uint32_t inviteCode;
extern uint8_t  inviterId;
extern char     inviterName[21];
extern uint32_t inviterCodeExpected;
extern uint8_t  inviterNonce8[8];   // nonce received in INV_REQ (invitee side)

extern void uiShowInvitePrompt(uint8_t fromId, const char* fromNm, uint32_t code6);
extern void uiShowInviteCode(uint8_t toId, uint32_t code6);
extern void inviteReset();

// nonce8 used by the inviter (generated at invite start)
static uint8_t inviteNonce8[8] = {0};

// ===== Current chat peer =====
static uint8_t currentPeerId = 0;
uint8_t protocolCurrentPeer() { return currentPeerId; }

void protocolEnterChat(uint8_t peerId){
  currentPeerId = peerId;
  scrollOffset  = 0;
  protocolMarkRead(peerId);
}

// ===== Radio helpers =====
static bool sendRaw(const Packet& p){
  Packet q = p;
  q.crc = 0;
  q.crc = crc16((const uint8_t*)&q, sizeof(q)-2);
  LoRa.beginPacket();
  LoRa.write((const uint8_t*)&q, sizeof(q));
  bool ok = (LoRa.endPacket(true) == 1);
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
  p.len      = (uint8_t)min((size_t)20, nm.length());
  memset(p.body, 0, sizeof(p.body));
  nm.substring(0, p.len).toCharArray(p.body, p.len+1);
  sendRaw(p);
}

static void sendDiscRsp(uint8_t to){
  Packet p{};
  p.sender   = DEVICE_ID;
  p.receiver = to;
  p.type     = TYPE_DISC_RSP;
  p.seq      = 0;
  String nm  = storageDeviceName();
  p.len      = (uint8_t)min((size_t)20, nm.length());
  memset(p.body, 0, sizeof(p.body));
  nm.substring(0, p.len).toCharArray(p.body, p.len+1);
  sendRaw(p);
}

static inline void putU32BE(uint8_t* b, uint32_t v){
  b[0]=uint8_t(v>>24); b[1]=uint8_t(v>>16); b[2]=uint8_t(v>>8); b[3]=uint8_t(v);
}
static inline uint32_t getU32BE(const uint8_t* b){
  return (uint32_t)b[0]<<24 | (uint32_t)b[1]<<16 | (uint32_t)b[2]<<8 | (uint32_t)b[3];
}

// INV_REQ body format (v2): code6(4B) + name(20B) + nonce8(8B) = 32B
bool protocolSendInviteRequest(uint8_t to, uint32_t code6){
  // Generate fresh nonce8 for this invite
  for (int i = 0; i < 8; i++) inviteNonce8[i] = (uint8_t)(esp_random() & 0xFF);

  Packet p{};
  p.sender   = DEVICE_ID;
  p.receiver = to;
  p.type     = TYPE_INV_REQ;
  p.seq      = 0;
  memset(p.body, 0, sizeof(p.body));
  putU32BE((uint8_t*)p.body, code6);
  String nm = storageDeviceName();
  nm.substring(0, 20).toCharArray(p.body+4, 21);
  memcpy(p.body+24, inviteNonce8, 8);
  p.len = 32;
  return sendRaw(p);
}

// INV_ACK body format (v2): code6(4B) + name(20B) + nonce8(8B) = 32B (echoes inviter's nonce)
bool protocolSendInviteAccept(uint8_t to, uint32_t code6){
  Packet p{};
  p.sender   = DEVICE_ID;
  p.receiver = to;
  p.type     = TYPE_INV_ACK;
  p.seq      = 0;
  memset(p.body, 0, sizeof(p.body));
  putU32BE((uint8_t*)p.body, code6);
  String nm = storageDeviceName();
  nm.substring(0, 20).toCharArray(p.body+4, 21);
  memcpy(p.body+24, inviterNonce8, 8);  // echo back inviter's nonce8
  p.len = 32;
  return sendRaw(p);
}

// ===== ACK =====
static bool sendAck(uint8_t to, uint16_t seq){
  Packet p{};
  p.sender   = DEVICE_ID;
  p.receiver = to;
  p.type     = TYPE_ACK;
  p.seq      = seq;
  p.len      = 0;
  return sendRaw(p);
}

// ===== Encrypted DATA =====
static bool sendEncrypted(uint8_t toId, const String& plaintext, uint16_t seq, MsgPriority pri){
  int idx = storageFindContact(toId);
  if (idx < 0) return false;
  const Contact& c = storageContactAt(idx);

  uint8_t nonce4[4];
  for (int i = 0; i < 4; i++) nonce4[i] = (uint8_t)(esp_random() & 0xFF);

  // Layout: nonce4(4) | encrypt( plaintext(ptLen) | tag(4) )
  // ptLen capped at 151 to leave 4 bytes for the HMAC tag within body[160]
  uint8_t body[160];
  size_t  ptLen = min((size_t)151, plaintext.length());

  memcpy(body, nonce4, 4);
  memcpy(body+4, plaintext.c_str(), ptLen);

  // Append 4-byte authentication tag (over plaintext, before encryption)
  uint8_t tag4[4];
  hmacSign(c.key, nonce4, (const uint8_t*)plaintext.c_str(), ptLen, tag4);
  memcpy(body+4+ptLen, tag4, 4);

  // Encrypt plaintext+tag together
  keystreamXor(c.key, nonce4, body+4, ptLen+4);

  Packet p{};
  p.sender   = DEVICE_ID;
  p.receiver = toId;
  p.type     = TYPE_DATA;
  p.flags    = (uint8_t)pri;
  p.seq      = seq;
  p.len      = 4 + ptLen + 4;   // nonce + ciphertext + tag
  memset(p.body, 0, sizeof(p.body));
  memcpy(p.body, body, p.len);
  return sendRaw(p);
}

// ===== Pending (non-blocking retries with priority) =====
#define PEND_QUEUE_SIZE 4

struct PendingMsg {
  bool        active;
  uint8_t     to;
  uint16_t    seq;
  String      text;
  MsgPriority priority;
  uint8_t     retriesLeft;
  uint32_t    nextDeadline;
};
static PendingMsg g_pendQ[PEND_QUEUE_SIZE];

static const uint16_t ACK_TIMEOUT_NORMAL = 1200;
static const uint16_t ACK_TIMEOUT_HIGH   = 800;
static const uint16_t ACK_TIMEOUT_SOS    = 600;
static const uint8_t  MAX_RETRIES_NORMAL = 3;
static const uint8_t  MAX_RETRIES_HIGH   = 7;
static const uint8_t  MAX_RETRIES_SOS    = 15;

static uint16_t ackTimeoutFor(MsgPriority p){
  if (p == PRIORITY_HIGH) return ACK_TIMEOUT_HIGH;
  if (p == PRIORITY_SOS)  return ACK_TIMEOUT_SOS;
  return ACK_TIMEOUT_NORMAL;
}
static uint8_t maxRetriesFor(MsgPriority p){
  if (p == PRIORITY_HIGH) return MAX_RETRIES_HIGH;
  if (p == PRIORITY_SOS)  return MAX_RETRIES_SOS;
  return MAX_RETRIES_NORMAL;
}

void protocolStartSend(uint8_t to, uint16_t seq, const String& text, MsgPriority pri){
  sendEncrypted(to, text, seq, pri);
  for (int i = protocolChatCount()-1; i >= 0; --i){
    ChatMsg m; protocolGetChat(i, m);
    if (m.from == protocolDeviceId() && m.seq == seq){
      protocolSetChatStatus(i, ST_SENT);
      break;
    }
  }
  // Find a free slot in the pending queue
  for (int i = 0; i < PEND_QUEUE_SIZE; i++){
    if (!g_pendQ[i].active){
      g_pendQ[i] = {true, to, seq, text, pri, maxRetriesFor(pri),
                    millis() + ackTimeoutFor(pri)};
      return;
    }
  }
  // Queue full: evict the lowest-priority slot (or if tie, the one closest to expiry)
  int victim = 0;
  for (int i = 1; i < PEND_QUEUE_SIZE; i++){
    if (g_pendQ[i].priority < g_pendQ[victim].priority) victim = i;
  }
  g_pendQ[victim] = {true, to, seq, text, pri, maxRetriesFor(pri),
                     millis() + ackTimeoutFor(pri)};
}

static void clearPendingIf(uint8_t peer, uint16_t seq){
  for (int i = 0; i < PEND_QUEUE_SIZE; i++){
    if (g_pendQ[i].active && g_pendQ[i].to == peer && g_pendQ[i].seq == seq)
      g_pendQ[i].active = false;
  }
}

void protocolOnAckFrom(uint8_t fromPeer, uint16_t seq){
  for (int i = protocolChatCount()-1; i >= 0; --i){
    ChatMsg m; protocolGetChat(i, m);
    if (m.from == protocolDeviceId() && m.seq == seq){
      protocolSetChatStatus(i, ST_DELIVERED);
      if (page == PAGE_CHAT) uiDrawChat();
      break;
    }
  }
  clearPendingIf(fromPeer, seq);
}

void protocolPendingTick(){
  uint32_t now = millis();
  for (int i = 0; i < PEND_QUEUE_SIZE; i++){
    PendingMsg& p = g_pendQ[i];
    if (!p.active) continue;
    if (now < p.nextDeadline) continue;

    if (p.retriesLeft == 0){
      for (int j = protocolChatCount()-1; j >= 0; --j){
        ChatMsg m; protocolGetChat(j, m);
        if (m.from == protocolDeviceId() && m.seq == p.seq){
          protocolSetChatStatus(j, ST_FAILED);
          if (page == PAGE_CHAT) uiDrawChat();
          break;
        }
      }
      p.active = false;
      continue;
    }

    sendEncrypted(p.to, p.text, p.seq, p.priority);
    p.retriesLeft--;
    p.nextDeadline = now + ackTimeoutFor(p.priority);
  }
}

// ===== SOS =====
void protocolStartSOS(){
  g_sosActive    = true;
  g_sosSentCount = 0;
  sosLastSentMs  = millis() - 30000;  // send immediately on first tick
}

void protocolStopSOS(){
  g_sosActive = false;
}

void protocolSOSTick(){
  if (!g_sosActive) return;
  uint32_t now = millis();
  if (now - sosLastSentMs < 30000) return;
  sosLastSentMs = now;

  String msg = "[SOS] " + storageDeviceName();
  int cc = storageContactCount();
  for (int i = 0; i < cc; i++){
    uint16_t seq = nextSeq++;
    pushChat(DEVICE_ID, msg, ST_QUEUED, seq, PRIORITY_SOS);
    protocolStartSend(storageContactAt(i).id, seq, msg, PRIORITY_SOS);
  }
  // Also broadcast if no contacts or as additional reach
  if (cc == 0){
    Packet p{};
    p.sender   = DEVICE_ID;
    p.receiver = BROADCAST_ID;
    p.type     = TYPE_DISC_REQ;
    p.flags    = (uint8_t)PRIORITY_SOS;
    p.seq      = 0;
    String nm  = "[SOS]" + storageDeviceName();
    p.len = (uint8_t)min((size_t)20, nm.length());
    memset(p.body, 0, sizeof(p.body));
    nm.substring(0, p.len).toCharArray(p.body, p.len+1);
    sendRaw(p);
  }
  g_sosSentCount++;
  if (page == PAGE_SOS) uiDrawSOS();
}

// ===== Public send API =====
void protocolSendChat(const String& text, MsgPriority priority){
  uint16_t seq = nextSeq++;
  pushChat(DEVICE_ID, text, ST_QUEUED, seq, priority);
  scrollOffset = 0;
  uiDrawChat();
  protocolStartSend(currentPeerId, seq, text, priority);

  // Persist to history
  int cidx = storageFindContact(currentPeerId);
  if (cidx >= 0){
    const Contact& c = storageContactAt(cidx);
    uint8_t flags = 0x04 | (uint8_t)priority;  // bit2 = mine
    historyRecord(currentPeerId, DEVICE_ID, text, flags, c.key);
  }
}

void protocolBroadcast(const String& text){
  int cc = storageContactCount();
  for (int i = 0; i < cc; i++){
    uint16_t seq = nextSeq++;
    sendEncrypted(storageContactAt(i).id, text, seq, PRIORITY_NORMAL);
  }
}

// ===== Duplicate suppression (sliding window, N=8 per sender) =====
// seqBase[id] = highest seq seen from sender id
// seqMask[id] = bitmask: bit 0 = seqBase, bit 1 = seqBase-1, ..., bit 7 = seqBase-7
static uint16_t seqBase[256] = {0};
static uint8_t  seqMask[256] = {0};

static bool isDuplicate(uint8_t from, uint16_t seq){
  int16_t delta = (int16_t)(seq - seqBase[from]);
  if (delta > 0){
    // New seq ahead of window: advance base, shift mask, mark seen
    uint8_t shift = (delta > 7) ? 8 : (uint8_t)delta;
    seqMask[from] = (uint8_t)((seqMask[from] << shift) | 1);
    seqBase[from] = seq;
    return false;
  }
  // seq <= seqBase: within window or too old
  if (-delta >= 8) return true;           // outside window, treat as duplicate
  uint8_t bit = (uint8_t)(1u << (-delta));
  bool dup = (seqMask[from] & bit) != 0;
  seqMask[from] |= bit;                   // mark seen regardless
  return dup;
}
static uint32_t lastInviteReqCode[256] = {0};
static uint32_t lastInviteAckCode[256] = {0};

void protocolCancelInvite(){
  if (inviteeId != 0) lastInviteReqCode[inviteeId] = 0;
  inviteReset();
}

// Invite helpers (getters for UI)
uint32_t  protocolInviteCode()      { return inviteCode; }
uint8_t   protocolInviteeId()       { return inviteeId; }
void      protocolStartInvite()     { /* state managed via globals */ }
const char* protocolLastInviterName(){ return inviterName; }

// ===== Poll receive / dispatch =====
void protocolPoll(){
  static uint32_t lastDisc = 0;
  if (page == PAGE_SEARCH && millis() - lastDisc > 1000){
    lastDisc = millis();
    protocolSendDiscReq();
  }

  int pktLen = LoRa.parsePacket();
  if (pktLen <= 0) return;

  dbg_lastRssi = (int8_t)LoRa.packetRssi();

  if (pktLen < (int)sizeof(Packet)){
    dbg_lastWhy = 1;
    while (LoRa.available()) LoRa.read();
    dbg_rxCount++;
    return;
  }

  Packet r{};
  LoRa.readBytes((uint8_t*)&r, sizeof(r));

  dbg_lastType = r.type;
  dbg_lastFrom = r.sender;
  dbg_lastTo   = r.receiver;

  uint16_t saved = r.crc; r.crc = 0;
  uint16_t calc  = crc16((uint8_t*)&r, sizeof(r)-2);
  if (calc != saved){ dbg_lastWhy = 2; dbg_rxCount++; return; }

  bool forMe = (r.receiver == DEVICE_ID);
  bool isBc  = (r.receiver == BROADCAST_ID);
  if (!forMe && !isBc){ dbg_lastWhy = 3; dbg_rxCount++; return; }

  dbg_lastWhy = 0;
  dbg_rxCount++;

  // ===== Discovery =====
  if (r.type == TYPE_DISC_REQ && (isBc || forMe)){
    sendDiscRsp(r.sender);
    char nm[21] = {0};
    if (r.len > 0){ memcpy(nm, r.body, min((int)r.len, 20)); nm[20]=0; }
    else { snprintf(nm, sizeof(nm), "ID-%u", r.sender); }
    discUpsert(r.sender, nm, (int8_t)LoRa.packetRssi());
    addNearby(r.sender, nm);
    if (page == PAGE_SEARCH) uiDrawSearch();
    return;
  }

  if (r.type == TYPE_DISC_RSP && forMe){
    char nm[21] = {0};
    if (r.len > 0){ memcpy(nm, r.body, min((int)r.len, 20)); nm[20]=0; }
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
      if (lastInviteReqCode[r.sender] == code6) return;
      lastInviteReqCode[r.sender] = code6;

      char fromNm[21] = {0};
      if (r.len >= 24) memcpy(fromNm, r.body+4, 20);
      else snprintf(fromNm, sizeof(fromNm), "ID-%u", r.sender);

      // Extract nonce8 (v2 format: 32 bytes = code6(4) + name(20) + nonce8(8))
      if (r.len >= 32) {
        memcpy(inviterNonce8, r.body+24, 8);
      } else {
        memset(inviterNonce8, 0, 8);
      }

      discUpsert(r.sender, fromNm, (int8_t)LoRa.packetRssi());
      uiShowInvitePrompt(r.sender, fromNm, code6);
      page = PAGE_INVITE_PROMPT;
      uiForceBlinkRestart();
      uiDrawInvitePrompt();
      notifyIncoming(NOTIFY_INVITE);
    }
    return;
  }

  // ===== Invite accept =====
  if (r.type == TYPE_INV_ACK && forMe){
    if (r.len >= 4){
      uint32_t code6 = getU32BE((const uint8_t*)r.body);
      if (lastInviteAckCode[r.sender] == code6) return;
      lastInviteAckCode[r.sender] = code6;

      char peerNm[21] = {0};
      if (r.len >= 24) memcpy(peerNm, r.body+4, 20);
      else snprintf(peerNm, sizeof(peerNm), "ID-%u", r.sender);

      if (r.sender == inviteeId && code6 == inviteCode){
        // Derive shared key using our stored nonce8
        Contact c{};
        c.id = r.sender;
        strlcpy(c.name, peerNm, sizeof(c.name));
        String myName   = storageDeviceName();
        String peerName = String(peerNm);
        derivePairKey(myName, peerName, code6, inviteNonce8, c.key);
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
    if (isDuplicate(r.sender, r.seq)){
      sendAck(r.sender, r.seq);  // idempotent ACK for retransmitted frames
      return;
    }

    MsgPriority pri = (MsgPriority)(r.flags & 0x03);
    int cidx = storageFindContact(r.sender);
    // Minimum valid: nonce4(4) + at least 1 byte plaintext + tag(4) = 9 bytes
    if (cidx >= 0 && r.len >= 9){
      uint8_t nonce4[4] = {(uint8_t)r.body[0],(uint8_t)r.body[1],
                           (uint8_t)r.body[2],(uint8_t)r.body[3]};
      int ctLen = r.len - 4;          // ciphertext = plaintext + tag (both encrypted)
      uint8_t tmp[160]; memcpy(tmp, r.body+4, ctLen);
      keystreamXor(storageContactAt(cidx).key, nonce4, tmp, ctLen);

      // Verify authentication tag (last 4 bytes of decrypted payload)
      size_t ptLen = (size_t)ctLen - 4;
      const uint8_t* tag4 = tmp + ptLen;
      if (!hmacVerify(storageContactAt(cidx).key, nonce4, tmp, ptLen, tag4)){
        // Tag mismatch: tampered or corrupted packet — discard silently
        return;
      }

      String text = String((const char*)tmp).substring(0, ptLen);

      pushChat(r.sender, text, ST_RECV, r.seq, pri);
      sendAck(r.sender, r.seq);  // ACK only on successful decrypt

      // Track unread if not in this contact's chat
      if (page != PAGE_CHAT || currentPeerId != r.sender){
        if (unreadCount[r.sender] < 99) unreadCount[r.sender]++;
      }

      // Persist to history
      uint8_t hflags = (uint8_t)pri;  // not mine (bit2=0)
      historyRecord(r.sender, r.sender, text, hflags, storageContactAt(cidx).key);

      // Notify
      NotifyType nt = (pri == PRIORITY_SOS) ? NOTIFY_SOS : NOTIFY_MSG;
      notifyIncoming(nt);

      if (protocolScrollOffset() == 0 && page == PAGE_CHAT) uiDrawChat();
      if (page == PAGE_CONTACTS) uiDrawContacts();  // refresh unread badges
    }
    // Unknown sender or malformed packet: silently discard (no ACK)
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
  for (int i = 0; i < g_discCount; i++){
    if (g_disc[i].id == id){
      strlcpy(g_disc[i].name, nm, sizeof(g_disc[i].name));
      g_disc[i].rssi     = rssi;
      g_disc[i].lastSeen = millis();
      return;
    }
  }
  if (g_discCount < MAX_DISC){
    g_disc[g_discCount].id = id;
    strlcpy(g_disc[g_discCount].name, nm, sizeof(g_disc[g_discCount].name));
    g_disc[g_discCount].rssi     = rssi;
    g_disc[g_discCount].lastSeen = millis();
    g_discCount++;
  } else {
    int oldest = 0;
    for (int i = 1; i < MAX_DISC; i++){
      if (g_disc[i].lastSeen < g_disc[oldest].lastSeen) oldest = i;
    }
    g_disc[oldest].id = id;
    strlcpy(g_disc[oldest].name, nm, sizeof(g_disc[oldest].name));
    g_disc[oldest].rssi     = rssi;
    g_disc[oldest].lastSeen = millis();
  }
}

// ===== Keep Search alive & expire old entries =====
void protocolSearchTick(){
  static uint32_t lastTx = 0;
  if (page != PAGE_SEARCH) return;
  uint32_t now = millis();
  if (now - lastTx >= 1000){
    protocolSendDiscReq();
    lastTx = now;
  }
  for (int i = 0; i < g_discCount;){
    if (now - g_disc[i].lastSeen > 8000){
      g_disc[i] = g_disc[g_discCount-1];
      g_discCount--;
    } else i++;
  }
}

// ===== Init radio =====
void protocolInit(){
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_BAND, true)){
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
