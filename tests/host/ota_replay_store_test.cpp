#include "FurbleOTAReplayStore.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>

using Furble::OTA::JournalReplayStore;
using Furble::OTA::ReplayJournalBackend;
using Furble::OTA::MQTT::SessionId;

namespace {

class FaultBackend final : public ReplayJournalBackend {
 public:
  std::array<std::array<uint8_t, JournalReplayStore::RECORD_BYTES>, 2> durable {};
  std::array<bool, 2> present {};
  bool failRead = false;
  bool failWrite = false;
  bool failCommit = false;
  bool tearWrite = false;
  size_t truncatedLength = JournalReplayStore::RECORD_BYTES;

  ReadResult read(uint8_t slot, uint8_t *bytes, size_t length) override {
    if (failRead || slot >= present.size() || bytes == nullptr
        || length != JournalReplayStore::RECORD_BYTES) {
      return ReadResult::Failed;
    }
    if (!present[slot]) {
      return ReadResult::Missing;
    }
    std::memset(bytes, 0, length);
    const size_t copied = truncatedLength < length ? truncatedLength : length;
    std::memcpy(bytes, durable[slot].data(), copied);
    return ReadResult::Ok;
  }

  bool write(uint8_t slot, const uint8_t *bytes, size_t length) override {
    if (failWrite || slot >= present.size() || bytes == nullptr
        || length != JournalReplayStore::RECORD_BYTES) {
      return false;
    }
    pendingSlot = slot;
    pending = true;
    std::memset(staged.data(), 0, staged.size());
    const size_t copied = tearWrite ? staged.size() / 2 : length;
    std::memcpy(staged.data(), bytes, copied);
    return true;
  }

  bool commit() override {
    if (!pending || failCommit) {
      pending = false;
      return false;
    }
    durable[pendingSlot] = staged;
    present[pendingSlot] = true;
    pending = false;
    return true;
  }

  void reboot() {
    pending = false;
    failRead = false;
    failWrite = false;
    failCommit = false;
    tearWrite = false;
    truncatedLength = JournalReplayStore::RECORD_BYTES;
  }

  void corrupt(uint8_t slot, size_t offset) {
    assert(slot < present.size() && present[slot]);
    durable[slot][offset] ^= 0x5a;
  }

