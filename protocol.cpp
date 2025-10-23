
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

// ----- LoRa pins / radio config (Heltec WiFi LoRa 32 V2) -----
#define LORA_SCK   5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS   18
#define LORA_RST  14
#define LORA_DIO0 26
static const long    LORA_BAND       = 915E6;
static const uint8_t LORA_POWER_DBM  = 14;

// ----- Device ID (manual for now; set 1 or 2) -----
#ifndef DEVICE_ID
#define DEVICE_ID 1
#endif

uint8_t protocolDeviceId(){ return DEVICE_ID; }

// ----- Packet -----
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

DiscEntry g_disc[MAX_DISC];
int g_discCount = 0;

// Debug send receive msg
static uint32_t dbg_rxCount = 0;
static int8_t   dbg_lastRssi = 0;
static uint8_t  dbg_lastType = 0, dbg_lastFrom = 0, dbg_lastTo = 0;
static uint8_t  dbg_lastWhy  = 0;  // 0 ok, 1 short, 2 badcrc, 3 not-for-me

// ----- Chat storage -----
static const int MAX_MSGS = 64;
static ChatMsg chatBuf[MAX_MSGS];
static int chatCount = 0;
static int scrollOffset = 0;
uint16_t nextSeq = 1;
static const uint8_t  RETRIES        = 3;
static const uint16_t ACK_TIMEOUT_MS = 1200;

// One slot per possible sender ID (0..255). Zero-init is fine.
static uint16_t lastSeqSeen[256] = {0};

// Optional: also suppress duplicate INV_REQ/ACK repeats by code
static uint32_t lastInviteReqCode[256] = {0};
static uint32_t lastInviteAckCode[256] = {0};

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

// ----- Nearby cache -----
static NearbyItem nearby[10];
static int nearbyCount = 0;
static int nearbySel = 0;
int  protocolNearbyCount(){ return nearbyCount; }
NearbyItem protocolNearbyAt(int i){ return nearby[i]; }
void protocolNearbyClear(){ nearbyCount=0; nearbySel=0; g_discCount = 0; }
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
  if (nearbyCount<10){ nearby[nearbyCount].id=id; memset(nearby[nearbyCount].name,0,16); strncpy(nearby[nearbyCount].name,nm,15); nearbyCount++; }
}

// ----- Pairing (invite/accept) -----
// static uint32_t inviteCode = 0;
// static uint8_t  inviteeId  = 0;
static uint8_t  inviteNonce[8];
static bool     awaitingAccept=false;
static char     lastInviter[16]={0};
uint32_t protocolInviteCode(){ return inviteCode; }
uint8_t  protocolInviteeId(){ return inviteeId; }
void protocolCancelInvite(){ awaitingAccept=false; }
const char* protocolLastInviterName(){ return lastInviter; }

// ----- Current chat peer -----
static uint8_t currentPeerId = 0;
void protocolEnterChat(uint8_t peerId){ currentPeerId = peerId; scrollOffset=0; }

// ----- Radio helpers -----
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
  p.sender = DEVICE_ID;
  p.receiver = BROADCAST_ID;
  p.type = TYPE_DISC_REQ;
  p.seq = 0;
  // include our name so peers can show us immediately if they want
  String nm = storageDeviceName(); // your getter for saved name
  p.len = (uint8_t)min((size_t)20, nm.length());
  memset(p.body, 0, sizeof(p.body));
  nm.substring(0,p.len).toCharArray(p.body, p.len+1);
  p.crc = 0; p.crc = crc8(reinterpret_cast<uint8_t*>(&p), sizeof(p)-1);

  sendRaw(p);
}

static void sendDiscRsp(uint8_t to){
  Packet p{}; 
  p.sender = DEVICE_ID;
  p.receiver = to;
  p.type = TYPE_DISC_RSP;
  p.seq = 0;
  String nm = storageDeviceName();
  p.len = (uint8_t)min((size_t)20, nm.length());
  memset(p.body, 0, sizeof(p.body));
  nm.substring(0,p.len).toCharArray(p.body, p.len+1);
  p.crc = 0; p.crc = crc8(reinterpret_cast<uint8_t*>(&p), sizeof(p)-1);

  sendRaw(p);
}

