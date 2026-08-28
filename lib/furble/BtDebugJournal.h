#ifndef FURBLE_BT_DEBUG_JOURNAL_H
#define FURBLE_BT_DEBUG_JOURNAL_H

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace Furble {

/** The event families emitted by the developer BLE diagnostics. */
enum class BtDebugEventKind : uint8_t {
  GAP_CONNECT,
  GAP_DISCONNECT,
  GAP_CONNECT_FAILED,
  SCAN,
  CONNECTION_PARAMS,
  SECURITY,
  GATT,
};

/** A bounded, allocation-free diagnostic event. Empty fields are represented by zeroes. */
struct BtDebugEvent {
  static constexpr size_t ADDRESS_BYTES = 18;
  static constexpr size_t UUID_BYTES = 40;
  static constexpr size_t TEXT_BYTES = 32;
  // Keep journal hex output and retained payload prefixes bounded. The full
  // wire length remains in payload_length and payload_truncated records loss.
  static constexpr size_t PAYLOAD_BYTES = 24;

  uint64_t timestamp_ms = 0;
  uint64_t sequence = 0;
  uint32_t session_id = 0;
  uint32_t attempt_id = 0;
  BtDebugEventKind kind = BtDebugEventKind::GATT;
  uint64_t generation = 0;
  int32_t reason = 0;
  uint8_t address_type = 0xff;
  uint8_t identity_type = 0xff;
  uint8_t key_size = 0;
  bool success = false;
  bool response = false;
  bool encrypted = false;
  bool authenticated = false;
  bool bonded = false;
  bool physical = false;
  bool logical = false;
  bool begin = false;
  bool payload_truncated = false;
  int8_t rssi = 0;
  uint16_t duration_ms = 0;
  uint16_t interval_before = 0;
  uint16_t latency_before = 0;
  uint16_t timeout_before = 0;
  uint16_t interval_after = 0;
  uint16_t latency_after = 0;
  uint16_t timeout_after = 0;
  uint16_t payload_length = 0;
  char address[ADDRESS_BYTES] = {};
  char identity[ADDRESS_BYTES] = {};
  char service_uuid[UUID_BYTES] = {};
  char characteristic_uuid[UUID_BYTES] = {};
  char operation[TEXT_BYTES] = {};
  char owner[TEXT_BYTES] = {};
  char state[TEXT_BYTES] = {};
  char result[TEXT_BYTES] = {};
  char reason_text[TEXT_BYTES] = {};
  char name[TEXT_BYTES] = {};
  char manufacturer[TEXT_BYTES] = {};
  uint8_t payload[PAYLOAD_BYTES] = {};
};

/** Human-readable GAP reason. Unknown values are reported as "unknown". */
const char *btGapReasonName(int reason);

/** A fixed-capacity ring used by console diagnostics. It never writes logs while recording. */
class BtDebugJournal {
 public:
  // S3 builds configured for PSRAM request the larger ring. Allocation still
  // checks the capability heap at runtime because a board may lack the chip.
  static constexpr size_t INTERNAL_EVENTS = 32;
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(CONFIG_SPIRAM)
  static constexpr size_t MAX_EVENTS = 128;
#else
  static constexpr size_t MAX_EVENTS = INTERNAL_EVENTS;
#endif
  using Emit = void (*)(const BtDebugEvent &, void *context);

  static BtDebugJournal &instance();
  ~BtDebugJournal();

  bool setEnabled(bool enabled);
  bool isEnabled() const;
  uint32_t nextAttempt();
  uint32_t sessionId() const;
  void clear();
  void record(const BtDebugEvent &event);
  size_t size() const;
  size_t droppedCount() const;
  /** Number of records currently allocated. Useful for proving board fallback. */
  size_t capacity() const;
  /** Bytes held by the allocated record ring, excluding the mutex and counters. */
  size_t storageBytes() const;

  /** Emit at most count newest events, in chronological order. Zero means all. */
  size_t dump(size_t count, Emit emit, void *context) const;

  /** Emit at most count events not previously emitted by drain, oldest first. */
  size_t drain(size_t count, Emit emit, void *context);

 private:
  BtDebugJournal();
  BtDebugJournal(const BtDebugJournal &) = delete;
  BtDebugJournal &operator=(const BtDebugJournal &) = delete;

  // This is the retained representation. BtDebugEvent remains a convenient
  // decoded view for callbacks, but is never stored in the ring. Addresses and
  // UUIDs are binary, text is bounded, and payload is deliberately sampled.
  struct BtDebugRecord {
    uint64_t timestamp_ms = 0;
    uint64_t generation = 0;
    uint32_t attempt_id = 0;
    int32_t reason = 0;
    uint16_t duration_ms = 0;
    uint16_t interval_before = 0;
    uint16_t latency_before = 0;
    uint16_t timeout_before = 0;
    uint16_t interval_after = 0;
    uint16_t latency_after = 0;
    uint16_t timeout_after = 0;
    uint16_t payload_length = 0;
    uint16_t flags = 0;
    uint8_t kind = static_cast<uint8_t>(BtDebugEventKind::GATT);
    uint8_t address_type = 0xff;
    uint8_t identity_type = 0xff;
    uint8_t key_size = 0;
    int8_t rssi = 0;
    uint8_t address[6] = {};
    uint8_t identity[6] = {};
    uint8_t service_uuid_length = 0;
    uint8_t service_uuid[16] = {};
    uint8_t characteristic_uuid_length = 0;
    uint8_t characteristic_uuid[16] = {};
    char operation[12] = {};
    char owner[12] = {};
    char state[12] = {};
    char result[12] = {};
    char reason_text[12] = {};
    char name[16] = {};
    char manufacturer[16] = {};
    uint8_t payload[24] = {};
  };

  static constexpr uint16_t FLAG_SUCCESS = 1U << 0;
  static constexpr uint16_t FLAG_RESPONSE = 1U << 1;
  static constexpr uint16_t FLAG_ENCRYPTED = 1U << 2;
  static constexpr uint16_t FLAG_AUTHENTICATED = 1U << 3;
  static constexpr uint16_t FLAG_BONDED = 1U << 4;
  static constexpr uint16_t FLAG_PHYSICAL = 1U << 5;
  static constexpr uint16_t FLAG_LOGICAL = 1U << 6;
  static constexpr uint16_t FLAG_BEGIN = 1U << 7;
  static constexpr uint16_t FLAG_PAYLOAD_TRUNCATED = 1U << 8;

  static_assert(sizeof(BtDebugRecord) <= 216,
                "journal record must fit the documented per-board budget");

  static void encode(const BtDebugEvent &event, BtDebugRecord &record);
  static void decode(const BtDebugRecord &record,
                     uint64_t sequence,
                     uint32_t session,
                     BtDebugEvent &event);
  bool allocateLocked();
  void releaseLocked();

  mutable std::mutex m_Mutex;
  BtDebugRecord *m_Events = nullptr;
  size_t m_EventCapacity = 0;
  size_t m_Count = 0;
  uint64_t m_WriteSequence = 0;
  uint64_t m_LiveSequence = 0;
  uint32_t m_SessionId = 0;
  uint32_t m_NextAttemptId = 0;
  size_t m_DroppedCount = 0;
  bool m_Enabled = false;
};

}  // namespace Furble

#endif
