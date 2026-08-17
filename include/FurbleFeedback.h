#ifndef FURBLE_FEEDBACK_H
#define FURBLE_FEEDBACK_H

#include <array>
#include <cstddef>
#include <cstdint>

#include <lvgl.h>

namespace Furble {
class Feedback {
 public:
  /** Feedback events which can be enabled by the user. */
  typedef enum {
    SHUTTER_FIRED,
    COUNTDOWN,
    CONNECTED,
    DISCONNECTED,
    LOW_BATTERY,
  } event_t;

  /** Feedback output combinations. Values are persisted in NVS. */
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

  static Feedback &getInstance();

  /** Initialize the board capability table and cached settings. */
  static void init(void);

  /** Return whether an output value contains a speaker component. */
  static bool outputIncludesSound(output_t output);

  /** Return whether an output value contains an LED component. */
  static bool outputIncludesLight(output_t output);

  /** Return whether an output value contains a vibration component. */
  static bool outputIncludesVibration(output_t output);

  /** Return whether this board can perform an output value. */
  bool supports(output_t output) const;

  /** Build the board-filtered output roller list. */
  size_t getOutputOptions(std::array<output_t, OUTPUT_OPTION_COUNT> &options) const;

  /** Map a board-filtered output roller index to its persisted value. */
  output_t outputForOption(uint8_t index) const;

  /** Reload the live feedback settings (event mask and volume) after a menu
   * or console change. The output selection is frozen at boot and applies on
   * restart only. */
  void reload(void);

  /** Set the cached volume without persisting it, for live slider preview. */
  void setVolume(uint8_t volume);

  /** Signal an enabled feedback event without blocking the caller.
   * With force set the event mask is bypassed, for console testing. */
  void signal(event_t event, bool force = false);

  /** Feed the existing battery sampler into the low-battery event policy. */
  void updateBattery(uint8_t level, bool charging);

 private:
  struct capabilities_t {
    bool sound;
    bool light;
    bool vibration;
    int led_pin;
    bool led_active_low;
  };

  struct tone_step_t {
    uint16_t frequency;
    uint16_t duration;
    uint16_t gap;
  };

  struct pulse_step_t {
    uint16_t duration;
    uint16_t gap;
    uint8_t level;
  };

  Feedback() = default;

  void initialize(void);
  void update(void);
  void ensureTimer(void);
  bool beginSpeaker(void);
  void startTone(void);
  void stopSpeaker(void);
  void startLight(uint16_t on, uint16_t off, uint8_t count);
  void stopLight(void);
  void startVibration(uint16_t on, uint8_t count);
  void stopVibration(void);
  void setLight(bool on);
  static void timerHandler(lv_timer_t *timer);
  static uint8_t eventMask(event_t event);
  static bool isDue(uint32_t now, uint32_t deadline);

  bool m_Initialized = false;
  capabilities_t m_Capabilities = {false, false, false, -1, true};
  output_t m_Output = OUTPUT_OFF;
  uint8_t m_Events = 0;
  uint8_t m_Volume = 0;

  std::array<tone_step_t, 3> m_Tones = {};
  uint8_t m_ToneCount = 0;
  uint8_t m_ToneIndex = 0;
  bool m_TonePlaying = false;
  bool m_ToneGap = false;
  bool m_SpeakerActive = false;
  uint32_t m_ToneDeadline = 0;

  std::array<pulse_step_t, 3> m_Vibrations = {};
  uint8_t m_VibrationCount = 0;
  uint8_t m_VibrationIndex = 0;
  bool m_VibrationOn = false;
  uint32_t m_VibrationDeadline = 0;

  bool m_LightReady = false;
  uint8_t m_LightCount = 0;
  uint8_t m_LightIndex = 0;
  bool m_LightOn = false;
  uint16_t m_LightOnDuration = 0;
  uint16_t m_LightOffDuration = 0;
  uint32_t m_LightDeadline = 0;

  uint8_t m_LowBatterySamples = 0;
  bool m_LowBatteryWarned = false;
  lv_timer_t *m_Timer = nullptr;
};
}  // namespace Furble

#endif
