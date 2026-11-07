#ifndef FURBLE_CRASH_RECORD_H
#define FURBLE_CRASH_RECORD_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Furble {
namespace CrashRecord {

/**
 * One boot-surviving crash snapshot.
 *
 * The record is a fixed-size POD so it can live in RTC memory without a
 * constructor, an allocator, or a flash write. The CRC covers every byte before
 * the crc field. A caller validates magic, version, size and CRC before it
 * trusts any value.
 */
inline constexpr uint32_t MAGIC = 0x46524348U;  // "FRCH"
inline constexpr uint16_t VERSION = 2;
inline constexpr size_t TASK_BYTES = 16;
inline constexpr size_t ALLOC_FUNCTION_BYTES = 24;
inline constexpr size_t LOG_LINE_BYTES = 96;
inline constexpr size_t LOG_LINES = 4;
inline constexpr uint32_t FLAG_CLEAN = 1U << 0;

struct record_t {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t sequence;
  uint32_t reset_reason;
  uint32_t uptime_s;
  uint32_t heap_free;
  uint32_t heap_largest;
  uint32_t flags;
  char task[TASK_BYTES];
  char log[LOG_LINES][LOG_LINE_BYTES];
  // The last allocation failure, written by the heap's failed-allocation
  // callback just before CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS aborts.
  // alloc_size is zero when no allocation failed.
  uint32_t alloc_size;
  uint32_t alloc_caps;
  char alloc_task[TASK_BYTES];
  char alloc_function[ALLOC_FUNCTION_BYTES];
  uint32_t crc;
};

inline uint32_t crc32(const void *data, size_t length) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < length; index++) {
    crc ^= bytes[index];
    for (int bit = 0; bit < 8; bit++) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

inline uint32_t checksum(const record_t &record) {
  return crc32(&record, offsetof(record_t, crc));
}

inline void finalize(record_t &record) {
  record.magic = MAGIC;
  record.version = VERSION;
  record.size = sizeof(record_t);
  record.task[TASK_BYTES - 1] = '\0';
  for (size_t line = 0; line < LOG_LINES; line++) {
    record.log[line][LOG_LINE_BYTES - 1] = '\0';
  }
  record.alloc_task[TASK_BYTES - 1] = '\0';
  record.alloc_function[ALLOC_FUNCTION_BYTES - 1] = '\0';
  record.crc = checksum(record);
}

/**
 * Record a failed allocation in place. It runs inside the failing
 * allocation, possibly on a small task stack, so it copies only bounded
 * strings and builds no second record.
 */
inline void noteAllocFailure(record_t &record,
                             size_t size,
                             uint32_t caps,
                             const char *function,
                             const char *task) {
  record.alloc_size = static_cast<uint32_t>(size);
  record.alloc_caps = caps;
  std::snprintf(record.alloc_task, TASK_BYTES, "%s", task != nullptr ? task : "");
  std::snprintf(record.alloc_function, ALLOC_FUNCTION_BYTES, "%s",
                function != nullptr ? function : "");
}

/**
 * Carry the allocation failure in failed into a newer snapshot that has none.
 * A periodic snapshot built before the failure must not erase it.
 */
inline void keepAllocFailure(record_t &record, const record_t &failed) {
  if ((failed.alloc_size == 0) || (record.alloc_size != 0)) {
    return;
  }
  record.alloc_size = failed.alloc_size;
  record.alloc_caps = failed.alloc_caps;
  std::memcpy(record.alloc_task, failed.alloc_task, TASK_BYTES);
  std::memcpy(record.alloc_function, failed.alloc_function, ALLOC_FUNCTION_BYTES);
}

inline bool valid(const record_t &record) {
  return record.magic == MAGIC && record.version == VERSION && record.size == sizeof(record_t)
         && record.crc == checksum(record);
}

inline void clear(record_t &record) {
  std::memset(&record, 0, sizeof(record));
}

/**
 * ESP-IDF reset reason values are stable ABI. They are repeated here so the
 * host test and the simulator can name a record without pulling in
 * esp_system.h.
 */
inline const char *resetReasonName(uint32_t reason) {
  switch (reason) {
    case 0:
      return "unknown";
    case 1:
      return "poweron";
    case 2:
      return "external";
    case 3:
      return "software";
    case 4:
      return "panic";
    case 5:
      return "int_wdt";
    case 6:
      return "task_wdt";
    case 7:
      return "other_wdt";
    case 8:
      return "deepsleep";
    case 9:
      return "brownout";
    case 10:
      return "sdio";
    case 11:
      return "usb_uart_chip_reset";
    case 12:
      return "jtag";
    case 13:
      return "efuse";
    case 14:
      return "power_glitch";
    case 15:
      return "cpu_lockup";
    default:
      return "other";
  }
}

inline bool clean(const record_t &record) {
  return (record.flags & FLAG_CLEAN) != 0;
}

/** Summary buffer size that holds every field at its widest. */
inline constexpr size_t SUMMARY_BYTES = 256;

/** One line for the console or the simulator query surface. */
inline int formatSummary(const record_t &record, char *output, size_t size) {
  const int written = std::snprintf(
      output, size, "reset=%s task=%s uptime=%lus heap_free=%lu heap_largest=%lu clean=%s",
      resetReasonName(record.reset_reason), record.task,
      static_cast<unsigned long>(record.uptime_s), static_cast<unsigned long>(record.heap_free),
      static_cast<unsigned long>(record.heap_largest), clean(record) ? "yes" : "no");
  if ((record.alloc_size == 0) || (written < 0) || (static_cast<size_t>(written) >= size)) {
    return written;
  }
  const int more = std::snprintf(
      output + written, size - static_cast<size_t>(written),
      " alloc_fail=%lu caps=0x%08lx in=%s fn=%s", static_cast<unsigned long>(record.alloc_size),
      static_cast<unsigned long>(record.alloc_caps), record.alloc_task, record.alloc_function);
  return more < 0 ? written : written + more;
}

/** The most recent captured line, or an empty string. */
inline const char *lastLogLine(const record_t &record) {
  for (size_t index = LOG_LINES; index > 0; index--) {
    if (record.log[index - 1][0] != '\0') {
      return record.log[index - 1];
    }
  }
  return "";
}

}  // namespace CrashRecord
}  // namespace Furble

#endif