void protocolStartInvite(){
  int sel = protocolNearbySel();
  if (sel<0 || sel>=nearbyCount) return;
  inviteeId = nearby[sel].id;
  for (int i=0;i<16;i++) lastInviter[i]=0; // we use it on invite prompt path
  // create nonce and 6-digit code
  for (int i=0;i<8;i++) inviteNonce[i]=(uint8_t)esp_random();
  inviteCode = (esp_random() % 900000) + 100000;
  awaitingAccept=true;

  Packet p{};
  p.sender=DEVICE_ID; p.receiver=inviteeId; p.type=TYPE_INV_REQ; p.seq=nextSeq++;
  p.len=1+16+8; memset(p.body,0,160);
  p.body[0]=(char)DEVICE_ID;
  strncpy(p.body+1, storageDeviceName().c_str(), 15);
  memcpy(p.body+17, inviteNonce, 8);
  sendRaw(p);
}

void protocolSendAccept(uint32_t code6){
  // accept invites stored in lastInviter + inviteeId as inviter id
  Packet p{};
  p.sender=DEVICE_ID; p.receiver=inviteeId; p.type=TYPE_INV_ACK; p.seq=nextSeq++;
  p.len=1+16+8+4; memset(p.body,0,160);
  p.body[0]=(char)DEVICE_ID;
  strncpy(p.body+1, storageDeviceName().c_str(), 15);
  memcpy(p.body+17, inviteNonce, 8);
  p.body[25]=(code6>>24)&0xFF; p.body[26]=(code6>>16)&0xFF; p.body[27]=(code6>>8)&0xFF; p.body[28]=code6&0xFF;
  sendRaw(p);
}

static bool sendAck(uint8_t to, uint16_t seq){
  Packet p{};
  p.sender=DEVICE_ID; p.receiver=to; p.type=TYPE_ACK; p.seq=seq; p.len=0;
  return sendRaw(p);
}

// ----- Encrypted data -----
static bool sendEncrypted(uint8_t toId, const String& plaintext){
  int idx = storageFindContact(toId);
  if (idx<0) return false;
  const Contact& c = storageContactAt(idx);
  uint8_t nonce4[4]; for (int i=0;i<4;i++) nonce4[i]=(uint8_t)esp_random();

  uint8_t body[160]; size_t ptLen = min((size_t)155, plaintext.length());
  memcpy(body, nonce4, 4);
  memcpy(body+4, plaintext.c_str(), ptLen);
  keystreamXor(c.key, nonce4, body+4, ptLen);

  Packet p{};
  p.sender=DEVICE_ID; p.receiver=toId; p.type=TYPE_DATA; p.seq=nextSeq++;
  p.len = 4 + ptLen;
  memset(p.body,0,160);
  memcpy(p.body, body, p.len);
  return sendRaw(p);
}

static bool waitForAck(uint16_t seq, uint32_t timeoutMs){
  uint32_t start = millis();
  while (millis()-start < timeoutMs){
    int p = LoRa.parsePacket();
    if (p >= (int)sizeof(Packet)){
      Packet r{}; LoRa.readBytes((uint8_t*)&r, sizeof(r));
      uint8_t c=r.crc; r.crc=0; if (crc8((uint8_t*)&r,sizeof(r)-1)!=c) continue;

      if (r.type==TYPE_DATA && r.receiver==DEVICE_ID){
        int cidx = storageFindContact(r.sender);
        if (cidx>=0 && r.len>=4){
          uint8_t nonce4[4]={ (uint8_t)r.body[0], (uint8_t)r.body[1], (uint8_t)r.body[2], (uint8_t)r.body[3] };
          int ctLen=r.len-4; uint8_t tmp[160]; memcpy(tmp,r.body+4,ctLen);
          keystreamXor(storageContactAt(cidx).key, nonce4, tmp, ctLen);
          String text = String((const char*)tmp).substring(0, ctLen);
          pushChat(r.sender, text, ST_RECV, r.seq);
          sendAck(r.sender, r.seq);
          buzzIncoming(); vibIncoming();
          if (protocolScrollOffset()==0) uiDrawChat();
        }
      }
      if (r.receiver==DEVICE_ID && r.type==TYPE_ACK && r.seq==seq) return true;
    }
    delay(3);
  }
  return false;
}

