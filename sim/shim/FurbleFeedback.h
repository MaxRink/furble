// The include guard matches include/FurbleFeedback.h on purpose. FurbleUI.h
// pulls the real header through its own directory, so the guard has to stop
// that second definition after this fake is included first.
#ifndef FURBLE_FEEDBACK_H
#define FURBLE_FEEDBACK_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace Furble {

// The simulator has no speaker, LED or vibration hardware. The fake reports
// no capability beyond Off so the Feedback settings menu stays hidden, which
// keeps the scripted menu routes at their existing positions.
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

  static constexpr uint8_t EVENT_SHUTTER_MASK = 1 << 0;
  static constexpr uint8_t EVENT_COUNTDOWN_MASK = 1 << 1;
  static constexpr uint8_t EVENT_CONNECTION_MASK = 1 << 2;
  static constexpr uint8_t EVENT_LOW_BATTERY_MASK = 1 << 3;
  static constexpr size_t OUTPUT_OPTION_COUNT = 5;

  static Feedback &getInstance() {
    static Feedback instance;
    return instance;
  }

  static void init(void) {}

  static bool outputIncludesSound(output_t output) {
    return (output == OUTPUT_SOUND) || (output == OUTPUT_SOUND_LIGHT);
  }

  static bool outputIncludesLight(output_t output) {
    return (output == OUTPUT_LIGHT) || (output == OUTPUT_SOUND_LIGHT);
  }

  static bool outputIncludesVibration(output_t output) { return output == OUTPUT_VIBRATE; }

  bool supports(output_t output) const { return output == OUTPUT_OFF; }

  size_t getOutputOptions(std::array<output_t, OUTPUT_OPTION_COUNT> &options) const {
    options[0] = OUTPUT_OFF;
    return 1;
  }

  output_t outputForOption(uint8_t index) const {
    (void)index;
    return OUTPUT_OFF;
  }

  void reload(void) {}

  void setVolume(uint8_t volume) { (void)volume; }

  void signal(event_t event, bool force = false) {
    (void)event;
    (void)force;
  }

  void updateBattery(uint8_t level, bool charging) {
    (void)level;
    (void)charging;
  }

 private:
  Feedback() = default;
};

}  // namespace Furble

#endif
