#ifndef FURBLE_CRASH_LOG_H
#define FURBLE_CRASH_LOG_H

#include <cstdint>

#include "FurbleCrashRecord.h"

namespace Furble {
namespace CrashLog {

/**
 * Read the previous boot's record, install the log capture, and print an
 * unclean record once. Call after the boot banner and before the services that
 * can fail. Must not be called twice.
 */
void init(void);

/**
 * Refresh the retained snapshot. Call from a normal task loop on a slow cadence;
 * the implementation throttles and never allocates or writes flash.
 */
void update(void);

/** Mark the retained record clean. Registered as a shutdown handler on device. */
void markCleanShutdown(void);

/** True when this boot found and printed an unclean record. */
bool recovered(void);

/** The record printed this boot, or nullptr. Valid until the next init(). */
const CrashRecord::record_t *lastRecord(void);

/** Print the last record to the console. Returns false when there is none. */
bool printLastRecord(void);

#if defined(FURBLE_CRASH_LOG_HOST_TEST)
/** Test seams for the host encoder/print test. */
void hostSetRecord(const CrashRecord::record_t &record);
void hostReset(void);
const CrashRecord::record_t *hostRecord(void);
#endif

}  // namespace CrashLog
}  // namespace Furble

#endif
