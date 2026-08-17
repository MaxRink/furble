// Host tests for the bounded GPS fix hold and dead reckoning arithmetic in
// include/FurbleGPSHold.h.
//
// GPS::update() runs on the UI task with a receiver, a camera and LVGL behind
// it, so the parts that are easy to get quietly wrong live in a header with no
// dependencies and are pinned here. The three that matter are the bound, which
// is the only thing stopping an hour-old position from being stamped onto a
// photo; the UTC advance, which has to survive a minute, hour, day, month and
// year rollover because newlib has no timegm and the conversion is hand
// written; and the projection, which must not move a user who is standing
// still.
//
// Mutation checks (the test's teeth):
//   - make gpsHoldInBound accept elapsed > limit: testBoundIsInclusive fails.
//   - make gpsHoldInBound accept a zero limit: testHoldOffIsOff fails.
//   - drop the saturation from gpsHoldRemainingMs: testRemainingSaturates fails.
//   - drop the GPS_EXTRAPOLATE_MIN_SPEED_MPS floor: testStationaryIsNotMoved
//     fails.
//   - drop the horizon clamp from gpsExtrapolateElapsedMs: testHorizonFreezes
//     fails.
//   - use localtime_r instead of gmtime_r in gpsAdvanceUtc: every rollover case
//     in testUtcRollovers fails outside UTC.
//   - drop the days-from-civil leap year term: testUtcRollovers fails on the
//     2024-02-28 and 1900-style century cases.
//   - drop the input validation from gpsAdvanceUtc: testUtcRejectsGarbage
//     fails.
//   - swap sin and cos in gpsExtrapolate: testCourseIntegration fails, because
//     a due north course would move the longitude instead of the latitude.
//   - drop the pole guard from gpsExtrapolate: testPoleIsRejected fails.

#include <cmath>
#include <cstdint>
#include <iostream>

#include "FurbleGPSHold.h"

using namespace Furble;

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

bool nearly(double a, double b, double tolerance) {
  return std::fabs(a - b) <= tolerance;
}

// The fields gpsAdvanceUtc touches, matching Camera::timesync_t without
// dragging the camera headers, and therefore NimBLE, into a host test.
typedef struct {
  unsigned int year;
  unsigned int month;
  unsigned int day;
  unsigned int hour;
  unsigned int minute;
  unsigned int second;
  unsigned int centisecond;
} utc_t;

bool utcEquals(const utc_t &value,
               unsigned int year,
               unsigned int month,
               unsigned int day,
               unsigned int hour,
               unsigned int minute,
               unsigned int second) {
  return (value.year == year) && (value.month == month) && (value.day == day)
         && (value.hour == hour) && (value.minute == minute) && (value.second == second);
}

void testHoldLimits(void) {
  std::cout << "hold limit table\n";
  check(gpsHoldLimitMs(0) == 0, "off means no hold");
  check(gpsHoldLimitMs(1) == 30u * 1000u, "30 s");
  check(gpsHoldLimitMs(2) == 2u * 60u * 1000u, "2 min");
  check(gpsHoldLimitMs(3) == 10u * 60u * 1000u, "10 min");
  check(gpsHoldLimitMs(4) == 60u * 60u * 1000u, "60 min");
  // A value that was never written by this firmware, for example a downgrade or
  // a corrupt NVS record, must fall back to off rather than index off the end.
  check(gpsHoldLimitMs(5) == 0, "out of range setting means off");
  check(gpsHoldLimitMs(255) == 0, "0xff setting means off");
  check(GPS_HOLD_MAX == 4, "highest valid setting");
}

void testHoldOffIsOff(void) {
  std::cout << "hold off never holds\n";
  check(!gpsHoldInBound(0, 0), "a zero limit holds nothing, not even at zero elapsed");
  check(!gpsHoldInBound(1, 0), "a zero limit holds nothing after a tick");
  check(gpsHoldRemainingMs(0, 0) == 0, "no remaining time when hold is off");
}

