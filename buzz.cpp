
#include "buzz.h"
#include <Arduino.h>

#define BUZZER_PIN 25
#define USE_PASSIVE_BUZZER false

struct BuzzSeg { uint16_t val; uint16_t ms; }; // val: freq for passive, 1/0 for active
static BuzzSeg PATTERN_INCOMING[] = { {1,200},{0,100},{1,200},{0,0} };

static bool active=false;
static uint32_t segStart=0;
static int segIndex=0;

#if USE_PASSIVE_BUZZER
const int BUZZ_LEDC_CH=0, BUZZ_LEDC_BITS=10;
#endif

static void setBuzz(uint16_t v){
#if USE_PASSIVE_BUZZER
  if (v==0){ ledcWriteTone(BUZZ_LEDC_CH,0); ledcWrite(BUZZ_LEDC_CH,0); }
  else { ledcWriteTone(BUZZ_LEDC_CH,v); ledcWrite(BUZZ_LEDC_CH,(1<<(BUZZ_LEDC_BITS-1))); }
#else
  digitalWrite(BUZZER_PIN, v?HIGH:LOW);
#endif
}

void buzzInit(){
  pinMode(BUZZER_PIN, OUTPUT);
#if USE_PASSIVE_BUZZER
  ledcSetup(BUZZ_LEDC_CH, 1000, BUZZ_LEDC_BITS);
  ledcAttachPin(BUZZER_PIN, BUZZ_LEDC_CH);
  ledcWrite(BUZZ_LEDC_CH, 0);
#else
  digitalWrite(BUZZER_PIN, LOW);
#endif
}

void buzzIncoming(){
  active=true; segIndex=0; segStart=millis(); setBuzz(PATTERN_INCOMING[0].val);
}

void buzzTick(){
  if (!active) return;
  BuzzSeg &seg = PATTERN_INCOMING[segIndex];
  if (seg.ms==0 && seg.val==0){ active=false; setBuzz(0); return; }
  if (millis()-segStart >= seg.ms){
    segIndex++;
    BuzzSeg &n = PATTERN_INCOMING[segIndex];
    if (n.ms==0 && n.val==0){ active=false; setBuzz(0); return; }
    setBuzz(n.val);
    segStart = millis();
  }
}