// Public chat send (called by input)
void protocolSendChat(const String& text){
  uint16_t seq = nextSeq++;
  pushChat(DEVICE_ID, text, ST_QUEUED, seq);
  scrollOffset=0; uiDrawChat();

  bool ok=false;
  for (uint8_t a=0; a<RETRIES && !ok; ++a){
    if (sendEncrypted(currentPeerId, text)){
      for (int i=chatCount-1;i>=0;--i) if (chatBuf[i].seq==seq && chatBuf[i].from==DEVICE_ID){ chatBuf[i].status=ST_SENT; break; }
      uiDrawChat();
      if (waitForAck(seq, ACK_TIMEOUT_MS)){
        for (int i=chatCount-1;i>=0;--i) if (chatBuf[i].seq==seq && chatBuf[i].from==DEVICE_ID){ chatBuf[i].status=ST_DELIVERED; break; }
        ok=true; uiDrawChat(); break;
      }
    }
    delay(60);
  }
  if (!ok){
    for (int i=chatCount-1;i>=0;--i) if (chatBuf[i].seq==seq && chatBuf[i].from==DEVICE_ID){ chatBuf[i].status=ST_FAILED; break; }
    uiDrawChat();
  }
}

// Broadcast (encrypt per contact)
void protocolBroadcast(const String& text){
  int cc = storageContactCount();
  for (int i=0;i<cc;i++){
    sendEncrypted(storageContactAt(i).id, text);
  }
}

// ----- Receive / Dispatch -----
void protocolPoll(){
  // keep the periodic discovery ping while on Search page
  static uint32_t lastDisc = 0;
  if (page == PAGE_SEARCH && millis() - lastDisc > 1000) {
    lastDisc = millis();
    protocolSendDiscReq();
  }

  int p = LoRa.parsePacket();
  if (p <= 0) return;

  dbg_lastRssi = (int8_t)LoRa.packetRssi();

  if (p < (int)sizeof(Packet)) {
    // too short to be a Packet
    dbg_lastWhy = 1;  // short
    while (LoRa.available()) LoRa.read();
    dbg_rxCount++;
    return;
  }

  // ---- read ONCE ----
  Packet r{};
  LoRa.readBytes((uint8_t*)&r, sizeof(r));

  // helpers (inline)
  auto crc8_calc = [](uint8_t* buf, size_t len)->uint8_t {
    return crc8(buf, len);
  };
  auto getU32BE = [](const uint8_t* b)->uint32_t {
    return (uint32_t)b[0]<<24 | (uint32_t)b[1]<<16 | (uint32_t)b[2]<<8 | (uint32_t)b[3];
  };

  // CRC check
  uint8_t saved = r.crc; r.crc = 0;
  uint8_t calc  = crc8_calc((uint8_t*)&r, sizeof(r)-1);

  // update debug fields
  dbg_lastType = r.type;
  dbg_lastFrom = r.sender;
  dbg_lastTo   = r.receiver;

  if (calc != saved) {
    dbg_lastWhy = 2;  // bad CRC
    dbg_rxCount++;
    return;
  }

  // address filter (allow broadcast for discovery)
  bool forMe = (r.receiver == DEVICE_ID);
  bool isBc  = (r.receiver == BROADCAST_ID);

  if (!forMe && !isBc) {
    dbg_lastWhy = 3;  // wrong dst
    dbg_rxCount++;
    return;
  }

  dbg_lastWhy = 0; // OK
  dbg_rxCount++;

  // ===================== Handlers =====================

  // ---- Discovery (single, consistent implementation) ----
  if (r.type == TYPE_DISC_REQ && (isBc || forMe)) {
    // Always respond with our name
    sendDiscRsp(r.sender);

    // Upsert sender into discovered list (their name is in body if len>0)
    char nm[21] = {0};
    if (r.len > 0) { memcpy(nm, r.body, min((int)r.len, 20)); nm[20] = 0; }
    else           { snprintf(nm, sizeof(nm), "ID-%u", r.sender); }
    discUpsert(r.sender, nm, (int8_t)LoRa.packetRssi());

    if (page == PAGE_SEARCH) uiDrawSearch();
    return;
  }

  if (r.type == TYPE_DISC_RSP && forMe) {
    char nm[21] = {0};
    if (r.len > 0) { memcpy(nm, r.body, min((int)r.len, 20)); nm[20] = 0; }
    else           { snprintf(nm, sizeof(nm), "ID-%u", r.sender); }
    discUpsert(r.sender, nm, (int8_t)LoRa.packetRssi());
    if (page == PAGE_SEARCH) uiDrawSearch();
    return;
  }

  // ---- INVITE REQUEST: receiver sees prompt, types code ----
  if (r.type == TYPE_INV_REQ && forMe) {
    inviteReset();
    if (r.len >= 4) {
      uint32_t code6 = getU32BE((const uint8_t*)r.body);

      // If we already saw this same code from same sender, ignore (but you could re-show UI if you prefer)
      if (lastInviteReqCode[r.sender] == code6) {
        return;
      }
      lastInviteReqCode[r.sender] = code6;

      char fromNm[21] = {0};
      if (r.len >= 24) memcpy(fromNm, r.body + 4, 20);
      else snprintf(fromNm, sizeof(fromNm), "ID-%u", r.sender);

      discUpsert(r.sender, fromNm, (int8_t)LoRa.packetRssi());

      uiShowInvitePrompt(r.sender, fromNm, code6);
      page = PAGE_INVITE_PROMPT;
      uiForceBlinkRestart();
      uiDrawInvitePrompt();
    }
    return;
  }

  // ---- INVITE ACCEPT: requester verifies code, adds contact ----
  if (r.type == TYPE_INV_ACK && forMe) {
    if (r.len >= 4) {
      uint32_t code6 = getU32BE((const uint8_t*)r.body);

      // ignore repeats of the same ack
      if (lastInviteAckCode[r.sender] == code6) return;
      lastInviteAckCode[r.sender] = code6;

      char peerNm[17] = {0};
      if (r.len >= 24) memcpy(peerNm, r.body + 4, 16);
      else snprintf(peerNm, sizeof(peerNm), "ID-%u", r.sender);

      if (r.sender == inviteeId && code6 == inviteCode) {
        Contact c{};
        c.id = r.sender;
        strlcpy(c.name, peerNm, sizeof(c.name));
        memset(c.key, 0, sizeof(c.key));
        storageAddContact(c);

        inviteReset();
        page = PAGE_CONTACTS;
        uiDrawContacts();
      }
    }
    return;
  }

  // ---- DATA: encrypted chat message to me ----
  if (r.type == TYPE_DATA && forMe) {
    // Duplicate suppress (per sender, seq)
    if (r.seq == lastSeqSeen[r.sender]) {
      // We’ve already processed this DATA. Just ACK again so the sender stops retrying.
      sendAck(r.sender, r.seq);
      return;
    }

    // New sequence: remember it and process
    lastSeqSeen[r.sender] = r.seq;

    int cidx = storageFindContact(r.sender);
    if (cidx >= 0 && r.len >= 4) {
      uint8_t nonce4[4] = { (uint8_t)r.body[0], (uint8_t)r.body[1], (uint8_t)r.body[2], (uint8_t)r.body[3] };
      int ctLen = r.len - 4;
      uint8_t tmp[160]; memcpy(tmp, r.body + 4, ctLen);
      keystreamXor(storageContactAt(cidx).key, nonce4, tmp, ctLen);

      String text = String((const char*)tmp).substring(0, ctLen);
      pushChat(r.sender, text, ST_RECV, r.seq);
      sendAck(r.sender, r.seq);
      buzzIncoming(); vibIncoming();

      if (protocolScrollOffset() == 0 && page == PAGE_CHAT) uiDrawChat();
    }
    return;
  }

  // (Optional: handle TYPE_ACK for your sent messages here)
}

