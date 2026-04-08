#pragma once
#include <Arduino.h>
#include "HT_SSD1306Wire.h"

#define DEVICE_NAME_MAX_LEN 20
#define CHAT_MSG_MAX_LEN    60

extern SSD1306Wire oled;

// ===== Page enum (v2 — expanded) =====
enum Page : uint8_t {
  // Existing pages
  PAGE_NAME,
  PAGE_CONTACTS,
  PAGE_SEARCH,
  PAGE_INVITE_CODE,
  PAGE_INVITE_PROMPT,
  PAGE_CHAT,
  PAGE_BROADCAST,
  PAGE_CONFIG,
  PAGE_CONFIRM_RESET,
  // New pages (v2)
  PAGE_LOCK,
  PAGE_SLEEP_STATUS,
  PAGE_SOS,
  PAGE_SETTINGS_NOTIFY,
  PAGE_SETTINGS_POWER,
  PAGE_SETTINGS_SECURITY,
  PAGE_SETTINGS_MESSAGES,
  PAGE_SETTINGS_SYSTEM,
  PAGE_CONTACT_DETAIL,
  PAGE_PIN_SETUP,
  PAGE_PIN_CHANGE,
  PAGE_BT_PAIR,
};

extern Page page;

// ===== Invite state =====
extern uint8_t  inviteeId;
extern uint32_t inviteCode;
extern uint8_t  inviterId;
extern char     inviterName[21];
extern uint32_t inviterCodeExpected;
extern uint8_t  inviterNonce8[8];    // nonce received in INV_REQ (invitee side)

// ===== Hardware init =====
void appInitHardware();
void uiBootStep(const char* step);  // update boot splash during setup

// ===== Boot page =====
void uiEnterBootPage();

// ===== UI tick (blink / animations) =====
void uiTick();

// ===== Draw functions =====
void uiDrawStatusBar();          // 12px top bar — call inside every page draw
void uiDrawNameEntry();
void uiDrawContacts();
void uiDrawSearch();
void uiDrawChat();
void uiDrawConfig();
void uiDrawConfirmReset();
void uiRedrawComposeBand(bool push);

void uiDrawInviteCode();
void uiShowInviteCode(uint8_t toId, uint32_t code6);
void uiShowInvitePrompt(uint8_t fromId, const char* fromName, uint32_t code6);
void uiDrawInvitePrompt();

// Lock & sleep
void uiDrawLock();
void uiDrawSleepStatus();

// SOS
void uiDrawSOS();

// Settings sub-pages
void uiDrawSettingsNotify();
void uiDrawSettingsPower();
void uiDrawSettingsSecurity();
void uiDrawSettingsMessages();
void uiDrawSettingsSystem();
void uiDrawContactDetail();
void uiDrawPinSetup();
void uiDrawPinChange();
void uiDrawBTPair();

// Invite helpers
void inviteReset();
bool inviteInProgress();

// Utility
void uiForceBlinkRestart();
void uiToast(const String& msg);
void uiDebugBlinkOverlay();
