// Host test for gate 3 of plan 320 C2: the timed-wake marker check in the
// production Platform.
//
// `TimeKeeper::adoptTimedWake()` is called from exactly one place,
// `Platform::consumeTimedWake()`, and only when the board reports a timer wake
// *and* the wake marker the shutdown wrote is still in the M5PM1 RTC buffer.
// That gate needs the M5PM1, not an ESP32, so it is reachable from a host
// target: this one compiles the real src/FurblePlatform.cpp against the
// register-bus double in platform_shims/M5PM1.h, the same pattern as
// boundary-platform-rail.
//
// Asserted here:
//   * A confirmed timed wake adopts the record the shutdown wrote, byte for
//     byte, and the interval the shutdown armed is the interval in the record.
//   * A boot with the timer wake source but no marker adopts nothing.
//   * A boot with the marker but no timer wake source adopts nothing.
//   * Every one of those boots clears the whole 32-byte buffer, so no later
//     boot can pair a stale record with a later marker.
//   * A boot whose record read fails still clears the buffer and adopts
//     nothing, which is the record-outliving-its-boot fix.
//   * A shutdown that cannot build a prediction clears the record region, so
//     the marker it just wrote is never paired with an earlier interval, and so
//     does a shutdown whose record write fails.
//
// What this cannot show, because no host process can: that a real M5PM1 keeps
// its RTC buffer across the shutdown it performs, that a real timer wake sets
// WAKE_SRC bit 0, and what the timer's accuracy is. Those are the device
// confirmations plans/320-rtc-use.md names.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "FurblePlatform.h"
#include "FurbleTimeKeeper.h"
#include "M5PM1.h"

using Furble::Platform;
using Furble::TimeKeeper;
using Furble::TimeKeeperDouble::expectedPrediction;
using Furble::TimeKeeperPolicy::RECORD_BYTES;

namespace {

int g_Failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_Failures++;
  }
}

// The two constants src/FurblePlatform.cpp keeps in an anonymous namespace. The
// marker is a magic value and the record sits directly behind it, which is the
// layout docs/components/m5pm1.md documents; a change to either is a change to
// the on-device layout, so the test pins them rather than reading them.
constexpr uint8_t BUFFER_BYTES = 32;
constexpr uint8_t MARKER_BYTES = 4;
constexpr uint8_t RECORD_OFFSET = MARKER_BYTES;
constexpr uint32_t TIMED_WAKE_MARKER = 0x49564c31;

static_assert(RECORD_OFFSET + RECORD_BYTES <= BUFFER_BYTES,
              "the marker and the record have to fit the 32-byte M5PM1 buffer");

Platform &platform(void) {
  return Platform::getInstance();
}

M5PM1 &pmic(void) {
  return M5PM1::instanceForTest();
}

// Every case starts from a PM1 that has just been powered and an ESP32 that has
// not shut down yet.
void resetBoot(void) {
  // Platform::getInstance() constructs the production Platform and, with it, the
  // M5PM1 the double reports. Nothing else in this test may touch the double
  // before it exists.
  (void)platform();
  pmic().clearRtcRam();
  pmic().setWakeSourceRaw(0);
  pmic().clearTimer();
  TimeKeeper::resetForTest();
}

bool bufferIsClear(void) {
  const uint8_t *ram = pmic().rtcRam();
  for (uint8_t i = 0; i < BUFFER_BYTES; i++) {
    if (ram[i] != 0) {
      return false;
    }
  }
  return true;
}

bool markerPresent(void) {
  const uint8_t *ram = pmic().rtcRam();
  return std::memcmp(ram, &TIMED_WAKE_MARKER, MARKER_BYTES) == 0;
}

bool regionEquals(uint8_t offset, const uint8_t *expected, size_t length) {
  return std::memcmp(pmic().rtcRam() + offset, expected, length) == 0;
}

bool adoptedEquals(const uint8_t *expected, size_t length) {
  return (TimeKeeper::lastAdoptLength() == length)
         && (std::memcmp(TimeKeeper::lastAdoptBytes(), expected, length) == 0);
}

// A verified timed wake: the shutdown armed the timer, wrote the marker, wrote
// the prediction, and the boot reports the timer as the wake source.
void confirmedTimedWakeAdopts(const uint8_t *record, uint32_t seconds) {
  resetBoot();
  TimeKeeper::setPredictionEnabled(true);
  TimeKeeper::setAdoptResult(true);

  check(platform().powerOffUntil(seconds), "the timed shutdown is accepted");
  check(pmic().timerSeconds() == seconds, "the shutdown armed the interval it was given");
  check(markerPresent(), "the shutdown writes the wake marker first");
  check(regionEquals(RECORD_OFFSET, record, RECORD_BYTES),
        "the shutdown writes the prediction behind the marker");
  check(TimeKeeper::lastProgrammedSeconds() == seconds,
        "the prediction is built for the armed interval");

  pmic().setWakeSourceRaw(M5PM1_WAKE_SRC_TIM);
  check(platform().consumeTimedWake(), "a timer wake with the marker is a timed wake");
  check(TimeKeeper::adoptCalls() == 1, "a confirmed timed wake adopts exactly once");
  check(adoptedEquals(record, RECORD_BYTES),
        "the adopted record is the one the shutdown wrote, byte for byte");
  check(bufferIsClear(), "the adopted buffer is cleared so a later boot cannot adopt it");
}

