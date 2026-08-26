#ifndef FURBLE_OTA_REPLAY_STORE_H
#define FURBLE_OTA_REPLAY_STORE_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "FurbleOTAMQTT.h"

namespace Furble {
namespace OTA {

/**
 * The two-slot journal stores only the anti-rollback floor and its one
 * outstanding owner. The backend is deliberately smaller than an NVS or
 * flash API: reads return persisted bytes and write attempts one complete
 * inactive slot. A failed or torn write may leave that slot invalid, but it
 * must never modify the other slot. Callers serialize every
 * JournalReplayStore operation (including calls made from callbacks); the
 * class is not a mutex and does not make a non-serialized backend safe.
 */
class ReplayJournalBackend {
 public:
  enum class ReadResult : uint8_t {
    Ok,
    Missing,
    Failed,
  };

  virtual ~ReplayJournalBackend() = default;
  virtual ReadResult read(uint8_t slot, uint8_t *bytes, size_t length) = 0;
  /** Persist one complete fixed-size slot, or report a failed or torn attempt. */
  virtual bool write(uint8_t slot, const uint8_t *bytes, size_t length) = 0;
};

/**
 * Fixed-record, two-slot durable implementation of MQTT::ReplayStore.
 *
 * Record format is versioned and CRC protected.  An invalid newest slot never
 * hides an older valid slot.  If bytes exist in both slots but neither is
 * valid, operations fail closed; an erased pair is the one documented initial
 * state and returns floor zero.  Rollback counters are intentionally not
 * wrap-safe: accepting a wrapped counter would make an old signed image
 * reusable.  Generation numbers are serial-number ordered and may wrap.
 */
class JournalReplayStore final: public MQTT::ReplayStore {
 public:
  static constexpr uint8_t SLOT_COUNT = 2;
  static constexpr size_t RECORD_BYTES = 64;
  static constexpr uint16_t FORMAT_VERSION = 1;

  explicit JournalReplayStore(ReplayJournalBackend &backend);

  bool loadFloor(uint32_t &floor) override;
  bool reserveFloor(uint32_t expectedFloor,
                    uint32_t nextFloor,
                    const MQTT::SessionId &owner) override;
  bool markStaged(const MQTT::SessionId &owner, uint32_t counter) override;
  bool completeReservation(const MQTT::SessionId &owner, uint32_t counter) override;
  bool abandonReservation(const MQTT::SessionId &owner, uint32_t counter) override;
  bool recoverAbandonedReservation() override;

 private:
  enum class ReservationState : uint8_t {
    None = 0,
    Reserved = 1,
    Staged = 2,
  };

  struct Record {
    uint32_t generation = 0;
    uint32_t floor = 0;
    uint32_t counter = 0;
    MQTT::SessionId owner {};
    ReservationState state = ReservationState::None;
    uint8_t slot = 0;
  };

  struct Latest {
    Record record {};
    bool present = false;
  };

  bool readLatest(Latest &latest);
  bool publish(const Latest &latest, const Record &next);
  static bool newerGeneration(uint32_t candidate, uint32_t current);
  static bool recordsEqual(const Record &a, const Record &b);
  static bool sameOwner(const MQTT::SessionId &a, const MQTT::SessionId &b);
  static bool active(const Record &record);
  static bool validRecord(const uint8_t *bytes, size_t length, Record &out);
  static void encodeRecord(const Record &record, uint8_t *bytes, size_t length);

  ReplayJournalBackend &m_Backend;
};

/**
 * ESP-IDF NVS backend. Each write updates exactly one NVS blob and calls
 * nvs_commit before returning. NVS does not provide a transaction spanning
 * both slots, so recovery relies on inactive-slot ordering and record CRC.
 * The application must initialize NVS before begin().
 */
class NvsReplayJournalBackend final: public ReplayJournalBackend {
 public:
  NvsReplayJournalBackend();
  ~NvsReplayJournalBackend() override;

  bool begin(const char *namespaceName = "furble_ota");
  void end();

  ReadResult read(uint8_t slot, uint8_t *bytes, size_t length) override;
  bool write(uint8_t slot, const uint8_t *bytes, size_t length) override;

 private:
  uint32_t m_Handle;
  bool m_Started;
  char m_Namespace[16];
};

}  // namespace OTA
}  // namespace Furble

#endif