void testBoundIsInclusive(void) {
  std::cout << "hold bound\n";
  const uint32_t limit = gpsHoldLimitMs(1);
  check(gpsHoldInBound(0, limit), "held immediately");
  check(gpsHoldInBound(limit - 1, limit), "held one ms before the bound");
  // The bound is the last moment the fix is still sent, not the first moment it
  // is dropped, so a 30 s hold really does cover the thirtieth second.
  check(gpsHoldInBound(limit, limit), "held exactly at the bound");
  check(!gpsHoldInBound(limit + 1, limit), "dropped one ms past the bound");
  check(!gpsHoldInBound(limit * 10, limit), "still dropped long past the bound");
}

void testRemainingSaturates(void) {
  std::cout << "remaining hold time\n";
  const uint32_t limit = gpsHoldLimitMs(2);
  check(gpsHoldRemainingMs(0, limit) == limit, "full window at the start");
  check(gpsHoldRemainingMs(1000, limit) == limit - 1000, "counts down");
  check(gpsHoldRemainingMs(limit, limit) == 0, "zero at the bound");
  // Past the bound this is unsigned arithmetic, so a missing saturation would
  // wrap to roughly 49 days and the page would offer to hold the fix for weeks.
  check(gpsHoldRemainingMs(limit + 1, limit) == 0, "saturates past the bound");
  check(gpsHoldRemainingMs(0xFFFFFFFFu, limit) == 0, "saturates at a wrapped tick");
}

void testExtrapolateGate(void) {
  std::cout << "extrapolation gate\n";
  check(!gpsExtrapolateAllowed(false, true, true, 20.0), "off means no projection");
  check(!gpsExtrapolateAllowed(true, false, true, 20.0), "no course means no projection");
  check(!gpsExtrapolateAllowed(true, true, false, 20.0), "no speed means no projection");
  check(gpsExtrapolateAllowed(true, true, true, GPS_EXTRAPOLATE_MIN_SPEED_MPS),
        "the floor itself counts as motion");
  check(!gpsExtrapolateAllowed(true, true, true, GPS_EXTRAPOLATE_MIN_SPEED_MPS - 0.01),
        "just under the floor is not motion");
  check(!gpsExtrapolateAllowed(true, true, true, std::nan("")), "a NaN speed is not motion");
  check(!gpsExtrapolateAllowed(true, true, true, INFINITY), "an infinite speed is not motion");
}

void testHorizonFreezes(void) {
  std::cout << "extrapolation horizon\n";
  // Two clocks. The first argument is how long ago the receiver produced the
  // fix, which is how far the user has actually travelled. The second is how
  // long the fix has been held, which is what the horizon bounds.
  check(gpsExtrapolateElapsedMs(0, 0) == 0, "no time, no distance");
  check(gpsExtrapolateElapsedMs(10000, 5000) == 10000, "inside the horizon");

  // The whole point of the two clocks: a fix is only declared stale after the
  // freshness window, so at the first moment of a hold the user has already
  // been travelling for that window. Projecting only the hold time would drop
  // it and leave the reported position a window's worth of travel behind.
  check(gpsExtrapolateElapsedMs(31000, 1000) == 31000, "the freshness window counts as travel");

  check(gpsExtrapolateElapsedMs(GPS_EXTRAPOLATE_MAX_MS + 5000, GPS_EXTRAPOLATE_MAX_MS)
            == GPS_EXTRAPOLATE_MAX_MS + 5000,
        "the last moment on the horizon still projects the full fix age");

  // Past the horizon the projection freezes where it reached. Letting it run
  // would keep walking the reported position away from the user for the rest of
  // an hour long hold.
  const uint32_t frozen =
      gpsExtrapolateElapsedMs(GPS_EXTRAPOLATE_MAX_MS + 5000, GPS_EXTRAPOLATE_MAX_MS);
  check(gpsExtrapolateElapsedMs(GPS_EXTRAPOLATE_MAX_MS + 6000, GPS_EXTRAPOLATE_MAX_MS + 1000)
            == frozen,
        "one second past the horizon is frozen");
  check(gpsExtrapolateElapsedMs(60u * 60u * 1000u, 60u * 60u * 1000u - 5000) == frozen,
        "still frozen at the same distance an hour later");

  // A hold longer than the fix age cannot happen: the caller only holds a fix
  // it has already watched go stale. The saturation is here because the
  // subtraction is unsigned and the function is public, so a caller that
  // swapped the arguments would otherwise get a 49 day projection.
  check(gpsExtrapolateElapsedMs(40000, 0) == 40000, "a zero hold projects the whole fix age");
  check(gpsExtrapolateElapsedMs(0, 60000) == 0, "swapped arguments saturate instead of wrapping");
}

