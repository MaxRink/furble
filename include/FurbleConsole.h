#ifndef FURBLE_CONSOLE_H
#define FURBLE_CONSOLE_H

#include <cstddef>
#include <cstdint>

namespace Furble {
/**
 * Developer only serial command console.
 *
 * Built into the '-debug' environments only, gated on FURBLE_CONSOLE. Without
 * the gate every entry point below is an empty inline, so callers need no
 * preprocessor guards and release builds carry no console code.
 */
class Console {
 public:
  Console() = delete;
  ~Console() = delete;

#if defined(FURBLE_CONSOLE)
  /** Start the console transport and command task. */
  static void init(void);

  /** Mirror received GPS bytes to the console, see the 'gps raw' command. */
  static void gpsRaw(const char *data, size_t length);

  /** Print a verified GPS binary frame, see the 'gps binary' command. */
  static void gpsBinary(const uint8_t *data, size_t length);
#else
  static inline void init(void) {}
  static inline void gpsRaw(const char *, size_t) {}
  static inline void gpsBinary(const uint8_t *, size_t) {}
#endif
};
}  // namespace Furble

#endif
