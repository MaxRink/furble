// Host feedback shim for the console command suite.
//
// The real header pulls in LVGL. The console names the event enumerators and
// the highest output value, and asks the cache to reload after a settings
// write, so the double carries exactly that.
#ifndef FURBLE_FEEDBACK_H
#define FURBLE_FEEDBACK_H

#include <cstddef>
#include <cstdint>

namespace Furble {

class Feedback {
 public:
  typedef enum {
    SHUTTER_FIRED,
    COUNTDOWN,
    CONNECTED,
    DISCONNECTED,
    LOW_BATTERY,
  } event_t;

  typedef enum {
    OUTPUT_OFF = 0,
    OUTPUT_SOUND = 1,
    OUTPUT_LIGHT = 2,
    OUTPUT_VIBRATE = 3,
    OUTPUT_SOUND_LIGHT = 4,
  } output_t;

  static Feedback &getInstance(void);

  void reload(void);

 private:
  Feedback() = default;
};

}  // namespace Furble

#endif