void testCourseIntegration(void) {
  std::cout << "course and speed integration\n";
  double latitude = 0.0;
  double longitude = 0.0;

  // Due north at 10 m/s for 30 s is 300 m of latitude and no longitude change.
  // 300 m is 300 / 6371000 radians, which is 0.00269796 degrees. The literal
  // pins the earth radius as well as the integration.
  const double north_deg = 0.00269796;
  check(gpsExtrapolate(48.1173, 11.51667, 0.0, 10.0, 30000, latitude, longitude),
        "due north projects");
  check(nearly(latitude, 48.1173 + north_deg, 1e-7), "north moves the latitude");
  check(nearly(longitude, 11.51667, 1e-9), "north leaves the longitude alone");

  // Due east at the same latitude covers the same ground distance but more
  // degrees of longitude, because the parallels are shorter than a meridian.
  check(gpsExtrapolate(48.1173, 11.51667, 90.0, 10.0, 30000, latitude, longitude),
        "due east projects");
  check(nearly(latitude, 48.1173, 1e-9), "east leaves the latitude alone");
  check(nearly(longitude, 11.51667 + (north_deg / std::cos(48.1173 * M_PI / 180.0)), 1e-7),
        "east moves the longitude by the cosine corrected amount");

  // Due south and west are the same distances with the sign flipped.
  check(gpsExtrapolate(48.1173, 11.51667, 180.0, 10.0, 30000, latitude, longitude),
        "due south projects");
  check(nearly(latitude, 48.1173 - north_deg, 1e-7), "south moves the latitude back");

  // The distance is linear in both speed and time.
  check(gpsExtrapolate(0.0, 0.0, 0.0, 20.0, 15000, latitude, longitude), "doubles both ways");
  check(nearly(latitude, north_deg, 1e-7), "twice the speed for half the time is the same");

  check(gpsExtrapolate(0.0, 0.0, 0.0, 10.0, 0, latitude, longitude), "zero elapsed projects");
  check(nearly(latitude, 0.0, 1e-12), "zero elapsed moves nothing");
}

void testStationaryIsNotMoved(void) {
  std::cout << "a standing user is not moved\n";
  // The gate, not the projection, is what protects a stationary user. Below the
  // floor the reported course is receiver noise, so a caller that projected
  // anyway would walk a parked photographer around the map for the whole hold.
  check(!gpsExtrapolateAllowed(true, true, true, 0.21), "0.412 knots is not motion");
  check(!gpsExtrapolateAllowed(true, true, true, 0.0), "standing still is not motion");
  check(!gpsExtrapolateAllowed(true, true, true, 1.99), "just under walking pace is not motion");
}

void testAntimeridian(void) {
  std::cout << "antimeridian wrap\n";
  double latitude = 0.0;
  double longitude = 0.0;
  // Heading east from just short of the dateline has to come out just past
  // -180, not at 180.0027, which no camera would accept.
  check(gpsExtrapolate(0.0, 179.9999, 90.0, 100.0, 30000, latitude, longitude),
        "east across the dateline projects");
  check(longitude <= 180.0 && longitude >= -180.0, "longitude stays in range going east");
  check(longitude < 0.0, "east across the dateline wraps negative");

  check(gpsExtrapolate(0.0, -179.9999, 270.0, 100.0, 30000, latitude, longitude),
        "west across the dateline projects");
  check(longitude <= 180.0 && longitude >= -180.0, "longitude stays in range going west");
  check(longitude > 0.0, "west across the dateline wraps positive");
}

