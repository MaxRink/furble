#include "FurbleCrashLog.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

using Furble::CrashRecord::LOG_LINE_BYTES;
using Furble::CrashRecord::LOG_LINES;
using Furble::CrashRecord::record_t;
using Furble::CrashRecord::TASK_BYTES;

[[maybe_unused]] constexpr const char *LOG_TAG = "crash";
[[maybe_unused]] constexpr uint64_t UPDATE_PERIOD_US = 1000 * 1000;

/*
 * The snapshot lives in RTC memory, which a panic, watchdog or software reset
 * does not clear. Nothing here writes flash: a crash path may run with the
 * cache disabled or NVS unwritable.
 */
RTC_NOINIT_ATTR record_t s_Record;

portMUX_TYPE s_Lock = portMUX_INITIALIZER_UNLOCKED;
char s_Lines[LOG_LINES][LOG_LINE_BYTES] = {};
uint32_t s_Head = 0;
uint32_t s_Count = 0;
char s_LastLogTask[TASK_BYTES] = {};
uint32_t s_Sequence = 0;
bool s_Initialized = false;
bool s_HookInstalled = false;
bool s_Recovered = false;
// Recursion guard for the print path. A panic can interrupt any task, so the
// read/write pair crosses task contexts and must not be a plain bool.
std::atomic<bool> s_Printing {false};
uint64_t s_LastUpdateUs = 0;
vprintf_like_t s_PreviousVprintf = nullptr;
record_t s_Printed;

void copyTaskName(char (&destination)[TASK_BYTES]) {
  const char *name = pcTaskGetName(nullptr);
  if (name == nullptr || name[0] == '\0') {
    name = "unknown";
  }
  std::snprintf(destination, TASK_BYTES, "%s", name);
}

/** Remove the ESP_LOG colour prefix/suffix so retained lines stay readable. */
void stripAnsi(char *line) {
  char *source = line;
  char *destination = line;
  while (*source != '\0') {
    if ((source[0] == '\x1b') && (source[1] == '[')) {
      source += 2;
      while ((*source != '\0') && ((*source < '@') || (*source > '~'))) {
        source++;
      }
      if (*source != '\0') {
        source++;
      }
      continue;
    }
    *destination++ = *source++;
  }
  *destination = '\0';
}

void appendLine(const char *line) {
  if (line == nullptr || s_Printing.load(std::memory_order_relaxed)) {
    return;
  }
  portENTER_CRITICAL(&s_Lock);
  std::snprintf(s_Lines[s_Head], LOG_LINE_BYTES, "%s", line);
  s_Head = (s_Head + 1) % LOG_LINES;
  if (s_Count < LOG_LINES) {
    s_Count++;
  }
  copyTaskName(s_LastLogTask);
  portEXIT_CRITICAL(&s_Lock);
}

int logVprintf(const char *format, va_list args) {
  char line[LOG_LINE_BYTES] = {};
  va_list copy;
  va_copy(copy, args);
  const int written = std::vsnprintf(line, sizeof(line), format, copy);
  va_end(copy);
  if (written > 0) {
    size_t length = std::strlen(line);
    while ((length > 0) && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
      line[--length] = '\0';
    }
    if (length > 0) {
      stripAnsi(line);
      if (line[0] != '\0') {
        appendLine(line);
      }
    }
  }
  if (s_PreviousVprintf != nullptr) {
    return s_PreviousVprintf(format, args);
  }
  return ::vprintf(format, args);
}

