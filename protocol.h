#pragma once
#include <Arduino.h>
#include "storage.h"

#define BROADCAST_ID 0xFF

// Message types
enum MsgType : uint8_t {
  TYPE_DATA     = 1,
  TYPE_ACK      = 2,
  TYPE_DISC_REQ = 10,
  TYPE_DISC_RSP = 11,
  TYPE_INV_REQ  = 20,
  TYPE_INV_ACK  = 21
};

// Message priority
enum MsgPriority : uint8_t {
  PRIORITY_NORMAL = 0,   // 3 retries, 1200ms timeout
  PRIORITY_HIGH   = 1,   // 7 retries, 800ms timeout
  PRIORITY_SOS    = 2    // 15 retries + repeat every 30s, 600ms timeout
};

enum MsgStatus : uint8_t { ST_QUEUED, ST_SENT, ST_DELIVERED, ST_FAILED, ST_RECV };

struct ChatMsg {
  uint8_t     from;
  String      text;
  MsgStatus   status;
  uint16_t    seq;
  MsgPriority priority;
  uint32_t    timestamp;  // millis() at receive/send time
};

// Discovery (for UI Search page)
struct NearbyItem {
  uint8_t id;
  char    name[16];
};

struct DiscEntry {
  uint8_t   id;
  char      name[21];
  int8_t    rssi;
  uint32_t  lastSeen;
};

const int MAX_DISC = 10;

extern DiscEntry g_disc[MAX_DISC];
extern int       g_discCount;

// Debug counters
extern uint32_t dbg_rxCount;
extern int8_t   dbg_lastRssi;
extern uint8_t  dbg_lastType, dbg_lastFrom, dbg_lastTo, dbg_lastWhy;

// SOS state
extern bool     g_sosActive;
extern int      g_sosSentCount;

// Init & loop
void protocolInit();
void protocolPoll();
void protocolSearchTick();
void protocolPendingTick();

// Discovery
void protocolSendDiscReq();

// Nearby (legacy)
int        protocolNearbyCount();
NearbyItem protocolNearbyAt(int i);
void       protocolNearbyClear();
void       protocolNearbyMoveSel(int delta);
int        protocolNearbySel();

// Discovery helpers
bool  protocolIsOnline(uint8_t id);    // seen in last 30s
int8_t protocolContactRssi(uint8_t id);

// Invite / accept
void      protocolStartInvite();
uint32_t  protocolInviteCode();
uint8_t   protocolInviteeId();
void      protocolCancelInvite();
const char* protocolLastInviterName();

bool protocolSendInviteRequest(uint8_t to, uint32_t code6);
bool protocolSendInviteAccept(uint8_t to, uint32_t code6);

// Chat
uint8_t protocolDeviceId();
void    protocolEnterChat(uint8_t peerId);
void    protocolSendChat(const String& text, MsgPriority priority = PRIORITY_NORMAL);
void    protocolBroadcast(const String& text);

// SOS
void protocolStartSOS();
void protocolStopSOS();
void protocolSOSTick();   // resend every 30s

// Chat buffer
int  protocolChatCount();
void protocolGetChat(int idx, ChatMsg& out);
int  protocolScrollOffset();
void protocolScroll(int delta);
void protocolClearChat();

// ACK handling
void protocolOnAckFrom(uint8_t fromPeer, uint16_t seq);

// Internal
void protocolStartSend(uint8_t to, uint16_t seq, const String& text, MsgPriority pri);

// Discovery upsert
void discUpsert(uint8_t id, const char* nm, int8_t rssi);

// Allow UI to set a status on a chat row
void protocolSetChatStatus(int idx, MsgStatus st);

// History integration — called from history.cpp when loading persisted msgs
void protocolPushHistoryMsg(uint8_t from, const String& txt, MsgStatus s);

// Unread count per contact
int  protocolUnreadCount(uint8_t contactId);
void protocolMarkRead(uint8_t contactId);

// Current chat peer
uint8_t protocolCurrentPeer();