void testPoleIsRejected(void) {
  std::cout << "poles are rejected\n";
  double latitude = 1.0;
  double longitude = 2.0;
  // At the pole the longitude step divides by a cosine of zero. Refusing the
  // projection leaves the caller's measured position in place, which is the
  // only sane answer there.
  check(!gpsExtrapolate(90.0, 0.0, 90.0, 10.0, 30000, latitude, longitude),
        "the north pole is refused");
  check(!gpsExtrapolate(-90.0, 0.0, 90.0, 10.0, 30000, latitude, longitude),
        "the south pole is refused");
  check((latitude == 1.0) && (longitude == 2.0), "a refused projection writes nothing");
}

void testUtcRollovers(void) {
  std::cout << "held UTC advance\n";
  utc_t out = {};

  const utc_t base = {1994, 3, 23, 12, 35, 19, 42};
  check(gpsAdvanceUtc(base, 0, out), "zero advance succeeds");
  check(utcEquals(out, 1994, 3, 23, 12, 35, 19), "zero advance changes nothing");
  check(out.centisecond == 42, "the centisecond is carried through unchanged");

  // Sub-second elapsed time is truncated, because the receiver only ever gave
  // whole seconds and inventing a centisecond would be a lie.
  check(gpsAdvanceUtc(base, 999, out), "sub second advance succeeds");
  check(utcEquals(out, 1994, 3, 23, 12, 35, 19), "sub second advance does not tick");

  check(gpsAdvanceUtc(base, 1000, out), "one second advance succeeds");
  check(utcEquals(out, 1994, 3, 23, 12, 35, 20), "one second advance ticks");

  // A 60 minute hold is the longest one the setting offers, so an hour of
  // advance has to be exact.
  check(gpsAdvanceUtc(base, 60u * 60u * 1000u, out), "an hour advance succeeds");
  check(utcEquals(out, 1994, 3, 23, 13, 35, 19), "an hour advance carries the hour");

  const utc_t minuteEdge = {2026, 9, 6, 10, 59, 59, 0};
  check(gpsAdvanceUtc(minuteEdge, 1000, out), "minute rollover succeeds");
  check(utcEquals(out, 2026, 9, 6, 11, 0, 0), "minute and hour roll together");

  // Midnight is the case a hand written conversion gets wrong, and a photo
  // stamped with the wrong day is worse than one stamped with no time at all.
  const utc_t midnight = {2026, 9, 6, 23, 59, 59, 0};
  check(gpsAdvanceUtc(midnight, 1000, out), "midnight rollover succeeds");
  check(utcEquals(out, 2026, 9, 7, 0, 0, 0), "midnight rolls the day");

  check(gpsAdvanceUtc(midnight, 60u * 60u * 1000u, out), "an hour across midnight succeeds");
  check(utcEquals(out, 2026, 9, 7, 0, 59, 59), "an hour across midnight lands on the next day");

  const utc_t monthEnd = {2026, 9, 30, 23, 59, 59, 0};
  check(gpsAdvanceUtc(monthEnd, 1000, out), "month rollover succeeds");
  check(utcEquals(out, 2026, 10, 1, 0, 0, 0), "a 30 day month rolls to the first");

  const utc_t yearEnd = {2026, 12, 31, 23, 59, 59, 0};
  check(gpsAdvanceUtc(yearEnd, 1000, out), "year rollover succeeds");
  check(utcEquals(out, 2027, 1, 1, 0, 0, 0), "the year rolls");

  // Leap years are the other half of the days-from-civil arithmetic.
  const utc_t leap = {2024, 2, 28, 23, 59, 59, 0};
  check(gpsAdvanceUtc(leap, 1000, out), "leap day rollover succeeds");
  check(utcEquals(out, 2024, 2, 29, 0, 0, 0), "2024 has a 29 February");

  const utc_t leapEnd = {2024, 2, 29, 23, 59, 59, 0};
  check(gpsAdvanceUtc(leapEnd, 1000, out), "leap day end succeeds");
  check(utcEquals(out, 2024, 3, 1, 0, 0, 0), "29 February rolls to March");

  const utc_t nonLeap = {2026, 2, 28, 23, 59, 59, 0};
  check(gpsAdvanceUtc(nonLeap, 1000, out), "non leap February succeeds");
  check(utcEquals(out, 2026, 3, 1, 0, 0, 0), "2026 has no 29 February");

  // 2100 is divisible by four and is not a leap year. A conversion that only
  // handles the four year rule gets this wrong.
  const utc_t century = {2100, 2, 28, 23, 59, 59, 0};
  check(gpsAdvanceUtc(century, 1000, out), "century rule succeeds");
  check(utcEquals(out, 2100, 3, 1, 0, 0, 0), "2100 is not a leap year");

  // 2000 is divisible by four hundred and is a leap year.
  const utc_t fourHundred = {2000, 2, 28, 23, 59, 59, 0};
  check(gpsAdvanceUtc(fourHundred, 1000, out), "four hundred year rule succeeds");
  check(utcEquals(out, 2000, 2, 29, 0, 0, 0), "2000 is a leap year");
}

