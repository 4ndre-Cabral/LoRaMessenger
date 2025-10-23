
#pragma once
#include <Arduino.h>
#include "storage.h"

#define BROADCAST_ID 0xFF

// Messaging types
enum MsgType : uint8_t {
  TYPE_DATA = 1,
  TYPE_ACK  = 2,
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

// Nearby cache item
struct NearbyItem {
  uint8_t id;
  char    name[16];
};

struct DiscEntry {
  uint8_t id;
  char    name[21];   // 20 + null
  int8_t  rssi;
  uint32_t lastSeen;
};

const int MAX_DISC = 10;
extern DiscEntry g_disc[MAX_DISC];
extern int g_discCount;

void protocolInit();
void protocolPoll();
void protocolSearchTick();   // call from loop

// Nearby search controls
void protocolSendDiscReq();
int  protocolNearbyCount();
NearbyItem protocolNearbyAt(int i);
void protocolNearbyClear();
void protocolNearbyMoveSel(int delta);
int  protocolNearbySel();

// Invite/accept
void protocolStartInvite();
uint32_t protocolInviteCode();
uint8_t  protocolInviteeId();
void protocolCancelInvite();
const char* protocolLastInviterName();
void protocolSendAccept(uint32_t code6);

bool protocolSendInviteRequest(uint8_t to, uint32_t code6);
bool protocolSendInviteAccept(uint8_t to, uint32_t code6);

// Chat
uint8_t protocolDeviceId();
void protocolEnterChat(uint8_t peerId);
void protocolSendChat(const String& text);
void protocolBroadcast(const String& text);

// Chat buffer queries for UI
int  protocolChatCount();
void protocolGetChat(int idx, ChatMsg& out);
int  protocolScrollOffset();
void protocolScroll(int delta);

void discUpsert(uint8_t id, const char* nm, int8_t rssi);
