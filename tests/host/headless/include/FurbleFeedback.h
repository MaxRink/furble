#ifndef FURBLE_HEADLESS_FEEDBACK_H
#define FURBLE_HEADLESS_FEEDBACK_H

#include <cstddef>

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
  static void init(void);
  static bool outputIncludesSound(output_t output);
  static bool outputIncludesLight(output_t output);
  static bool outputIncludesVibration(output_t output);
  bool supports(output_t output) const;
  size_t getOutputOptions(output_t *options) const;
  output_t outputForOption(uint8_t index) const;
  void reload(void);
  void setVolume(uint8_t volume);
  void setDisplayOff(bool off);
  void signal(event_t event, bool force = false);
  void updateBattery(uint8_t level, bool charging);
};
}  // namespace Furble

#endif
