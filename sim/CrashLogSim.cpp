#include "crash_log_sim.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "FurbleCrashLog.h"
#include "clock.h"
#include "heap_model.h"

namespace {

using Furble::CrashRecord::LOG_LINE_BYTES;
using Furble::CrashRecord::LOG_LINES;
using Furble::CrashRecord::record_t;
using Furble::CrashRecord::TASK_BYTES;

constexpr const char *LOG_TAG = "crash";
constexpr uint32_t UPDATE_PERIOD_MS = 1000;

char s_Lines[LOG_LINES][LOG_LINE_BYTES] = {};
uint32_t s_Head = 0;
uint32_t s_Count = 0;
// The simulator runs the UI, control and service tasks on real host threads.
// The retained tail and its sequence cross those threads, so guard both.
std::mutex s_Mutex;
std::mutex s_PersistMutex;
std::atomic<uint32_t> s_Sequence {0};
uint32_t s_LastUpdateMs = 0;
std::atomic<bool> s_Printing {false};
bool s_Initialized = false;
bool s_Recovered = false;
bool s_Injected = false;
record_t s_Printed;
record_t s_Current = {};
bool s_HaveCurrent = false;
std::chrono::steady_clock::time_point s_LastPersist;

std::string recordPath(void) {
  const char *preferences = std::getenv("FURBLE_SIM_PREFS");
  if (preferences == nullptr || preferences[0] == '\0') {
    return {};
  }
  return std::string(preferences) + ".crash";
}

bool readPersisted(record_t &record) {
  const std::string path = recordPath();
  if (path.empty()) {
    return false;
  }
  std::FILE *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return false;
  }
  const size_t read = std::fread(&record, 1, sizeof(record), file);
  std::fclose(file);
  return read == sizeof(record);
}

void writePersisted(const record_t &record) {
  const std::lock_guard<std::mutex> lock(s_PersistMutex);
  const std::string path = recordPath();
  if (path.empty()) {
    return;
  }
  const std::string temporary = path + ".tmp";
  std::FILE *file = std::fopen(temporary.c_str(), "wb");
  if (file == nullptr) {
    return;
  }
  const size_t written = std::fwrite(&record, 1, sizeof(record), file);
  const bool closed = std::fclose(file) == 0;
  if (written != sizeof(record) || !closed || std::rename(temporary.c_str(), path.c_str()) != 0) {
    std::remove(temporary.c_str());
  }
}

void appendLine(const char *line) {
  if (line == nullptr || line[0] == '\0' || s_Printing.load(std::memory_order_relaxed)) {
    return;
  }
  const std::lock_guard<std::mutex> lock(s_Mutex);
  std::snprintf(s_Lines[s_Head], LOG_LINE_BYTES, "%s", line);
  s_Head = (s_Head + 1) % LOG_LINES;
  if (s_Count < LOG_LINES) {
    s_Count++;
  }
}

void copyTaskName(char (&destination)[TASK_BYTES]) {
  const char *name = pcTaskGetName(nullptr);
  if (name == nullptr || name[0] == '\0') {
    name = "ui";
  }
  std::snprintf(destination, TASK_BYTES, "%s", name);
}

void fillSnapshot(record_t &snapshot, uint32_t resetReason) {
  Furble::CrashRecord::clear(snapshot);
  snapshot.sequence = ++s_Sequence;
  snapshot.reset_reason = resetReason;
  snapshot.uptime_s = Furble::Sim::clockMillis() / 1000;
  snapshot.heap_free = static_cast<uint32_t>(Furble::Sim::heapFree());
  snapshot.heap_largest = static_cast<uint32_t>(Furble::Sim::heapLargest());
  {
    const std::lock_guard<std::mutex> lock(s_Mutex);
    for (uint32_t index = 0; index < s_Count; index++) {
      const uint32_t source = (s_Head + LOG_LINES - s_Count + index) % LOG_LINES;
      std::memcpy(snapshot.log[index], s_Lines[source], LOG_LINE_BYTES);
    }
  }
  copyTaskName(snapshot.task);
  Furble::CrashRecord::finalize(snapshot);
}

void printRecord(const record_t &record) {
  char summary[192] = {};
  Furble::CrashRecord::formatSummary(record, summary, sizeof(summary));
  bool expected = false;
  if (!s_Printing.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
    return;
  }
  ESP_LOGE(LOG_TAG, "CRASH RECORD: %s", summary);
  for (size_t line = 0; line < LOG_LINES; line++) {
    if (record.log[line][0] != '\0') {
      ESP_LOGE(LOG_TAG, "CRASH LOG: %s", record.log[line]);
    }
  }
  s_Printing.store(false, std::memory_order_relaxed);
}

}  // namespace

extern "C" void furble_sim_crash_log_capture(const char *tag, const char *format, va_list args) {
  (void)tag;
  if (format == nullptr || s_Printing) {
    return;
  }
  char line[LOG_LINE_BYTES] = {};
  va_list copy;
  va_copy(copy, args);
  const int written = std::vsnprintf(line, sizeof(line), format, copy);
  va_end(copy);
  if (written <= 0) {
    return;
  }
  size_t length = std::strlen(line);
  while ((length > 0) && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
    line[--length] = '\0';
  }
  appendLine(line);
}

