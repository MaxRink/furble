#ifndef FURBLE_GPS_HOLD_H
#define FURBLE_GPS_HOLD_H

#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>

namespace Furble {

// Bounded fix hold and short horizon dead reckoning, shared between GPS::update
// and the host unit test so the test asserts the exact production arithmetic.
// No FreeRTOS, LVGL, NVS or camera dependency, so it builds on the host.
//
// The receiver loses its fix in tunnels, indoors, and in every standby window.
// Without a hold the geotag stream simply stops, so a run of photos loses its
// coordinates. Holding the last fix keeps them, but an unbounded hold would
// eventually tag a photo with a position the user left hours ago. Every value
// here therefore has an explicit bound.

/** Fix hold bounds, in ms, indexed by the GPS_HOLD setting value. */
constexpr std::array<uint32_t, 5> GPS_HOLD_LIMIT_MS = {
    0, 30 * 1000, 2 * 60 * 1000, 10 * 60 * 1000, 60 * 60 * 1000,
};

/** Highest valid GPS_HOLD setting value. */
constexpr uint8_t GPS_HOLD_MAX = static_cast<uint8_t>(GPS_HOLD_LIMIT_MS.size() - 1);

// Extrapolation is a straight line, so its error grows with every turn the user
// makes. Thirty seconds is short enough that a walking or driving track has not
// usually changed direction, and it is always clipped by the hold bound too.
constexpr uint32_t GPS_EXTRAPOLATE_MAX_MS = 30 * 1000;

// Below this the reported course is receiver noise rather than a heading, and
// projecting along it would move a standing user around at random.
constexpr double GPS_EXTRAPOLATE_MIN_SPEED_MPS = 2.0;

constexpr double GPS_EARTH_RADIUS_M = 6371000.0;

/** Resolve a GPS_HOLD setting value to its bound. Out of range means off. */
inline uint32_t gpsHoldLimitMs(uint8_t setting) {
  return setting <= GPS_HOLD_MAX ? GPS_HOLD_LIMIT_MS[setting] : 0;
}

/** Is a fix lost this long ago still inside the configured hold bound? */
inline bool gpsHoldInBound(uint32_t elapsed_ms, uint32_t limit_ms) {
  return (limit_ms > 0) && (elapsed_ms <= limit_ms);
}

/** Remaining hold time, saturating at zero. */
inline uint32_t gpsHoldRemainingMs(uint32_t elapsed_ms, uint32_t limit_ms) {
  return gpsHoldInBound(elapsed_ms, limit_ms) ? limit_ms - elapsed_ms : 0;
}

/** Should a held fix be extrapolated, or replayed at its last position? */
inline bool gpsExtrapolateAllowed(bool enabled,
                                  bool course_valid,
                                  bool speed_valid,
                                  double speed_mps) {
  return enabled && course_valid && speed_valid && std::isfinite(speed_mps)
         && (speed_mps >= GPS_EXTRAPOLATE_MIN_SPEED_MPS);
}

/**
 * How far to project, in ms of travel.
 *
 * Two clocks matter and they are not the same. The distance travelled is
 * measured from the moment the receiver produced the fix, because that is when
 * the user was last actually at that position; measuring from the moment
 * furble noticed the fix was stale would silently drop the whole freshness
 * window and under-project by up to 30 seconds of travel. The horizon is
 * measured from that later moment, because the setting means "stop trusting
 * the projection this long after losing the fix". Past the horizon the
 * projection freezes where it reached rather than being abandoned, so the
 * geotag stream stays monotonic instead of jumping hundreds of metres back to
 * the measured point.
 */
inline uint32_t gpsExtrapolateElapsedMs(uint32_t fix_age_ms, uint32_t hold_elapsed_ms) {
  const uint32_t past_horizon =
      hold_elapsed_ms > GPS_EXTRAPOLATE_MAX_MS ? hold_elapsed_ms - GPS_EXTRAPOLATE_MAX_MS : 0;
  // The caller only ever holds a fix it has already seen go stale, so the fix
  // is always at least as old as the hold and this saturation cannot fire in
  // production. It is kept because the function is public and the subtraction
  // is unsigned: a caller that passed the two the other way round would get a
  // 49 day projection instead of a clamped one.
  return fix_age_ms > past_horizon ? fix_age_ms - past_horizon : 0;
}

/**
 * Advance a UTC calendar timestamp by whole elapsed seconds.
 *
 * A camera stamps the photo with the time furble hands it, so a held fix that
 * repeated its original timestamp would date every later photo to the moment
 * the fix was lost. Templated on the timestamp type so the host test does not
 * need the camera headers; the fields are the ones Camera::timesync_t carries.
 *
 * newlib has no timegm, so this converts the calendar date with the
 * days-from-civil algorithm and lets gmtime_r carry the month and year
 * rollovers back. Returns false and leaves the output alone on a timestamp the
 * receiver could not have produced.
 */
template <typename Timesync>
bool gpsAdvanceUtc(const Timesync &input, uint32_t elapsed_ms, Timesync &output) {
  if ((input.year < 1970) || (input.month < 1) || (input.month > 12) || (input.day < 1)
      || (input.day > 31) || (input.hour > 23) || (input.minute > 59) || (input.second > 60)) {
    return false;
  }

  int year = static_cast<int>(input.year);
  const int month = static_cast<int>(input.month);
  const int day = static_cast<int>(input.day);
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
  const int64_t epoch =
      days * 86400 + input.hour * 3600 + input.minute * 60 + static_cast<int64_t>(input.second);

  const time_t advanced = static_cast<time_t>(epoch + (elapsed_ms / 1000));
  struct tm updated = {};
  if (gmtime_r(&advanced, &updated) == nullptr) {
    return false;
  }

  output.year = static_cast<decltype(output.year)>(updated.tm_year + 1900);
  output.month = static_cast<decltype(output.month)>(updated.tm_mon + 1);
  output.day = static_cast<decltype(output.day)>(updated.tm_mday);
  output.hour = static_cast<decltype(output.hour)>(updated.tm_hour);
  output.minute = static_cast<decltype(output.minute)>(updated.tm_min);
  output.second = static_cast<decltype(output.second)>(updated.tm_sec);
  output.centisecond = input.centisecond;
  return true;
}

/**
 * Project a position along a constant course and speed.
 *
 * Straight line dead reckoning on a sphere, which is accurate enough over the
 * thirty second horizon and avoids the cost of a full geodesic. Returns false
 * and leaves the outputs alone when the result would not be a usable position,
 * which includes the poles, where the longitude step diverges.
 */
inline bool gpsExtrapolate(double latitude_deg,
                           double longitude_deg,
                           double course_deg,
                           double speed_mps,
                           uint32_t elapsed_ms,
                           double &out_latitude_deg,
                           double &out_longitude_deg) {
  const double radians_per_degree = M_PI / 180.0;
  const double latitude_radians = latitude_deg * radians_per_degree;
  const double cos_latitude = std::cos(latitude_radians);
  if (!(std::fabs(cos_latitude) > 1.0e-6)) {
    return false;
  }

  const double distance_m = speed_mps * (elapsed_ms / 1000.0);
  const double course_radians = course_deg * radians_per_degree;
  const double latitude =
      (latitude_radians + (distance_m * std::cos(course_radians) / GPS_EARTH_RADIUS_M))
      / radians_per_degree;
  double longitude =
      ((longitude_deg * radians_per_degree)
       + (distance_m * std::sin(course_radians) / (GPS_EARTH_RADIUS_M * cos_latitude)))
      / radians_per_degree;

  // Normalize with a remainder rather than a loop. A loop here is unbounded:
  // near a pole the longitude step is divided by a vanishing cosine, and a
  // single call could spin for billions of iterations before terminating.
  longitude = std::fmod(longitude + 180.0, 360.0);
  if (longitude < 0.0) {
    longitude += 360.0;
  }
  longitude -= 180.0;

  if (!std::isfinite(latitude) || !std::isfinite(longitude) || (latitude < -90.0)
      || (latitude > 90.0)) {
    return false;
  }

  out_latitude_deg = latitude;
  out_longitude_deg = longitude;
  return true;
}

}  // namespace Furble

#endif
