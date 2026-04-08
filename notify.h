#pragma once
#include <Arduino.h>

enum NotifyType : uint8_t {
  NOTIFY_MSG    = 0,
  NOTIFY_INVITE = 1,
  NOTIFY_SOS    = 2,
  NOTIFY_DISC   = 3,
};

void notifyInit();
void notifyTick();
void notifyIncoming(NotifyType type);
void notifyHeartbeatTick();   // call from loop for periodic heartbeat LED