 private:
  uint8_t pendingSlot = 0;
  bool pending = false;
  std::array<uint8_t, JournalReplayStore::RECORD_BYTES> staged {};
};

SessionId owner(uint8_t value) {
  SessionId result {};
  result[0] = value;
  result[15] = static_cast<uint8_t>(value ^ 0xa5U);
  return result;
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

void write32(uint8_t *bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

void seedRecord(FaultBackend &backend, uint8_t slot, uint32_t generation, uint32_t floor) {
  auto &bytes = backend.durable[slot];
  bytes.fill(0);
  bytes[0] = 'F';
  bytes[1] = 'R';
  bytes[2] = 'J';
  bytes[3] = '1';
  bytes[4] = 1;
  bytes[6] = JournalReplayStore::RECORD_BYTES;
  write32(bytes.data() + 8, generation);
  write32(bytes.data() + 12, floor);
  write32(bytes.data() + 60, crc32(bytes.data(), 60));
  backend.present[slot] = true;
}

void basicTransitions() {
  FaultBackend backend;
  JournalReplayStore store(backend);
  uint32_t floor = 99;
  assert(store.loadFloor(floor) && floor == 0);
  const SessionId first = owner(1);
  const SessionId second = owner(2);
  assert(store.reserveFloor(0, 5, first));
  assert(store.loadFloor(floor) && floor == 5);
  assert(!store.reserveFloor(0, 8, second));
  assert(!store.markStaged(second, 5));
  assert(store.markStaged(first, 5));
  assert(!store.completeReservation(second, 5));
  assert(store.completeReservation(first, 5));
  assert(store.loadFloor(floor) && floor == 5);
  assert(!store.completeReservation(first, 5));
  assert(store.reserveFloor(5, 9, second));
  assert(store.abandonReservation(second, 9));
  assert(store.loadFloor(floor) && floor == 9);
  assert(store.reserveFloor(9, UINT32_MAX, first));
  assert(store.recoverAbandonedReservation());
  assert(store.loadFloor(floor) && floor == UINT32_MAX);
  assert(!store.reserveFloor(UINT32_MAX, 1, second));
}

void commitAndCorruptionRecovery() {
  const SessionId first = owner(3);
  FaultBackend backend;
  JournalReplayStore store(backend);
  backend.failCommit = true;
  assert(!store.reserveFloor(0, 7, first));
  backend.reboot();
  uint32_t floor = 42;
  assert(store.loadFloor(floor) && floor == 0);
  backend.failWrite = true;
  assert(!store.reserveFloor(0, 7, first));
  backend.reboot();
  assert(store.loadFloor(floor) && floor == 0);
  assert(store.reserveFloor(0, 7, first));
  assert(store.markStaged(first, 7));
  backend.corrupt(1, 8);  // newest generation: fall back to the reserved slot
  backend.reboot();
  assert(store.loadFloor(floor) && floor == 7);
  assert(store.markStaged(first, 7));
  backend.truncatedLength = 12;
  backend.corrupt(0, 12);
  assert(!store.loadFloor(floor));  // both present slots are now invalid
}

void generationRollover() {
  FaultBackend backend;
  seedRecord(backend, 0, UINT32_MAX - 1U, 4);
  JournalReplayStore store(backend);
  const SessionId first = owner(4);
  assert(store.reserveFloor(4, 5, first));  // generation UINT32_MAX
  assert(store.markStaged(first, 5));        // generation zero
  uint32_t floor = 0;
  assert(store.loadFloor(floor) && floor == 5);
  assert(store.completeReservation(first, 5));
  assert(store.loadFloor(floor) && floor == 5);
}

void faultBoundaries() {
  for (unsigned boundary = 0; boundary < 3; boundary++) {
    FaultBackend backend;
    JournalReplayStore store(backend);
    const SessionId first = owner(static_cast<uint8_t>(10 + boundary));
    if (boundary == 0) {
      backend.failRead = true;
    } else if (boundary == 1) {
      backend.failWrite = true;
    } else {
      backend.failCommit = true;
    }
    assert(!store.reserveFloor(0, 11, first));
    backend.reboot();
    uint32_t floor = 99;
    assert(store.loadFloor(floor) && floor == 0);
    assert(store.reserveFloor(0, 11, first));
  }
}

void randomizedStateMachine() {
  FaultBackend backend;
  JournalReplayStore store(backend);
  std::mt19937 rng(0x5eed1234U);
  uint32_t floor = 0;
  bool reserved = false;
  bool staged = false;
  SessionId currentOwner {};
  uint32_t currentCounter = 0;
  for (unsigned step = 0; step < 100000; step++) {
    const unsigned operation = rng() % 6U;
    const SessionId candidate = owner(static_cast<uint8_t>((rng() % 250U) + 1U));
    const uint32_t candidateCounter = (rng() & 1U) ? currentCounter : (rng() % 1000U) + 1U;
    bool expected = false;
    if (operation == 0) {
      uint32_t loaded = 0;
      expected = store.loadFloor(loaded);
      assert(expected && loaded == floor);
      continue;
    }
    if (operation == 1) {
      const uint32_t next = (rng() & 1U) ? floor + 1U : candidateCounter;
      expected = !reserved && (next > floor);
      assert(store.reserveFloor(floor, next, candidate) == expected);
      if (expected) {
        floor = next;
        reserved = true;
        staged = false;
        currentOwner = candidate;
        currentCounter = next;
      }
    } else if (operation == 2) {
      expected = reserved && !staged && (candidate == currentOwner)
                && (candidateCounter == currentCounter);
      assert(store.markStaged(candidate, candidateCounter) == expected);
      if (expected) {
        staged = true;
      }
    } else if (operation == 3) {
      expected = reserved && staged && (candidate == currentOwner)
                && (candidateCounter == currentCounter);
      assert(store.completeReservation(candidate, candidateCounter) == expected);
      if (expected) {
        reserved = false;
        staged = false;
      }
    } else if (operation == 4) {
      expected = reserved && (candidate == currentOwner) && (candidateCounter == currentCounter);
      assert(store.abandonReservation(candidate, candidateCounter) == expected);
      if (expected) {
        reserved = false;
        staged = false;
      }
    } else {
      expected = true;
      assert(store.recoverAbandonedReservation());
      if (reserved) {
        reserved = false;
        staged = false;
      }
    }
    uint32_t loaded = 0;
    assert(store.loadFloor(loaded) && loaded == floor);
  }
}

}  // namespace

int main() {
  basicTransitions();
  commitAndCorruptionRecovery();
  generationRollover();
  faultBoundaries();
  randomizedStateMachine();
  std::cout << "ota replay store: transitions, recovery, rollover, faults, and 100k property steps passed\n";
  return 0;
}