void protocolSearchTick(){
  static uint32_t lastTx=0;
  if (page != PAGE_SEARCH) return;
  uint32_t now = millis();
  if (now - lastTx >= 1000) {  // 1 Hz broadcast
    protocolSendDiscReq();
    lastTx = now;
  }
  // Optionally expire stale entries (e.g., older than 8s)
  for (int i=0;i<g_discCount;){
    if (now - g_disc[i].lastSeen > 8000) {
      // remove by swapping last
      g_disc[i] = g_disc[g_discCount-1];
      g_discCount--;
    } else i++;
  }
}

// ----- Init radio -----
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
  p.crc=0; p.crc=crc8((uint8_t*)&p, sizeof(p)-1);

  LoRa.beginPacket(); LoRa.write((uint8_t*)&p, sizeof(p));
  bool ok=(LoRa.endPacket(true)==1);
  LoRa.receive();
  return ok;
}

bool protocolSendInviteAccept(uint8_t to, uint32_t code6){
  Packet p{}; 
  p.sender=DEVICE_ID; p.receiver=to; p.type=TYPE_INV_ACK; p.seq=0;
  memset(p.body,0,sizeof(p.body));
  putU32BE((uint8_t*)p.body, code6);
  String nm = storageDeviceName();
  nm.substring(0,20).toCharArray(p.body+4, 21);
  p.len = 24;
  p.crc=0; p.crc=crc8((uint8_t*)&p, sizeof(p)-1);

  LoRa.beginPacket(); LoRa.write((uint8_t*)&p, sizeof(p));
  bool ok=(LoRa.endPacket(true)==1);
  LoRa.receive();
  return ok;
}