namespace Furble {
namespace CrashLog {

void init(void) {
  if (s_Initialized) {
    return;
  }
  s_Initialized = true;

  record_t previous = {};
  const bool valid = readPersisted(previous) && CrashRecord::valid(previous);
  const bool clean = valid && CrashRecord::clean(previous);
  if (valid && !clean) {
    printRecord(previous);
    s_Printed = previous;
    s_Recovered = true;
  }
  s_Injected = false;

  // Consume the old record and persist a fresh unclean snapshot for this boot.
  // Keeping a clean sidecar until the first periodic update let an early crash
  // inherit the previous run's clean flag and disappear.
  record_t snapshot = {};
  fillSnapshot(snapshot, 1);  // ESP_RST_POWERON
  {
    const std::lock_guard<std::mutex> lock(s_Mutex);
    s_Current = snapshot;
    s_HaveCurrent = true;
  }
  writePersisted(snapshot);
}

void update(void) {
  if (s_Injected) {
    return;
  }
  const uint32_t now = Furble::Sim::clockMillis();
  if ((s_LastUpdateMs != 0) && ((now - s_LastUpdateMs) < UPDATE_PERIOD_MS)) {
    return;
  }
  s_LastUpdateMs = now;
  record_t snapshot = {};
  fillSnapshot(snapshot, 1);  // ESP_RST_POWERON
  {
    const std::lock_guard<std::mutex> lock(s_Mutex);
    s_Current = snapshot;
    s_HaveCurrent = true;
  }
  // The snapshot is an in-memory RTC substitute. Persist at most once per wall
  // second so virtual-time-heavy scenarios are not turned into file-I/O tests,
  // which perturbs unrelated cross-task timing assertions.
  const auto wall = std::chrono::steady_clock::now();
  if ((s_LastPersist.time_since_epoch().count() == 0)
      || ((wall - s_LastPersist) >= std::chrono::seconds(1))) {
    s_LastPersist = wall;
    writePersisted(snapshot);
  }
}

void markCleanShutdown(void) {
  // An injected crash models an unclean reset. Do not overwrite it on the way
  // to the simulated reboot, or the next boot would have nothing to print.
  if (s_Injected) {
    return;
  }
  record_t snapshot;
  bool haveCurrent = false;
  {
    const std::lock_guard<std::mutex> lock(s_Mutex);
    haveCurrent = s_HaveCurrent;
    if (haveCurrent) {
      snapshot = s_Current;
    }
  }
  if (!haveCurrent && (!readPersisted(snapshot) || !CrashRecord::valid(snapshot))) {
    Furble::CrashRecord::clear(snapshot);
    snapshot.sequence = ++s_Sequence;
    snapshot.reset_reason = 1;
  }
  snapshot.flags |= CrashRecord::FLAG_CLEAN;
  CrashRecord::finalize(snapshot);
  writePersisted(snapshot);
}

bool recovered(void) {
  return s_Recovered;
}

const record_t *lastRecord(void) {
  return s_Recovered ? &s_Printed : nullptr;
}

bool printLastRecord(void) {
  if (!s_Recovered) {
    return false;
  }
  printRecord(s_Printed);
  return true;
}

}  // namespace CrashLog
}  // namespace Furble

namespace Furble::Sim {

void crashLogInject(uint32_t resetReason, const char *task, const char *line) {
  record_t record = {};
  Furble::CrashRecord::clear(record);
  record.sequence = ++s_Sequence;
  record.reset_reason = resetReason;
  record.uptime_s = Furble::Sim::clockMillis() / 1000;
  record.heap_free = static_cast<uint32_t>(Furble::Sim::heapFree());
  record.heap_largest = static_cast<uint32_t>(Furble::Sim::heapLargest());
  record.flags = 0;
  copyTaskName(record.task);
  {
    const std::lock_guard<std::mutex> lock(s_Mutex);
    for (uint32_t index = 0; index < s_Count; index++) {
      const uint32_t source = (s_Head + LOG_LINES - s_Count + index) % LOG_LINES;
      std::memcpy(record.log[index], s_Lines[source], LOG_LINE_BYTES);
    }
  }
  if (task != nullptr && task[0] != '\0') {
    std::snprintf(record.task, TASK_BYTES, "%s", task);
  }
  if (line != nullptr && line[0] != '\0') {
    std::snprintf(record.log[LOG_LINES - 1], LOG_LINE_BYTES, "%s", line);
  }
  Furble::CrashRecord::finalize(record);
  writePersisted(record);
  s_Injected = true;
}

bool crashLogInjected(void) {
  return s_Injected;
}

void crashLogRemovePersisted(void) {
  const std::string path = recordPath();
  if (!path.empty()) {
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
  }
}

}  // namespace Furble::Sim