// The timer fired, but the boot cannot find its own marker: an erased buffer, or
// one a failed shutdown wrote. Nothing is added to the clock.
void timerWithoutMarkerAdoptsNothing(void) {
  resetBoot();
  TimeKeeper::setPredictionEnabled(true);
  TimeKeeper::setAdoptResult(true);

  check(platform().powerOffUntil(300), "the timed shutdown is accepted");
  pmic().clearRtcRam();

  pmic().setWakeSourceRaw(M5PM1_WAKE_SRC_TIM);
  check(!platform().consumeTimedWake(), "a timer wake without the marker is not a timed wake");
  check(TimeKeeper::adoptCalls() == 0, "no record is adopted without the marker");
  check(bufferIsClear(), "the buffer is cleared anyway");
}

// The buffer still holds the marker and the record from a shutdown, but this
// boot was not a timer wake: the rail came back some other way.
void markerWithoutTimerWakeAdoptsNothing(void) {
  resetBoot();
  TimeKeeper::setPredictionEnabled(true);
  TimeKeeper::setAdoptResult(true);

  check(platform().powerOffUntil(600), "the timed shutdown is accepted");
  check(markerPresent(), "the buffer holds the marker this boot did not earn");

  pmic().setWakeSourceRaw(M5PM1_WAKE_SRC_PWRBTN);
  check(!platform().consumeTimedWake(), "the power button is not a timed wake");
  check(TimeKeeper::adoptCalls() == 0, "no record is adopted on a manual power-on");
  check(bufferIsClear(), "a manual power-on clears the record rather than keeping it");
}

// The record read fails twice, which is what production's one retry sees on a
// dead bus. The boot must not leave the record behind for the next boot.
void failedRecordReadClearsTheBuffer(void) {
  resetBoot();
  TimeKeeper::setPredictionEnabled(true);
  TimeKeeper::setAdoptResult(true);

  uint8_t record[RECORD_BYTES];
  expectedPrediction(60, record);
  check(platform().powerOffUntil(60), "the timed shutdown is accepted");
  check(regionEquals(RECORD_OFFSET, record, RECORD_BYTES),
        "the record is in the buffer before the failing boot");

  // Two failures: m5pm1Access() retries once, so the boot sees a failed read
  // rather than a zeroed one.
  // The marker stays readable and only the record region fails, which is the
  // case production guards: it has already decided this is a timed wake when it
  // tries to read the record.
  pmic().failRtcRamReadsAt(RECORD_OFFSET, 2);
  pmic().setWakeSourceRaw(M5PM1_WAKE_SRC_TIM);
  check(!platform().consumeTimedWake(), "a boot whose record cannot be read is not a timed wake");
  check(TimeKeeper::adoptCalls() == 0, "an unreadable record is not adopted");
  check(bufferIsClear(), "the unreadable record is cleared, so it cannot outlive its boot");
}

// The shutdown cannot build a prediction. The marker it writes must not meet the
// interval of an earlier shutdown.
void shutdownWithoutPredictionClearsTheRegion(void) {
  resetBoot();
  TimeKeeper::setPredictionEnabled(true);
  TimeKeeper::setAdoptResult(true);

  uint8_t stale[RECORD_BYTES];
  expectedPrediction(600, stale);
  check(platform().powerOffUntil(600), "the first timed shutdown is accepted");
  check(regionEquals(RECORD_OFFSET, stale, RECORD_BYTES), "the first record is in the buffer");

  // A later shutdown on a clock that cannot predict, for a different interval.
  TimeKeeper::setPredictionEnabled(false);
  check(platform().powerOffUntil(900), "the second timed shutdown is accepted");
  check(pmic().timerSeconds() == 900, "the second shutdown armed its own interval");
  check(markerPresent(), "the second shutdown writes its own marker");
  const uint8_t zero[RECORD_BYTES] = {};
  check(regionEquals(RECORD_OFFSET, zero, RECORD_BYTES),
        "the earlier interval is cleared, so the new marker cannot pair with it");

  // And the boot that follows it has a marker but no decodable record: the
  // policy behind adoptTimedWake() refuses the zeros, the Platform clears the
  // buffer, and the stored checkpoint clock survives.
  TimeKeeper::setAdoptResult(false);
  pmic().setWakeSourceRaw(M5PM1_WAKE_SRC_TIM);
  check(platform().consumeTimedWake(), "the marker still reports a timed wake");
  check(TimeKeeper::adoptCalls() == 1, "the refusal is decided by the record, not skipped");
  check(bufferIsClear(), "the buffer is cleared after the refusal");
}

