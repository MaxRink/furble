#ifndef FURBLE_HOST_HMAC_SHA256_H
#define FURBLE_HOST_HMAC_SHA256_H

// Host SHA-256 and HMAC-SHA256 for the companion password gate. The firmware
// adapter in src/FurbleCompanionCrypto.cpp calls mbedTLS, which the host build
// does not link. This is the same primitive kept in one place so the auth
// state machine test and the GATT test cannot drift apart. The auth test pins
// it against an RFC 4231 vector and the shared golden fixture.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "FurbleCompanionAuth.h"

namespace FurbleHostCrypto {

class Sha256 {
 public:
  Sha256() {
    m_State = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
               0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  }

  void update(const uint8_t *data, size_t len) {
    while (len != 0) {
      const size_t copy = std::min(len, sizeof(m_Buffer) - m_BufferLength);
      std::memcpy(m_Buffer.data() + m_BufferLength, data, copy);
      m_BufferLength += copy;
      m_BitLength += static_cast<uint64_t>(copy) * 8;
      data += copy;
      len -= copy;
      if (m_BufferLength == sizeof(m_Buffer)) {
        transform(m_Buffer.data());
        m_BufferLength = 0;
      }
    }
  }

  std::array<uint8_t, 32> finish(void) {
    const uint64_t bitLength = m_BitLength;
    m_Buffer[m_BufferLength++] = 0x80;
    if (m_BufferLength > 56) {
      while (m_BufferLength < 64) {
        m_Buffer[m_BufferLength++] = 0;
      }
      transform(m_Buffer.data());
      m_BufferLength = 0;
    }
    while (m_BufferLength < 56) {
      m_Buffer[m_BufferLength++] = 0;
    }
    for (int index = 7; index >= 0; index--) {
      m_Buffer[m_BufferLength++] = static_cast<uint8_t>(bitLength >> (index * 8));
    }
    transform(m_Buffer.data());

    std::array<uint8_t, 32> digest = {};
    for (size_t index = 0; index < m_State.size(); index++) {
      digest[index * 4] = static_cast<uint8_t>(m_State[index] >> 24);
      digest[index * 4 + 1] = static_cast<uint8_t>(m_State[index] >> 16);
      digest[index * 4 + 2] = static_cast<uint8_t>(m_State[index] >> 8);
      digest[index * 4 + 3] = static_cast<uint8_t>(m_State[index]);
    }
    return digest;
  }

 private:
  static constexpr std::array<uint32_t, 64> K = {
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
      0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
      0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
      0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
      0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
      0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
      0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
      0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
      0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
      0xc67178f2U,
  };

  static uint32_t rotateRight(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32 - count));
  }

  static uint32_t choose(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }

  static uint32_t majority(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
  }

  static uint32_t bigSigma0(uint32_t value) {
    return rotateRight(value, 2) ^ rotateRight(value, 13) ^ rotateRight(value, 22);
  }

  static uint32_t bigSigma1(uint32_t value) {
    return rotateRight(value, 6) ^ rotateRight(value, 11) ^ rotateRight(value, 25);
  }

  static uint32_t smallSigma0(uint32_t value) {
    return rotateRight(value, 7) ^ rotateRight(value, 18) ^ (value >> 3);
  }

  static uint32_t smallSigma1(uint32_t value) {
    return rotateRight(value, 17) ^ rotateRight(value, 19) ^ (value >> 10);
  }

  void transform(const uint8_t *block) {
    std::array<uint32_t, 64> words = {};
    for (size_t index = 0; index < 16; index++) {
      words[index] = (static_cast<uint32_t>(block[index * 4]) << 24)
                     | (static_cast<uint32_t>(block[index * 4 + 1]) << 16)
                     | (static_cast<uint32_t>(block[index * 4 + 2]) << 8)
                     | static_cast<uint32_t>(block[index * 4 + 3]);
    }
    for (size_t index = 16; index < words.size(); index++) {
      words[index] = smallSigma1(words[index - 2]) + words[index - 7]
                     + smallSigma0(words[index - 15]) + words[index - 16];
    }

    uint32_t a = m_State[0];
    uint32_t b = m_State[1];
    uint32_t c = m_State[2];
    uint32_t d = m_State[3];
    uint32_t e = m_State[4];
    uint32_t f = m_State[5];
    uint32_t g = m_State[6];
    uint32_t h = m_State[7];
    for (size_t index = 0; index < words.size(); index++) {
      const uint32_t temp1 = h + bigSigma1(e) + choose(e, f, g) + K[index] + words[index];
      const uint32_t temp2 = bigSigma0(a) + majority(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    m_State[0] += a;
    m_State[1] += b;
    m_State[2] += c;
    m_State[3] += d;
    m_State[4] += e;
    m_State[5] += f;
    m_State[6] += g;
    m_State[7] += h;
  }

  std::array<uint32_t, 8> m_State = {};
  std::array<uint8_t, 64> m_Buffer = {};
  size_t m_BufferLength = 0;
  uint64_t m_BitLength = 0;
};

constexpr std::array<uint32_t, 64> Sha256::K;

bool hmacSha256(const uint8_t *key,
                size_t keyLen,
                const uint8_t *message,
                size_t messageLen,
                uint8_t *digest,
                size_t digestLen) {
  if (digest == nullptr || digestLen < Furble::CompanionAuth::HMAC_SIZE
      || (key == nullptr && keyLen != 0) || (message == nullptr && messageLen != 0)) {
    return false;
  }

  std::array<uint8_t, 64> paddedKey = {};
  if (keyLen > paddedKey.size()) {
    Sha256 keyHash;
    keyHash.update(key, keyLen);
    const auto digestKey = keyHash.finish();
    std::copy(digestKey.begin(), digestKey.end(), paddedKey.begin());
  } else if (keyLen != 0) {
    std::memcpy(paddedKey.data(), key, keyLen);
  }

  std::array<uint8_t, 64> innerPad = {};
  std::array<uint8_t, 64> outerPad = {};
  for (size_t index = 0; index < paddedKey.size(); index++) {
    innerPad[index] = paddedKey[index] ^ 0x36;
    outerPad[index] = paddedKey[index] ^ 0x5c;
  }

  Sha256 inner;
  inner.update(innerPad.data(), innerPad.size());
  inner.update(message, messageLen);
  const auto innerDigest = inner.finish();

  Sha256 outer;
  outer.update(outerPad.data(), outerPad.size());
  outer.update(innerDigest.data(), innerDigest.size());
  const auto fullDigest = outer.finish();
  std::copy(fullDigest.begin(), fullDigest.end(), digest);
  return true;
}

}  // namespace FurbleHostCrypto

#endif