void testUtcRejectsGarbage(void) {
  std::cout << "held UTC input validation\n";
  utc_t out = {9999, 9, 9, 9, 9, 9, 9};

  // A receiver that has time but no date reports year zero. Advancing that
  // would hand the camera a timestamp from the first century.
  const utc_t noDate = {0, 0, 0, 12, 0, 0, 0};
  check(!gpsAdvanceUtc(noDate, 1000, out), "a dateless fix is refused");
  check(utcEquals(out, 9999, 9, 9, 9, 9, 9), "a refused advance writes nothing");

  const utc_t preEpoch = {1969, 12, 31, 23, 59, 59, 0};
  check(!gpsAdvanceUtc(preEpoch, 1000, out), "a pre epoch year is refused");

  const utc_t badMonth = {2026, 13, 1, 0, 0, 0, 0};
  check(!gpsAdvanceUtc(badMonth, 1000, out), "month 13 is refused");

  const utc_t badDay = {2026, 9, 32, 0, 0, 0, 0};
  check(!gpsAdvanceUtc(badDay, 1000, out), "day 32 is refused");

  const utc_t badHour = {2026, 9, 6, 24, 0, 0, 0};
  check(!gpsAdvanceUtc(badHour, 1000, out), "hour 24 is refused");

  const utc_t badMinute = {2026, 9, 6, 0, 60, 0, 0};
  check(!gpsAdvanceUtc(badMinute, 1000, out), "minute 60 is refused");

  // A leap second really does report second 60, so that one is accepted.
  const utc_t leapSecond = {2026, 9, 6, 23, 59, 60, 0};
  check(gpsAdvanceUtc(leapSecond, 0, out), "a leap second is accepted");
}

}  // namespace

int main() {
  std::cout << "GPS fix hold and dead reckoning tests\n";

  testHoldLimits();
  testHoldOffIsOff();
  testBoundIsInclusive();
  testRemainingSaturates();
  testExtrapolateGate();
  testHorizonFreezes();
  testCourseIntegration();
  testStationaryIsNotMoved();
  testAntimeridian();
  testPoleIsRejected();
  testUtcRollovers();
  testUtcRejectsGarbage();

  if (g_Failures > 0) {
    std::cerr << g_Failures << " check(s) failed\n";
    return 1;
  }

  std::cout << "all GPS fix hold checks passed\n";
  return 0;
}
