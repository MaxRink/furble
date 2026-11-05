#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "FurbleCrashLog.h"
#include "FurbleCrashRecord.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

bool captured(const std::string &needle) {
  for (const auto &line : hostCrashLogLines()) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

Furble::CrashRecord::record_t makeRecord(uint32_t reason, const char *task, const char *line) {
  Furble::CrashRecord::record_t record;
  Furble::CrashRecord::clear(record);
  record.sequence = 3;
  record.reset_reason = reason;
  record.uptime_s = 42;
  record.heap_free = 13543;
  record.heap_largest = 12788;
  std::snprintf(record.task, sizeof(record.task), "%s", task);
  std::snprintf(record.log[0], sizeof(record.log[0]), "%s", line);
  Furble::CrashRecord::finalize(record);
  return record;
}

}  // namespace

int main() {
  using namespace Furble;
  const auto *record = CrashLog::hostRecord();
  check(record != nullptr, "the host test seam returns a record");

  // An unclean previous record is reported once, with the reason the current
  // boot reports for how the previous run ended.
  hostCrashLogLines().clear();
  hostCrashResetReason() = ESP_RST_PANIC;
  CrashLog::hostSetRecord(makeRecord(ESP_RST_PANIC, "mqtt", "synthetic panic"));
  CrashLog::init();
  check(CrashLog::recovered(), "an unclean record is recovered");
  const auto *printed = CrashLog::lastRecord();
  check(printed != nullptr, "the recovered record is retained");
  if (printed != nullptr) {
    check(std::string(CrashRecord::resetReasonName(printed->reset_reason)) == "panic",
          "the recovered reason is panic");
    check(std::string(printed->task) == "mqtt", "the recovered task survives");
    check(printed->heap_free == 13543, "the recovered free heap survives");
    check(printed->heap_largest == 12788, "the recovered largest block survives");
    check(std::string(CrashRecord::lastLogLine(*printed)) == "synthetic panic",
          "the recovered log tail survives");
  }
  check(captured("CRASH RECORD: reset=panic task=mqtt"), "the boot prints the crash headline");
  check(captured("CRASH LOG: synthetic panic"), "the boot prints the captured log tail");
  check(!CrashRecord::valid(*CrashLog::hostRecord()), "the record is consumed after the print");
  check(CrashLog::printLastRecord(), "the retained record can be reprinted for a console");

  // A clean previous shutdown is not a crash.
  hostCrashLogLines().clear();
  auto clean = makeRecord(ESP_RST_SW, "ui", "clean");
  clean.flags |= CrashRecord::FLAG_CLEAN;
  CrashRecord::finalize(clean);
  CrashLog::hostSetRecord(clean);
  CrashLog::init();
  check(!CrashLog::recovered(), "a clean record is not reported");
  check(!captured("CRASH RECORD:"), "a clean boot does not print a crash headline");

  // The periodic snapshot carries the log tail and the current heap.
  CrashLog::hostReset();
  hostCrashLogLines().clear();
  hostCrashHeapFree() = 9000;
  hostCrashHeapLargest() = 8000;
  hostCrashNowUs() = 5000000;
  ESP_LOGE("test", "line before the snapshot");
  CrashLog::update();
  const auto *snapshot = CrashLog::hostRecord();
  check(CrashRecord::valid(*snapshot), "the periodic snapshot validates");
  check(snapshot->uptime_s == 5, "the snapshot records uptime");
  check(snapshot->heap_free == 9000, "the snapshot records free heap");
  check(snapshot->heap_largest == 8000, "the snapshot records the largest block");
  check(std::string(snapshot->task) == "ui", "the snapshot records the task name");
  check(std::string(CrashRecord::lastLogLine(*snapshot)).find("line before the snapshot")
            != std::string::npos,
        "the snapshot keeps the newest log line");

  // The throttle keeps two updates inside one second from rewriting the
  // sequence, then a later update advances it.
  const uint32_t sequence = snapshot->sequence;
  hostCrashNowUs() = 5500000;
  CrashLog::update();
  check(CrashLog::hostRecord()->sequence == sequence, "the update cadence is throttled");
  hostCrashNowUs() = 7000000;
  CrashLog::update();
  check(CrashLog::hostRecord()->sequence == sequence + 1, "a later update advances the sequence");

  // The shutdown handler marks the record clean and is registered.
  check(hostCrashShutdownHandler() != nullptr, "init registers a shutdown handler");
  hostCrashShutdownHandler()();
  check(CrashRecord::clean(*CrashLog::hostRecord()), "a shutdown marks the record clean");
  check(CrashRecord::valid(*CrashLog::hostRecord()), "a clean shutdown record validates");

  if (failures != 0) {
    std::cerr << failures << " crash log checks failed\n";
    return 1;
  }
  std::cout << "crash log checks passed\n";
  return 0;
}
