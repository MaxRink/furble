#include "FurbleOTAReplayStore.h"
#include "nvs.h"

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
  int failReadSlot = -1;
  bool failWrite = false;
  int failWriteSlot = -1;
  bool failCommit = false;
  bool tearWrite = false;
  size_t tearLength = JournalReplayStore::RECORD_BYTES;
  size_t truncatedLength = JournalReplayStore::RECORD_BYTES;

  ReadResult read(uint8_t slot, uint8_t *bytes, size_t length) override {
    if (failRead || (static_cast<int>(slot) == failReadSlot) || slot >= present.size() || bytes == nullptr
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
    if (failWrite || (static_cast<int>(slot) == failWriteSlot) || slot >= present.size() || bytes == nullptr
        || length != JournalReplayStore::RECORD_BYTES) {
      return false;
    }
    pendingSlot = slot;
    pending = true;
    std::memset(staged.data(), 0, staged.size());
    const size_t copied = tearWrite ? (tearLength < length ? tearLength : length) : length;
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
    return !tearWrite;
  }

  void reboot() {
    pending = false;
    failRead = false;
    failReadSlot = -1;
    failWrite = false;
    failWriteSlot = -1;
    failCommit = false;
    tearWrite = false;
    tearLength = JournalReplayStore::RECORD_BYTES;
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

void goldenAndAmbiguousRecords() {
  // External golden bytes for an erased, format-v1 FRJ1 record. The CRC is
  // intentionally a fixed oracle, not produced by the implementation under
  // test. Every single-byte mutation must cease to be a valid initial record.
  constexpr auto golden = [] {
    std::array<uint8_t, JournalReplayStore::RECORD_BYTES> bytes {};
    bytes[0] = 0x46;
    bytes[1] = 0x52;
    bytes[2] = 0x4a;
    bytes[3] = 0x31;
    bytes[4] = 0x01;
    bytes[6] = 0x40;
    // CRC-32 of the preceding 60 fixed bytes, independently generated.
    bytes[60] = 0x32;
    bytes[61] = 0xbf;
    bytes[62] = 0xe1;
    bytes[63] = 0xf4;
    return bytes;
  }();
  FaultBackend backend;
  backend.durable[0] = golden;
  backend.present[0] = true;
  JournalReplayStore store(backend);
  uint32_t floor = 99;
  assert(store.loadFloor(floor) && floor == 0);
  for (size_t index = 0; index < golden.size(); index++) {
    backend.durable[0] = golden;
    backend.durable[0][index] ^= 1;
    assert(!store.loadFloor(floor));
  }

  // Same generation and same record is harmless; divergent equal generations
  // and RFC1982's exact half-range are both deliberately unorderable.
  backend = FaultBackend {};
  seedRecord(backend, 0, 10, 3);
  seedRecord(backend, 1, 10, 3);
  assert(store.loadFloor(floor) && floor == 3);
  seedRecord(backend, 1, 10, 4);
  assert(!store.loadFloor(floor));
  seedRecord(backend, 0, 0, 3);
  seedRecord(backend, 1, 0x80000000U, 4);
  assert(!store.loadFloor(floor));
}

void tornWriteMatrix() {
  const SessionId first = owner(8);
  for (size_t length = 0; length <= JournalReplayStore::RECORD_BYTES; length++) {
    // Reserved -> staged publishes slot 1 after slot 0 is known-good.
    FaultBackend backend;
    JournalReplayStore store(backend);
    assert(store.reserveFloor(0, 7, first));
    backend.tearWrite = true;
    backend.tearLength = length;
    assert(!store.markStaged(first, 7));
    backend.reboot();
    uint32_t floor = 0;
    assert(store.loadFloor(floor) && floor == 7);

    // Staged -> complete publishes slot 0 after slot 1 is known-good.
    FaultBackend secondBackend;
    JournalReplayStore second(secondBackend);
    assert(second.reserveFloor(0, 7, first));
    assert(second.markStaged(first, 7));
    secondBackend.tearWrite = true;
    secondBackend.tearLength = length;
    assert(!second.completeReservation(first, 7));
    secondBackend.reboot();
    assert(second.loadFloor(floor) && floor == 7);
  }
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
  for (int slot = 0; slot < 2; slot++) {
    FaultBackend backend;
    JournalReplayStore store(backend);
    const SessionId first = owner(static_cast<uint8_t>(20 + slot));
    backend.failReadSlot = slot;
    assert(!store.reserveFloor(0, 11, first));
    backend.reboot();
    assert(store.reserveFloor(0, 11, first));
    uint32_t floor = 0;
    assert(store.loadFloor(floor) && floor == 11);
  }
  {
    FaultBackend backend;
    JournalReplayStore store(backend);
    const SessionId first = owner(22);
    assert(store.reserveFloor(0, 11, first));
    backend.failWriteSlot = 1;
    assert(!store.markStaged(first, 11));
    backend.reboot();
    uint32_t floor = 0;
    assert(store.loadFloor(floor) && floor == 11);
  }
  {
    FaultBackend backend;
    JournalReplayStore store(backend);
    const SessionId first = owner(23);
    assert(store.reserveFloor(0, 11, first));
    assert(store.markStaged(first, 11));
    backend.failWriteSlot = 0;
    assert(!store.completeReservation(first, 11));
    backend.reboot();
    uint32_t floor = 0;
    assert(store.loadFloor(floor) && floor == 11);
  }
}

void randomizedStateMachine() {
  FaultBackend backend;
  std::mt19937 rng(0x5eed1234U);
  uint32_t floor = 0;
  bool reserved = false;
  bool staged = false;
  SessionId currentOwner {};
  uint32_t currentCounter = 0;
  for (unsigned step = 0; step < 100000; step++) {
    if ((step % 97U) == 0U) {
      backend.reboot();
    }
    // Reconstruct the production object after each reboot boundary. The
    // logical model below is intentionally independent of the journal bytes.
    JournalReplayStore store(backend);
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

void transactionalNvsBackend() {
  FakeNvs::reset();
  Furble::OTA::NvsReplayJournalBackend nvs;
  assert(nvs.begin());
  JournalReplayStore store(nvs);
  const SessionId first = owner(31);
  assert(store.reserveFloor(0, 17, first));
  FakeNvs::reboot();
  nvs.end();
  assert(nvs.begin());
  JournalReplayStore rebooted(nvs);
  uint32_t floor = 0;
  assert(rebooted.loadFloor(floor) && floor == 17);

  // A set_blob is invisible until commit, and a failed commit is discarded.
  std::array<uint8_t, JournalReplayStore::RECORD_BYTES> scratch {};
  assert(nvs.write(1, scratch.data(), scratch.size()));
  FakeNvs::reboot();
  assert(nvs.read(1, scratch.data(), scratch.size()) == ReplayJournalBackend::ReadResult::Missing);
  FakeNvs::failNextCommit();
  assert(!rebooted.markStaged(first, 17));
  FakeNvs::reboot();
  assert(rebooted.loadFloor(floor) && floor == 17);

  // A present but truncated blob is corruption and must not be accepted.
  FakeNvs::truncateSlot(0, 7);
  assert(!rebooted.loadFloor(floor));
  nvs.end();
}

}  // namespace

int main() {
  basicTransitions();
  commitAndCorruptionRecovery();
  generationRollover();
  goldenAndAmbiguousRecords();
  tornWriteMatrix();
  faultBoundaries();
  randomizedStateMachine();
  transactionalNvsBackend();
  std::cout << "ota replay store: transitions, recovery, rollover, faults, and 100k property steps passed\n";
  return 0;
}
