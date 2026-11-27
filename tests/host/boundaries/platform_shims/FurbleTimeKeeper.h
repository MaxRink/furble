#ifndef FURBLE_PLATFORM_SHIM_TIME_KEEPER_H
#define FURBLE_PLATFORM_SHIM_TIME_KEEPER_H

#include <cstddef>
#include <cstdint>

namespace Furble {

// The record the timed-wake prediction is encoded in. Kept at the production
// value, because src/FurblePlatform.cpp bounds its M5PM1 RTC buffer write with
// static_assert() on it and the shim header replaces include/FurbleTimeKeeper.h
// in this target.
namespace TimeKeeperPolicy {
constexpr size_t RECORD_BYTES = 28;
}  // namespace TimeKeeperPolicy

namespace TimeKeeperDouble {

/**
 * The exact bytes prepareTimedWake() writes for an armed interval.
 *
 * A test compares the record the shutdown stored in the M5PM1 buffer and the
 * record the boot handed to adoptTimedWake() against this, so both sides come
 * from the same definition rather than from a copy in the test.
 */
void expectedPrediction(uint32_t programmed_seconds, uint8_t *buffer);

}  // namespace TimeKeeperDouble

/**
 * The clock seam, for a target that links the production Platform but not the
 * production TimeKeeper.
 *
 * The default is the fail-closed answer the rail target relies on: no
 * prediction is produced and nothing is adopted, so a shutdown writes no record
 * and a boot keeps the stored checkpoint clock. `platform-timed-wake` turns the
 * prediction on to prove the marker gate, and reads back what the production
 * boot handed to adoptTimedWake().
 */
class TimeKeeper {
 public:
  static TimeKeeper &getInstance(void);
  void flush(void);
  size_t prepareTimedWake(uint32_t programmed_seconds, uint8_t *buffer, size_t capacity);
  bool adoptTimedWake(const uint8_t *buffer, size_t length);

  // ---- test controls -------------------------------------------------

  /**
   * Produce a record on the next prepareTimedWake(), or stop producing one.
   *
   * The bytes are a fixed pattern this double owns, not a TimeSample: the
   * production code under test only moves the record, so the test proves the
   * bytes survive the round trip rather than that the clock decodes them. Byte
   * 0 is the pattern tag and byte 1 the low byte of the armed interval, which
   * is what lets a test assert the interval the shutdown armed reached the
   * record.
   */
  static void setPredictionEnabled(bool enabled);
  static void setAdoptResult(bool result);
  /** How many times the production boot called adoptTimedWake(). */
  static unsigned adoptCalls(void);
  static size_t lastAdoptLength(void);
  static const uint8_t *lastAdoptBytes(void);
  /** The interval the last prepareTimedWake() was asked to predict. */
  static uint32_t lastProgrammedSeconds(void);
  static void resetForTest(void);
};

}  // namespace Furble

#endif
