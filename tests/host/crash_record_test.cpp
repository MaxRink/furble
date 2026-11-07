#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "FurbleCrashRecord.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

void fill(Furble::CrashRecord::record_t &record) {
  Furble::CrashRecord::clear(record);
  record.sequence = 7;
  record.reset_reason = 4;
  record.uptime_s = 42;
  record.heap_free = 13543;
  record.heap_largest = 12788;
  std::snprintf(record.task, sizeof(record.task), "%s", "mqtt");
  std::snprintf(record.log[0], sizeof(record.log[0]), "%s", "first line");
  std::snprintf(record.log[1], sizeof(record.log[1]), "%s", "last line");
  Furble::CrashRecord::finalize(record);
}

}  // namespace

int main() {
  using namespace Furble::CrashRecord;

  record_t record = {};
  fill(record);
  check(valid(record), "a finalized record is valid");
  check(record.magic == MAGIC, "magic is written by finalize");
  check(record.version == VERSION, "version is written by finalize");
  check(record.size == sizeof(record_t), "size is written by finalize");
  check(record.crc == checksum(record), "checksum matches");

  record.heap_free++;
  check(!valid(record), "a changed payload invalidates the CRC");
  fill(record);

  record.crc ^= 1U;
  check(!valid(record), "a corrupted CRC is rejected");
  fill(record);

  record.magic = 0x12345678U;
  check(!valid(record), "a foreign magic is rejected");
  fill(record);

  record.version = VERSION + 1;
  record.crc = checksum(record);
  check(!valid(record), "an unknown version is rejected");
  fill(record);

  check(!clean(record), "a fresh record is unclean");
  record.flags |= FLAG_CLEAN;
  Furble::CrashRecord::finalize(record);
  check(clean(record), "the clean flag survives finalize and validates");

  check(std::string(resetReasonName(4)) == "panic", "reset reason 4 is a panic");
  check(std::string(resetReasonName(6)) == "task_wdt", "reset reason 6 is a task watchdog");
  check(std::string(resetReasonName(9)) == "brownout", "reset reason 9 is brownout");
  check(std::string(resetReasonName(99)) == "other", "unknown reset reasons stay printable");

  fill(record);
  check(std::string(lastLogLine(record)) == "last line", "the newest non-empty line is returned");
  record.log[1][0] = '\0';
  check(std::string(lastLogLine(record)) == "first line", "empty tail lines are skipped");
  Furble::CrashRecord::clear(record);
  check(std::string(lastLogLine(record)).empty(), "an empty record has no log line");
  Furble::CrashRecord::finalize(record);
  check(valid(record), "an empty record can still validate");

  fill(record);
  char summary[Furble::CrashRecord::SUMMARY_BYTES] = {};
  const int written = formatSummary(record, summary, sizeof(summary));
  check(written > 0, "summary formatting returns a length");
  check(std::string(summary).find("reset=panic") != std::string::npos, "summary names the reset");
  check(std::string(summary).find("task=mqtt") != std::string::npos, "summary names the task");
  check(std::string(summary).find("heap_free=13543") != std::string::npos,
        "summary carries the free heap");
  check(std::string(summary).find("heap_largest=12788") != std::string::npos,
        "summary carries the largest block");
  check(std::string(summary).find("clean=no") != std::string::npos, "summary carries the flag");
  check(std::string(summary).find("alloc_fail") == std::string::npos,
        "a record without an allocation failure does not mention one");

  // The failed allocation, written in place by the heap callback.
  fill(record);
  Furble::CrashRecord::noteAllocFailure(record, 0x2400, 0x1800, "heap_caps_malloc_with_a_long_name",
                                        "a_task_name_longer_than_fifteen");
  Furble::CrashRecord::finalize(record);
  check(valid(record), "a record with an allocation failure validates");
  check(record.alloc_size == 0x2400 && record.alloc_caps == 0x1800, "size and caps are kept");
  check(std::strlen(record.alloc_task) == TASK_BYTES - 1, "the task name is bounded");
  check(std::strlen(record.alloc_function) == Furble::CrashRecord::ALLOC_FUNCTION_BYTES - 1,
        "the function name is bounded");
  record.alloc_size++;
  check(!valid(record), "the CRC covers the allocation failure");
  record.alloc_size--;
  // Every field at its widest still fits the summary buffer.
  record.reset_reason = 14;
  record.uptime_s = 0xffffffffU;
  record.heap_free = 0xffffffffU;
  record.heap_largest = 0xffffffffU;
  record.alloc_size = 0xffffffffU;
  record.alloc_caps = 0xffffffffU;
  std::memset(record.task, 'x', TASK_BYTES - 1);
  const int wide = formatSummary(record, summary, sizeof(summary));
  check(wide > 0 && static_cast<size_t>(wide) < sizeof(summary),
        "the widest summary fits SUMMARY_BYTES");
  check(std::string(summary).find("alloc_fail=4294967295 caps=0xffffffff") != std::string::npos,
        "summary carries the failed size and caps");

  // Truncation must stay terminated.
  char tiny[8] = {};
  formatSummary(record, tiny, sizeof(tiny));
  check(std::strlen(tiny) < sizeof(tiny), "a short buffer stays NUL terminated");

  if (failures != 0) {
    std::cerr << failures << " crash record checks failed\n";
    return 1;
  }
  std::cout << "crash record checks passed\n";
  return 0;
}
