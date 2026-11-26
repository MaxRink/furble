#ifndef FURBLE_PLATFORM_SHIM_FEEDBACK_H
#define FURBLE_PLATFORM_SHIM_FEEDBACK_H

#include <cstdint>

namespace Furble {
class Feedback {
 public:
  typedef enum {
    OUTPUT_OFF = 0,
    OUTPUT_SOUND = 1,
    OUTPUT_LIGHT = 2,
    OUTPUT_VIBRATE = 3,
    OUTPUT_SOUND_LIGHT = 4,
  } output_t;

  static bool outputIncludesSound(output_t output);
};
}  // namespace Furble

#endif