void printRecord(const record_t &record) {
  char summary[Furble::CrashRecord::SUMMARY_BYTES] = {};
  Furble::CrashRecord::formatSummary(record, summary, sizeof(summary));
  // The print goes through ESP_LOGE, which re-enters the capture hook. The
  // guard keeps the record itself out of the retained log tail. Two printing
  // tasks must not interleave their lines.
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

void storeRecord(const record_t &record) {
  portENTER_CRITICAL(&s_Lock);
  s_Record = record;
  portEXIT_CRITICAL(&s_Lock);
}

/** Write a valid snapshot for the current boot. */
void captureSnapshot(void) {
  const uint64_t now = esp_timer_get_time();
  record_t snapshot = {};
  snapshot.sequence = ++s_Sequence;
  snapshot.reset_reason = static_cast<uint32_t>(esp_reset_reason());
  snapshot.uptime_s = static_cast<uint32_t>(now / 1000000);
  snapshot.heap_free =
      static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  snapshot.heap_largest = static_cast<uint32_t>(
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  portENTER_CRITICAL(&s_Lock);
  for (size_t index = 0; index < s_Count; index++) {
    const uint32_t source = (s_Head + LOG_LINES - s_Count + index) % LOG_LINES;
    std::memcpy(snapshot.log[index], s_Lines[source], LOG_LINE_BYTES);
  }
  std::memcpy(snapshot.task, s_LastLogTask, TASK_BYTES);
  snapshot.task[TASK_BYTES - 1] = '\0';
  portEXIT_CRITICAL(&s_Lock);

  if (snapshot.task[0] == '\0') {
    copyTaskName(snapshot.task);
  }
  Furble::CrashRecord::finalize(snapshot);
  storeRecord(snapshot);
}

/**
 * Failed-allocation callback. ESP-IDF calls it from heap_caps_alloc_failed()
 * just before CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS aborts, on the task whose
 * allocation failed. That can be a small stack and the heap is exhausted, so it
 * edits the RTC record in place under the lock and never logs, allocates or
 * copies the record.
 */
void recordAllocFailure(size_t size, uint32_t caps, const char *function) {
  char task[TASK_BYTES];
  copyTaskName(task);
  portENTER_CRITICAL(&s_Lock);
  Furble::CrashRecord::noteAllocFailure(s_Record, size, caps, function, task);
  Furble::CrashRecord::finalize(s_Record);
  portEXIT_CRITICAL(&s_Lock);
}

}  // namespace

namespace Furble {
namespace CrashLog {

void init(void) {
  if (s_Initialized) {
    return;
  }
  s_Initialized = true;
  if (!s_HookInstalled) {
    s_PreviousVprintf = esp_log_set_vprintf(logVprintf);
    s_HookInstalled = true;
  }
  esp_register_shutdown_handler(markCleanShutdown);

  record_t previous;
  portENTER_CRITICAL(&s_Lock);
  previous = s_Record;
  portEXIT_CRITICAL(&s_Lock);

  const bool valid = CrashRecord::valid(previous);
  const bool clean = valid && CrashRecord::clean(previous);
  if (valid && !clean) {
    // The reset reason of the current boot is the reason the previous run
    // ended.
    previous.reset_reason = static_cast<uint32_t>(esp_reset_reason());
    CrashRecord::finalize(previous);
    printRecord(previous);
    s_Printed = previous;
    s_Recovered = true;
  }

  // Always consume the old record and install a fresh, unclean snapshot for
  // this boot. Leaving a clean record in place until the first periodic update
  // let an early-boot crash inherit the previous run's clean flag and vanish.
  captureSnapshot();
  // Registered only now, so a failure edits this boot's snapshot and never the
  // previous record that was just printed.
  heap_caps_register_failed_alloc_callback(recordAllocFailure);
}

void update(void) {
  const uint64_t now = esp_timer_get_time();
  if ((s_LastUpdateUs != 0) && ((now - s_LastUpdateUs) < UPDATE_PERIOD_US)) {
    return;
  }
  s_LastUpdateUs = now;
  captureSnapshot();
}

void markCleanShutdown(void) {
  record_t snapshot;
  portENTER_CRITICAL(&s_Lock);
  snapshot = s_Record;
  portEXIT_CRITICAL(&s_Lock);

  if (!CrashRecord::valid(snapshot)) {
    CrashRecord::clear(snapshot);
    snapshot.sequence = ++s_Sequence;
    snapshot.reset_reason = static_cast<uint32_t>(esp_reset_reason());
  }
  snapshot.flags |= CrashRecord::FLAG_CLEAN;
  CrashRecord::finalize(snapshot);
  storeRecord(snapshot);
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

#if defined(FURBLE_CRASH_LOG_HOST_TEST)
void hostSetRecord(const record_t &record) {
  s_Initialized = false;
  s_Recovered = false;
  s_Printing = false;
  s_Printed = {};
  s_LastUpdateUs = 0;
  storeRecord(record);
}

void hostReset(void) {
  s_Initialized = false;
  s_Recovered = false;
  s_Printing = false;
  s_Printed = {};
  s_LastUpdateUs = 0;
}

const record_t *hostRecord(void) {
  static record_t copy;
  portENTER_CRITICAL(&s_Lock);
  copy = s_Record;
  portEXIT_CRITICAL(&s_Lock);
  return &copy;
}
#endif

}  // namespace CrashLog
}  // namespace Furble
