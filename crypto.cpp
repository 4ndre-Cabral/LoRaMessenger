#include "crypto.h"
#include "mbedtls/sha256.h"

void sha256(const uint8_t* in, size_t inlen, uint8_t out[32]){
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);            // use non-_ret variants
  mbedtls_sha256_update(&ctx, in, inlen);
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

void derivePairKey(const String& a, const String& b, uint32_t code6, const uint8_t nonce8[8], uint8_t outKey[32]){
  String A=a, B=b;
  if (A > B) { String T=A; A=B; B=T; }
  uint8_t buf[16+16+4+8] = {0};
  memcpy(buf, A.c_str(), min((size_t)16, A.length()));
  memcpy(buf+16, B.c_str(), min((size_t)16, B.length()));
  buf[32]=(code6>>24)&0xFF; buf[33]=(code6>>16)&0xFF; buf[34]=(code6>>8)&0xFF; buf[35]=code6&0xFF;
  memcpy(buf+36, nonce8, 8);
  sha256(buf, sizeof(buf), outKey);
}

void hmacSign(const uint8_t key[32], const uint8_t nonce4[4], const uint8_t* msg, size_t len, uint8_t tag4[4]){
  // Derive auth subkey: SHA256(key || "auth")
  uint8_t sigKey[32];
  uint8_t kmat[36];
  memcpy(kmat, key, 32);
  kmat[32]='a'; kmat[33]='u'; kmat[34]='t'; kmat[35]='h';
  sha256(kmat, 36, sigKey);

  // tag = SHA256(sigKey || nonce4 || plaintext)
  // max len = 151 bytes, so tbuf max = 32+4+151 = 187 bytes (safe on ESP32 stack)
  uint8_t tbuf[32 + 4 + 151];
  size_t clen = (len > 151) ? 151 : len;
  memcpy(tbuf, sigKey, 32);
  memcpy(tbuf + 32, nonce4, 4);
  memcpy(tbuf + 36, msg, clen);
  uint8_t tag32[32];
  sha256(tbuf, 36 + clen, tag32);
  memcpy(tag4, tag32, 4);
}

bool hmacVerify(const uint8_t key[32], const uint8_t nonce4[4], const uint8_t* msg, size_t len, const uint8_t tag4[4]){
  uint8_t expected[4];
  hmacSign(key, nonce4, msg, len, expected);
  return memcmp(expected, tag4, 4) == 0;
}

void keystreamXor(const uint8_t key[32], const uint8_t nonce4[4], uint8_t* buf, size_t len){
  uint32_t counter=0;
  size_t off=0;
  while (off < len){
    uint8_t block[32];
    uint8_t material[32+4+4];
    memcpy(material, key, 32);
    memcpy(material+32, nonce4, 4);
    material[36]=(counter>>24)&0xFF;
    material[37]=(counter>>16)&0xFF;
    material[38]=(counter>>8)&0xFF;
    material[39]=(counter)&0xFF;
    sha256(material, sizeof(material), block);
    size_t n = min((size_t)32, len-off);
    for (size_t i=0;i<n;i++) buf[off+i] ^= block[i];
    off += n;
    counter++;
  }
}
