#include "FurbleOTAPartitionSink.h"

#include <cstring>

namespace Furble {
namespace OTA {

namespace {

constexpr uint32_t SHA256_K[] = {
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

uint32_t rotateRight(uint32_t value, unsigned count) {
  return (value >> count) | (value << (32U - count));
}

uint32_t readWord(const uint8_t *bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16)
         | (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

void writeWord(uint8_t *bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value >> 24);
  bytes[1] = static_cast<uint8_t>(value >> 16);
  bytes[2] = static_cast<uint8_t>(value >> 8);
  bytes[3] = static_cast<uint8_t>(value);
}

bool allZero(const MQTT::Digest &digest) {
  for (const uint8_t byte : digest) {
    if (byte != 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

PartitionSink::PartitionSink(PartitionTarget &target, ManifestVerifier &verifier)
    : m_Target(target), m_Verifier(verifier) {
  resetState();
}

bool PartitionSink::begin(const MQTT::Manifest &manifest) {
  if (m_Started) {
    abort();
  }
  if (!validManifest(manifest)) {
    return false;
  }

  m_ImageSize = manifest.imageSize;
  m_PartitionSize = manifest.partitionSize;
  m_NextOffset = 0;
  m_Finalized = false;
  m_Activated = false;
  hashInit(m_Hash);
  m_Started = true;
  if (!m_Target.begin(manifest.imageSize, manifest.partitionSize)) {
    abort();
    return false;
  }
  return true;
}

bool PartitionSink::write(uint32_t offset, const uint8_t *data, size_t length) {
  if (!m_Started || m_Finalized || (data == nullptr) || (length == 0) || (offset != m_NextOffset)
      || (offset > m_ImageSize) || (length > (m_ImageSize - offset))) {
    if (m_Started && !m_Finalized) {
      abort();
    }
    return false;
  }
  if (m_Target.write(offset, data, length) != length) {
    abort();
    return false;
  }
  hashUpdate(m_Hash, data, length);
  m_NextOffset += static_cast<uint32_t>(length);
  return true;
}

bool PartitionSink::matches(uint32_t offset,
                            const uint8_t *data,
                            size_t length,
                            uint32_t checksum) {
  if (!m_Started || m_Finalized || (data == nullptr) || (length == 0)
      || (MQTT::crc32(data, length) != checksum) || (offset > m_NextOffset)
      || (length > (m_NextOffset - offset))) {
    return false;
  }
  return m_Target.matches(offset, data, length);
}

bool PartitionSink::finalize(const MQTT::Manifest &manifest) {
  if (!m_Started || m_Finalized || (manifest.imageSize != m_ImageSize)
      || (manifest.partitionSize != m_PartitionSize) || (m_NextOffset != m_ImageSize)) {
    if (m_Started && !m_Finalized) {
      abort();
    }
    return false;
  }
  const MQTT::Digest digest = hashFinal(m_Hash);
  if ((digest != manifest.digest) || !m_Verifier.verify(manifest) || !m_Target.end()) {
    abort();
    return false;
  }
  m_Finalized = true;
  return true;
}

bool PartitionSink::activate() {
  if (m_Activated) {
    return true;
  }
  if (!m_Started || !m_Finalized || !m_Target.activate()) {
    if (m_Started) {
      abort();
    }
    return false;
  }
  m_Started = false;
  m_Activated = true;
  return true;
}

void PartitionSink::abort() {
  if (m_Started) {
    m_Target.abort();
  }
  resetState();
}

bool PartitionSink::validManifest(const MQTT::Manifest &manifest) const {
  return (manifest.imageSize != 0) && (manifest.partitionSize >= manifest.imageSize)
         && (manifest.partitionSize <= MQTT::MAX_IMAGE_BYTES) && !allZero(manifest.digest);
}

void PartitionSink::resetState() {
  m_Hash = {};
  m_ImageSize = 0;
  m_PartitionSize = 0;
  m_NextOffset = 0;
  m_Started = false;
  m_Finalized = false;
  m_Activated = false;
}

void PartitionSink::hashInit(Sha256State &state) {
  state = {};
  state.words[0] = 0x6a09e667U;
  state.words[1] = 0xbb67ae85U;
  state.words[2] = 0x3c6ef372U;
  state.words[3] = 0xa54ff53aU;
  state.words[4] = 0x510e527fU;
  state.words[5] = 0x9b05688cU;
  state.words[6] = 0x1f83d9abU;
  state.words[7] = 0x5be0cd19U;
}

void PartitionSink::hashUpdate(Sha256State &state, const uint8_t *data, size_t length) {
  while (length != 0) {
    const size_t copy = (sizeof(state.buffer) - state.bufferLength) < length
                            ? (sizeof(state.buffer) - state.bufferLength)
                            : length;
    std::memcpy(state.buffer + state.bufferLength, data, copy);
    state.bufferLength += copy;
    state.bitLength += static_cast<uint64_t>(copy) * 8U;
    data += copy;
    length -= copy;
    if (state.bufferLength != sizeof(state.buffer)) {
      continue;
    }

    uint32_t schedule[64] = {};
    for (size_t index = 0; index < 16; index++) {
      schedule[index] = readWord(state.buffer + (index * 4));
    }
    for (size_t index = 16; index < 64; index++) {
      const uint32_t s0 = rotateRight(schedule[index - 15], 7)
                          ^ rotateRight(schedule[index - 15], 18) ^ (schedule[index - 15] >> 3);
      const uint32_t s1 = rotateRight(schedule[index - 2], 17)
                          ^ rotateRight(schedule[index - 2], 19) ^ (schedule[index - 2] >> 10);
      schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
    }

    uint32_t a = state.words[0];
    uint32_t b = state.words[1];
    uint32_t c = state.words[2];
    uint32_t d = state.words[3];
    uint32_t e = state.words[4];
    uint32_t f = state.words[5];
    uint32_t g = state.words[6];
    uint32_t h = state.words[7];
    for (size_t index = 0; index < 64; index++) {
      const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
      const uint32_t choose = (e & f) ^ ((~e) & g);
      const uint32_t temp1 = h + s1 + choose + SHA256_K[index] + schedule[index];
      const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state.words[0] += a;
    state.words[1] += b;
    state.words[2] += c;
    state.words[3] += d;
    state.words[4] += e;
    state.words[5] += f;
    state.words[6] += g;
    state.words[7] += h;
    state.bufferLength = 0;
  }
}

MQTT::Digest PartitionSink::hashFinal(const Sha256State &state) {
  Sha256State copy = state;
  const uint64_t bitLength = state.bitLength;
  uint8_t padding[128] = {};
  padding[0] = 0x80;
  const size_t paddingLength =
      copy.bufferLength < 56 ? 56 - copy.bufferLength : 120 - copy.bufferLength;
  hashUpdate(copy, padding, paddingLength);
  uint8_t lengthBytes[8] = {};
  for (size_t index = 0; index < sizeof(lengthBytes); index++) {
    lengthBytes[sizeof(lengthBytes) - index - 1] = static_cast<uint8_t>(bitLength >> (index * 8));
  }
  hashUpdate(copy, lengthBytes, sizeof(lengthBytes));
  MQTT::Digest digest {};
  for (size_t index = 0; index < 8; index++) {
    writeWord(digest.data() + (index * 4), copy.words[index]);
  }
  return digest;
}

}  // namespace OTA
}  // namespace Furble
