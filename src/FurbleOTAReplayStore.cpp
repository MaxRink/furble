#include "FurbleOTAReplayStore.h"

#include <cstring>

namespace Furble {
namespace OTA {

namespace {

constexpr uint8_t MAGIC[] = {'F', 'R', 'J', '1'};
constexpr size_t CRC_OFFSET = 60;

uint16_t read16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) | static_cast<uint16_t>(bytes[1] << 8);
}

uint32_t read32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8)
         | (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

void write16(uint8_t *bytes, uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
}

void write32(uint8_t *bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t crc32(const uint8_t *bytes, size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t index = 0; index < length; index++) {
    crc ^= bytes[index];
    for (unsigned bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

bool allZero(const uint8_t *bytes, size_t length) {
  for (size_t index = 0; index < length; index++) {
    if (bytes[index] != 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

JournalReplayStore::JournalReplayStore(ReplayJournalBackend &backend) : m_Backend(backend) {}

bool JournalReplayStore::loadFloor(uint32_t &floor) {
  Latest latest;
  if (!readLatest(latest)) {
    return false;
  }
  floor = latest.present ? latest.record.floor : 0;
  return true;
}

bool JournalReplayStore::reserveFloor(uint32_t expectedFloor,
                                      uint32_t nextFloor,
                                      const MQTT::SessionId &owner) {
  if (owner == MQTT::SessionId {} || nextFloor <= expectedFloor) {
    // Rollback counters are not serial numbers.  Once UINT32_MAX is consumed
    // no later counter can be represented, which is safe exhaustion rather
    // than permission to wrap back to an old image.
    return false;
  }
  Latest latest;
  if (!readLatest(latest)) {
    return false;
  }
  const uint32_t floor = latest.present ? latest.record.floor : 0;
  if (floor != expectedFloor || (latest.present && active(latest.record))) {
    return false;
  }
  Record next;
  next.generation = latest.present ? latest.record.generation + 1U : 1U;
  next.floor = nextFloor;
  next.counter = nextFloor;
  next.owner = owner;
  next.state = ReservationState::Reserved;
  return publish(latest, next);
}

bool JournalReplayStore::markStaged(const MQTT::SessionId &owner, uint32_t counter) {
  Latest latest;
  if (!readLatest(latest) || !latest.present || (latest.record.state != ReservationState::Reserved)
      || (latest.record.counter != counter) || !sameOwner(latest.record.owner, owner)) {
    return false;
  }
  Record next = latest.record;
  next.generation++;
  next.state = ReservationState::Staged;
  return publish(latest, next);
}

bool JournalReplayStore::completeReservation(const MQTT::SessionId &owner, uint32_t counter) {
  Latest latest;
  if (!readLatest(latest) || !latest.present || (latest.record.state != ReservationState::Staged)
      || (latest.record.counter != counter) || !sameOwner(latest.record.owner, owner)) {
    return false;
  }
  Record next = latest.record;
  next.generation++;
  next.state = ReservationState::None;
  next.counter = 0;
  next.owner = MQTT::SessionId {};
  return publish(latest, next);
}

bool JournalReplayStore::abandonReservation(const MQTT::SessionId &owner, uint32_t counter) {
  Latest latest;
  if (!readLatest(latest) || !latest.present || !active(latest.record)
      || (latest.record.counter != counter) || !sameOwner(latest.record.owner, owner)) {
    return false;
  }
  Record next = latest.record;
  next.generation++;
  next.state = ReservationState::None;
  next.counter = 0;
  next.owner = MQTT::SessionId {};
  return publish(latest, next);
}

bool JournalReplayStore::recoverAbandonedReservation() {
  Latest latest;
  if (!readLatest(latest)) {
    return false;
  }
  if (!latest.present || !active(latest.record)) {
    return true;
  }
  Record next = latest.record;
  next.generation++;
  next.state = ReservationState::None;
  next.counter = 0;
  next.owner = MQTT::SessionId {};
  return publish(latest, next);
}

bool JournalReplayStore::readLatest(Latest &latest) {
  latest = Latest {};
  bool sawBytes = false;
  for (uint8_t slot = 0; slot < SLOT_COUNT; slot++) {
    std::array<uint8_t, RECORD_BYTES> bytes {};
    const ReplayJournalBackend::ReadResult result =
        m_Backend.read(slot, bytes.data(), bytes.size());
    if (result == ReplayJournalBackend::ReadResult::Failed) {
      return false;
    }
    if (result == ReplayJournalBackend::ReadResult::Missing) {
      continue;
    }
    sawBytes = true;
    Record candidate;
    if (!validRecord(bytes.data(), bytes.size(), candidate)) {
      continue;
    }
    candidate.slot = slot;
    if (!latest.present) {
      latest.record = candidate;
      latest.present = true;
      continue;
    }
    const uint32_t delta = candidate.generation - latest.record.generation;
    if (delta == 0U) {
      if (!recordsEqual(candidate, latest.record)) {
        // Equal-generation divergent records cannot be ordered safely.  Do
        // not choose one and risk lowering the anti-rollback floor.
        return false;
      }
    } else if (delta == 0x80000000U) {
      // RFC 1982's exact half-range is ambiguous in both directions.  Slot
      // order must never break that tie because it could lower the floor.
      return false;
    } else if (newerGeneration(candidate.generation, latest.record.generation)) {
      latest.record = candidate;
    }
  }
  // An erased pair is the only valid initial state. Corrupt or torn bytes in
  // both slots must fail closed rather than silently resetting the floor.
  return latest.present || !sawBytes;
}

bool JournalReplayStore::publish(const Latest &latest, const Record &next) {
  std::array<uint8_t, RECORD_BYTES> bytes {};
  const uint8_t target = latest.present ? static_cast<uint8_t>(latest.record.slot ^ 1U) : 0;
  Record encoded = next;
  encoded.slot = target;
  encodeRecord(encoded, bytes.data(), bytes.size());
  return m_Backend.write(target, bytes.data(), bytes.size());
}

bool JournalReplayStore::newerGeneration(uint32_t candidate, uint32_t current) {
  const uint32_t delta = candidate - current;
  return (delta != 0U) && (delta < 0x80000000U);
}

bool JournalReplayStore::recordsEqual(const Record &a, const Record &b) {
  return (a.generation == b.generation) && (a.floor == b.floor) && (a.counter == b.counter)
         && (a.owner == b.owner) && (a.state == b.state);
}

bool JournalReplayStore::sameOwner(const MQTT::SessionId &a, const MQTT::SessionId &b) {
  return a == b && a != MQTT::SessionId {};
}

bool JournalReplayStore::active(const Record &record) {
  return (record.state == ReservationState::Reserved) || (record.state == ReservationState::Staged);
}

bool JournalReplayStore::validRecord(const uint8_t *bytes, size_t length, Record &out) {
  if ((bytes == nullptr) || (length != RECORD_BYTES)
      || (std::memcmp(bytes, MAGIC, sizeof(MAGIC)) != 0) || (read16(bytes + 4) != FORMAT_VERSION)
      || (read16(bytes + 6) != RECORD_BYTES)
      || (read32(bytes + CRC_OFFSET) != crc32(bytes, CRC_OFFSET)) || !allZero(bytes + 17, 3)
      || !allZero(bytes + 40, 20)) {
    return false;
  }
  const uint8_t state = bytes[16];
  if ((state < static_cast<uint8_t>(ReservationState::None))
      || (state > static_cast<uint8_t>(ReservationState::Staged))) {
    return false;
  }
  out.generation = read32(bytes + 8);
  out.floor = read32(bytes + 12);
  out.state = static_cast<ReservationState>(state);
  out.counter = read32(bytes + 20);
  std::memcpy(out.owner.data(), bytes + 24, out.owner.size());
  if (out.state == ReservationState::None) {
    return (out.counter == 0) && (out.owner == MQTT::SessionId {});
  }
  return (out.floor != 0) && (out.counter == out.floor) && (out.owner != MQTT::SessionId {});
}

void JournalReplayStore::encodeRecord(const Record &record, uint8_t *bytes, size_t length) {
  std::memset(bytes, 0, length);
  std::memcpy(bytes, MAGIC, sizeof(MAGIC));
  write16(bytes + 4, FORMAT_VERSION);
  write16(bytes + 6, RECORD_BYTES);
  write32(bytes + 8, record.generation);
  write32(bytes + 12, record.floor);
  bytes[16] = static_cast<uint8_t>(record.state);
  write32(bytes + 20, record.counter);
  std::memcpy(bytes + 24, record.owner.data(), record.owner.size());
  write32(bytes + CRC_OFFSET, crc32(bytes, CRC_OFFSET));
}

}  // namespace OTA
}  // namespace Furble
