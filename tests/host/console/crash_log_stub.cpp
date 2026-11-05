#include "FurbleCrashLog.h"

/*
 * The console host test links the production console to check its parsing and
 * dispatch. It is not a crash-record test, and the production implementation
 * needs RTC attributes and the ESP log hook, so this stub keeps the link honest
 * and reports no retained record.
 */
namespace Furble {
namespace CrashLog {

void init(void) {}

void update(void) {}

void markCleanShutdown(void) {}

bool recovered(void) {
  return false;
}

const CrashRecord::record_t *lastRecord(void) {
  return nullptr;
}

bool printLastRecord(void) {
  return false;
}

}  // namespace CrashLog
}  // namespace Furble
