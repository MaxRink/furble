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
  char summary[192] = {};
  const int written = formatSummary(record, summary, sizeof(summary));
  check(written > 0, "summary formatting returns a length");
  check(std::string(summary).find("reset=panic") != std::string::npos, "summary names the reset");
  check(std::string(summary).find("task=mqtt") != std::string::npos, "summary names the task");
  check(std::string(summary).find("heap_free=13543") != std::string::npos,
        "summary carries the free heap");
  check(std::string(summary).find("heap_largest=12788") != std::string::npos,
        "summary carries the largest block");
  check(std::string(summary).find("clean=no") != std::string::npos, "summary carries the flag");

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
