#ifndef FURBLE_GPS_FORMAT_H
#define FURBLE_GPS_FORMAT_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace Furble {

/**
 * Render a receiver sentence age into at most four characters.
 *
 * The age is unbounded. Nothing resets the last sentence tick while a receiver
 * is quiet, so an unplugged unit counts up for as long as furble runs. The GPS
 * Data page row it lands in is budgeted to fourteen characters, because a wider
 * centred row slides under the floating navigation indicator on the non-touch
 * Stick layout. A plain seconds count crosses that budget after about
 * seventeen minutes, which is exactly when a quiet receiver is worth reading.
 *
 * So: seconds up to 99, then whole minutes up to 99, then a saturating "99m+".
 * Four characters is the worst case, at both the minute rollover and the
 * saturation point.
 *
 * out must be at least 5 bytes. Anything shorter is truncated by snprintf
 * rather than overrun.
 */
inline void gpsSentenceAge(char *out, size_t size, uint32_t age_ms) {
  const uint32_t seconds = age_ms / 1000;
  if (seconds < 100) {
    std::snprintf(out, size, "%lus", (unsigned long)seconds);
    return;
  }

  const uint32_t minutes = seconds / 60;
  if (minutes < 100) {
    std::snprintf(out, size, "%lum", (unsigned long)minutes);
    return;
  }

  std::snprintf(out, size, "99m+");
}

}  // namespace Furble

#endif
