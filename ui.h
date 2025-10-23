
#pragma once
#include <Arduino.h>
#include "HT_SSD1306Wire.h"

#define DEVICE_NAME_MAX_LEN 20
#define CHAT_MSG_MAX_LEN 60

// Expose display so other modules can render simple toasts if needed
extern SSD1306Wire oled;

// Application pages
enum Page : uint8_t { PAGE_NAME, PAGE_CONTACTS, PAGE_SEARCH, PAGE_INVITE_CODE,
                      PAGE_INVITE_PROMPT, PAGE_CHAT, PAGE_BROADCAST, PAGE_CONFIG, PAGE_CONFIRM_RESET };
extern Page page;

// === Invite state (symbol names matching your project) ===
// Requester (the device sending the invite)
extern uint8_t  inviteeId;     // who I'm inviting
extern uint32_t inviteCode;    // my 6-digit code

// Receiver (the device that got an invite)
extern uint8_t  inviterId;             // who invited me
extern char     inviterName[21];       // inviter's name (NUL-terminated)
extern uint32_t inviterCodeExpected;   // code I need to type

// Hardware init (Vext + OLED)
void appInitHardware();

// Decide first page based on storage (name set?)
void uiEnterBootPage();

// Small recurring UI ticks (blink caret / indicators)
void uiTick();

// Draw helpers
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

void inviteReset();
bool inviteInProgress();

void uiForceBlinkRestart();

void uiToast(const String& msg);
