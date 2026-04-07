#pragma once
#include <Arduino.h>
#include "storage.h"

#define BROADCAST_ID 0xFF

// Messaging types
enum MsgType : uint8_t {
  TYPE_DATA     = 1,
  TYPE_ACK      = 2,
  TYPE_DISC_REQ = 10,
  TYPE_DISC_RSP = 11,
  TYPE_INV_REQ  = 20,   // inviter -> invitee (contains 6-digit code + name)
  TYPE_INV_ACK  = 21    // invitee -> inviter (echoes code + name)
};

enum MsgStatus : uint8_t { ST_QUEUED, ST_SENT, ST_DELIVERED, ST_FAILED, ST_RECV };

struct ChatMsg {
  uint8_t   from;
  String    text;
  MsgStatus status;
  uint16_t  seq;
};

// Nearby search cache (legacy helpers still exposed)
struct NearbyItem {
  uint8_t id;
  char    name[16];
};

// Discovery (for UI Search page)
struct DiscEntry {
  uint8_t   id;
  char      name[21];   // 20 + null
  int8_t    rssi;
  uint32_t  lastSeen;
};

const int MAX_DISC = 10;

// Exposed discovery list for UI overlay (debug/status)
extern DiscEntry g_disc[MAX_DISC];
extern int       g_discCount;

// Debug counters for uiDrawRadioDebugOverlay()
extern uint32_t dbg_rxCount;
extern int8_t   dbg_lastRssi;
extern uint8_t  dbg_lastType, dbg_lastFrom, dbg_lastTo, dbg_lastWhy;

// Init & loop
void protocolInit();
void protocolPoll();
void protocolSearchTick();         // call from loop (keeps discovery alive)
void protocolPendingTick();        // non-blocking retry timer tick

// Discovery broadcast
void protocolSendDiscReq();

// Nearby (legacy simple list kept for compatibility)
int  protocolNearbyCount();
NearbyItem protocolNearbyAt(int i);
void protocolNearbyClear();
void protocolNearbyMoveSel(int delta);
int  protocolNearbySel();

// Invite / accept handshake
void      protocolStartInvite();                      // start invite to selected nearby
uint32_t  protocolInviteCode();                       // current 6-digit code you generated
uint8_t   protocolInviteeId();                        // current target id
void      protocolCancelInvite();                     // cancel in-flight invite
const char* protocolLastInviterName();                // optional display

bool protocolSendInviteRequest(uint8_t to, uint32_t code6);
bool protocolSendInviteAccept(uint8_t to, uint32_t code6);

// Chat
uint8_t protocolDeviceId();
void    protocolEnterChat(uint8_t peerId);
void    protocolSendChat(const String& text);
void    protocolBroadcast(const String& text);

// Chat buffer queries for UI
int   protocolChatCount();
void  protocolGetChat(int idx, ChatMsg& out);
int   protocolScrollOffset();
void  protocolScroll(int delta);

// ACK handling (called internally from protocolPoll)
void protocolOnAckFrom(uint8_t fromPeer, uint16_t seq);

// (internal send state, exposed so other modules can call tick)
void protocolStartSend(uint8_t to, uint16_t seq, const String& text);

// Discovery upsert (used by protocolPoll and UI)
void discUpsert(uint8_t id, const char* nm, int8_t rssi);

// Optional: allow UI to set a status on a chat row
void protocolSetChatStatus(int idx, MsgStatus st);