// The record write fails after the marker has landed. Production has to leave the
// region in a state the boot refuses, not one it can read as the interval of an
// earlier shutdown: the marker plus a 600 s record would be adopted by the next
// confirmed wake as if this 60 s shutdown had armed it.
void failedRecordWriteLeavesNoStaleRecord(void) {
  resetBoot();
  TimeKeeper::setPredictionEnabled(true);
  TimeKeeper::setAdoptResult(true);

  uint8_t stale[RECORD_BYTES];
  expectedPrediction(600, stale);
  check(platform().powerOffUntil(600), "the first timed shutdown is accepted");
  check(regionEquals(RECORD_OFFSET, stale, RECORD_BYTES), "the 600 s record is in the buffer");

  uint8_t fresh[RECORD_BYTES];
  expectedPrediction(60, fresh);
  // Two failures: m5pm1Access() retries once, so the record write does not land.
  pmic().failRtcRamWritesAt(RECORD_OFFSET, 2);
  check(platform().powerOffUntil(60), "a failed record write does not refuse the shutdown");
  check(pmic().timerSeconds() == 60, "the timer is armed for the interval that was asked for");
  check(markerPresent(), "the marker of the new shutdown is written");
  check(!regionEquals(RECORD_OFFSET, fresh, RECORD_BYTES), "the 60 s record did not land");
  check(!regionEquals(RECORD_OFFSET, stale, RECORD_BYTES),
        "the earlier 600 s record is gone rather than left behind the new marker");
  const uint8_t zero[RECORD_BYTES] = {};
  check(regionEquals(RECORD_OFFSET, zero, RECORD_BYTES),
        "the region is cleared, so the boot has a marker with no record to read");

  // The confirmed wake that follows cannot adopt the earlier interval. The double
  // accepts whatever it is handed, so the bytes offered to the clock are what is
  // asserted: the zeros, never the 600 s record.
  pmic().setWakeSourceRaw(M5PM1_WAKE_SRC_TIM);
  check(platform().consumeTimedWake(), "the marker still reports a timed wake");
  check(TimeKeeper::adoptCalls() == 1, "the record reaches the clock");
  check(!adoptedEquals(stale, RECORD_BYTES),
        "the clock is never handed the interval of the earlier shutdown");
  check(bufferIsClear(), "the buffer is cleared after the boot");
}

// A shutdown that a handshake refuses writes no record and leaves no marker, so
// the next boot cannot read a half-written pair. The rail double accepts the SD
// handshake, so this case is the zero-interval one: powerOffUntil(0) is a plain
// power-off and must not arm the timer or touch the buffer.
void plainPowerOffTouchesNothing(void) {
  resetBoot();
  TimeKeeper::setPredictionEnabled(true);
  TimeKeeper::setAdoptResult(true);

  uint8_t record[RECORD_BYTES];
  expectedPrediction(60, record);
  check(platform().powerOffUntil(60), "the timed shutdown is accepted");
  const size_t writes = pmic().rtcRamWrites().size();
  check(writes > 0, "the shutdown wrote the buffer");

  resetBoot();
  platform().powerOffUntil(0);
  check(pmic().rtcRamWrites().empty(), "an interval of zero writes no record");
  check(pmic().timerSeconds() == 0, "an interval of zero arms no timer");
  check(bufferIsClear(), "the buffer is untouched");
}

// The pattern the double predicts is what the test compares against, so pin the
// two bytes that identify it. A generator that silently returned zeros would
// make every assertion above pass for the wrong reason.
void predictionPatternIsDistinct(void) {
  uint8_t shortInterval[RECORD_BYTES];
  uint8_t a[RECORD_BYTES];
  uint8_t b[RECORD_BYTES];
  expectedPrediction(60, shortInterval);
  expectedPrediction(900, a);
  expectedPrediction(3600, b);
  check(shortInterval[0] == 0xA5, "the record carries its tag");
  check(shortInterval[1] == 60 && shortInterval[2] == 0, "the record carries the armed interval");
  check((a[1] == (900 & 0xFF)) && (a[2] == (900 >> 8)), "a different interval changes the record");
  check(std::memcmp(a, b, RECORD_BYTES) != 0, "two intervals do not share a record");
  // A generator that quietly returned zeros, or one that repeated a byte, would
  // make the round-trip assertions above pass for the wrong reason.
  for (size_t i = 0; i < RECORD_BYTES; i++) {
    check(a[i] != 0, "every byte of the pattern is non-zero");
    if (i + 1 < RECORD_BYTES) {
      check(a[i] != a[i + 1], "neighbouring bytes of the pattern differ");
    }
  }
}

}  // namespace

int main() {
  uint8_t record[RECORD_BYTES];

  expectedPrediction(60, record);
  confirmedTimedWakeAdopts(record, 60);

  expectedPrediction(3600, record);
  confirmedTimedWakeAdopts(record, 3600);

  timerWithoutMarkerAdoptsNothing();
  markerWithoutTimerWakeAdoptsNothing();
  failedRecordReadClearsTheBuffer();
  shutdownWithoutPredictionClearsTheRegion();
  failedRecordWriteLeavesNoStaleRecord();
  plainPowerOffTouchesNothing();
  predictionPatternIsDistinct();

  if (g_Failures != 0) {
    std::cerr << "platform-timed-wake: " << g_Failures << " failure(s)\n";
    return 1;
  }

  std::cout << "platform-timed-wake: PASS" << '\n';
  return 0;
}
