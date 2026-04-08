
#pragma once
#include <Arduino.h>
void sha256(const uint8_t* in, size_t inlen, uint8_t out[32]);
void derivePairKey(const String& a, const String& b, uint32_t code6, const uint8_t nonce8[8], uint8_t outKey[32]);
void keystreamXor(const uint8_t key[32], const uint8_t nonce4[4], uint8_t* buf, size_t len);

// 4-byte authentication tag (SHA-256 based MAC)
// tag = SHA256(authKey || nonce4 || plaintext)[0:4]
// where authKey = SHA256(key || "auth")
void hmacSign(const uint8_t key[32], const uint8_t nonce4[4], const uint8_t* msg, size_t len, uint8_t tag4[4]);
bool hmacVerify(const uint8_t key[32], const uint8_t nonce4[4], const uint8_t* msg, size_t len, const uint8_t tag4[4]);
