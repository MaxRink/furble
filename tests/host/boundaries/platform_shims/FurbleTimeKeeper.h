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

class TimeKeeper {
 public:
  static TimeKeeper &getInstance(void);
  void flush(void);
  // The timed-wake seam. This double never predicts or adopts anything, which
  // is the fail-closed answer: a rail test is not exercising the clock, and a
  // zero length means the production path writes no record.
  size_t prepareTimedWake(uint32_t programmed_seconds, uint8_t *buffer, size_t capacity);
  bool adoptTimedWake(const uint8_t *buffer, size_t length);
};

}  // namespace Furble

#endif
