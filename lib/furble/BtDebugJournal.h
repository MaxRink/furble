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
  static constexpr size_t PAYLOAD_BYTES = 64;

  uint64_t timestamp_ms = 0;
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
  uint8_t payload[PAYLOAD_BYTES] = {};
};

/** Human-readable GAP reason. Unknown values are reported as "unknown". */
const char *btGapReasonName(int reason);

/** A fixed-capacity ring used by console diagnostics. It never writes logs while recording. */
class BtDebugJournal {
 public:
  // Keep the console-only ring below 64 KiB even with the identity and UUID
  // fields. This is deliberately independent of the board's heap layout.
  static constexpr size_t MAX_EVENTS = 128;
  using Emit = void (*)(const BtDebugEvent &, void *context);

  static BtDebugJournal &instance();

  bool setEnabled(bool enabled);
  bool isEnabled() const;
  void clear();
  void record(const BtDebugEvent &event);
  size_t size() const;

  /** Emit at most count newest events, in chronological order. Zero means all. */
  size_t dump(size_t count, Emit emit, void *context) const;

  /** Emit at most count events not previously emitted by drain, oldest first. */
  size_t drain(size_t count, Emit emit, void *context);

 private:
  BtDebugJournal();
  BtDebugJournal(const BtDebugJournal &) = delete;
  BtDebugJournal &operator=(const BtDebugJournal &) = delete;

  mutable std::mutex m_Mutex;
  BtDebugEvent m_Events[MAX_EVENTS] = {};
  size_t m_Count = 0;
  uint64_t m_WriteSequence = 0;
  uint64_t m_LiveSequence = 0;
  bool m_Enabled = false;
};

}  // namespace Furble

#endif
