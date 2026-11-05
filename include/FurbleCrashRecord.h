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
inline constexpr uint16_t VERSION = 1;
inline constexpr size_t TASK_BYTES = 16;
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
  record.crc = checksum(record);
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
    default:
      return "other";
  }
}

inline bool clean(const record_t &record) {
  return (record.flags & FLAG_CLEAN) != 0;
}

/** One line for the console or the simulator query surface. */
inline int formatSummary(const record_t &record, char *output, size_t size) {
  return std::snprintf(
      output, size, "reset=%s task=%s uptime=%lus heap_free=%lu heap_largest=%lu clean=%s",
      resetReasonName(record.reset_reason), record.task,
      static_cast<unsigned long>(record.uptime_s), static_cast<unsigned long>(record.heap_free),
      static_cast<unsigned long>(record.heap_largest), clean(record) ? "yes" : "no");
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
