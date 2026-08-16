#include <M5Unified.h>
#include <driver/gpio.h>
#include <esp_log.h>

#include "FurbleFeedback.h"

#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {

namespace {
constexpr uint8_t LOW_BATTERY_LEVEL = 10;
constexpr uint8_t LOW_BATTERY_SAMPLES = 6;
constexpr uint8_t VIBRATION_LEVEL = 255;
constexpr uint32_t TIMER_PERIOD_MS = 10;

constexpr Feedback::output_t OUTPUTS[] = {
    Feedback::OUTPUT_OFF,     Feedback::OUTPUT_SOUND,       Feedback::OUTPUT_LIGHT,
    Feedback::OUTPUT_VIBRATE, Feedback::OUTPUT_SOUND_LIGHT,
};

constexpr const char *TAG = "feedback";
}  // namespace

Feedback &Feedback::getInstance(void) {
  static Feedback instance;
  return instance;
}

void Feedback::init(void) {
  getInstance().initialize();
}

bool Feedback::outputIncludesSound(output_t output) {
  return (output == OUTPUT_SOUND) || (output == OUTPUT_SOUND_LIGHT);
}

bool Feedback::outputIncludesLight(output_t output) {
  return (output == OUTPUT_LIGHT) || (output == OUTPUT_SOUND_LIGHT);
}

bool Feedback::outputIncludesVibration(output_t output) {
  return output == OUTPUT_VIBRATE;
}

bool Feedback::supports(output_t output) const {
  switch (output) {
    case OUTPUT_OFF:
    case OUTPUT_SOUND:
    case OUTPUT_LIGHT:
    case OUTPUT_VIBRATE:
    case OUTPUT_SOUND_LIGHT:
      break;
    default:
      return false;
  }

  if (output == OUTPUT_OFF) {
    return true;
  }

  if (outputIncludesSound(output) && !m_Capabilities.sound) {
    return false;
  }
  if (outputIncludesLight(output) && !m_Capabilities.light) {
    return false;
  }
  if (outputIncludesVibration(output) && !m_Capabilities.vibration) {
    return false;
  }

  return true;
}

size_t Feedback::getOutputOptions(std::array<output_t, OUTPUT_OPTION_COUNT> &options) const {
  size_t count = 0;
  for (const auto output : OUTPUTS) {
    if (supports(output)) {
      options[count++] = output;
    }
  }
  return count;
}

Feedback::output_t Feedback::outputForOption(uint8_t index) const {
  std::array<output_t, OUTPUT_OPTION_COUNT> options = {};
  size_t count = getOutputOptions(options);
  return (index < count) ? options[index] : OUTPUT_OFF;
}

void Feedback::initialize(void) {
  if (m_Initialized) {
    reload();
    return;
  }

  switch (M5.getBoard()) {
    case m5::board_t::board_M5StickC:
      m_Capabilities = {false, true, false, GPIO_NUM_10, true};
      break;

    case m5::board_t::board_M5StickCPlus:
      m_Capabilities = {true, true, false, GPIO_NUM_10, true};
      break;

    case m5::board_t::board_M5StickCPlus2:
      // G19 is also the IR pin on Plus2. There is no IR receiver backend in
      // this branch, so a future IR receive path must guard this blink.
      m_Capabilities = {true, true, false, GPIO_NUM_19, true};
      break;

    case m5::board_t::board_M5StickS3:
      m_Capabilities = {true, false, false, -1, true};
      break;

    case m5::board_t::board_M5Stack:
      m_Capabilities = {true, false, false, -1, true};
      break;

    case m5::board_t::board_M5StackCore2:
      m_Capabilities = {true, false, true, -1, true};
      break;

    default:
      m_Capabilities = {false, false, false, -1, true};
      break;
  }

  if (m_Capabilities.light) {
    (void)gpio_set_direction(static_cast<gpio_num_t>(m_Capabilities.led_pin), GPIO_MODE_OUTPUT);
    setLight(false);
  }

  m_Initialized = true;
  reload();

  // M5.begin() opens the speaker when internal_spk is enabled. Keep the
  // amplifier rail off until the first event instead.
  if (m_Capabilities.sound && outputIncludesSound(m_Output)) {
    M5.Speaker.setVolume(m_Volume);
    M5.Speaker.end();
  }

  ESP_LOGI(TAG, "capabilities: sound=%u light=%u vibration=%u", m_Capabilities.sound,
           m_Capabilities.light, m_Capabilities.vibration);
}

void Feedback::reload(void) {
  m_Output = static_cast<output_t>(Settings::load<uint8_t>(Settings::FB_OUTPUT));
  m_Events = Settings::load<uint8_t>(Settings::FB_EVENTS);
  m_Volume = Settings::load<uint8_t>(Settings::FB_VOLUME);

  if (!m_Initialized) {
    return;
  }

  if (!supports(m_Output)) {
    stopSpeaker();
    m_ToneCount = 0;
    stopLight();
    stopVibration();
  } else {
    if (!outputIncludesSound(m_Output)) {
      stopSpeaker();
      m_ToneCount = 0;
    }
    if (!outputIncludesLight(m_Output)) {
      stopLight();
    }
    if (!outputIncludesVibration(m_Output)) {
      stopVibration();
    }
  }
}

void Feedback::ensureTimer(void) {
  if (m_Timer == nullptr) {
    m_Timer = lv_timer_create(timerHandler, TIMER_PERIOD_MS, this);
  }
  if (m_Timer != nullptr) {
    lv_timer_resume(m_Timer);
  }
}

uint8_t Feedback::eventMask(event_t event) {
  switch (event) {
    case SHUTTER_FIRED:
      return EVENT_SHUTTER_MASK;
    case COUNTDOWN:
      return EVENT_COUNTDOWN_MASK;
    case CONNECTED:
    case DISCONNECTED:
      return EVENT_CONNECTION_MASK;
    case LOW_BATTERY:
      return EVENT_LOW_BATTERY_MASK;
  }
  return 0;
}

bool Feedback::isDue(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

void Feedback::signal(event_t event) {
  if (!m_Initialized || (m_Output == OUTPUT_OFF) || !supports(m_Output)
      || ((m_Events & eventMask(event)) == 0)) {
    return;
  }

  const bool sound = outputIncludesSound(m_Output);
  const bool light = outputIncludesLight(m_Output);
  const bool vibration = outputIncludesVibration(m_Output);

  stopSpeaker();
  m_ToneCount = 0;
  m_LightCount = 0;
  m_VibrationCount = 0;

  switch (event) {
    case SHUTTER_FIRED:
      m_Tones[0] = {4000, 40, 0};
      m_ToneCount = 1;
      m_LightOnDuration = 40;
      m_LightOffDuration = 0;
      m_LightCount = 1;
      m_Vibrations[0] = {60, 0, VIBRATION_LEVEL};
      m_VibrationCount = 1;
      break;

    case COUNTDOWN:
      m_Tones[0] = {2000, 40, 0};
      m_ToneCount = 1;
      m_LightOnDuration = 40;
      m_LightOffDuration = 0;
      m_LightCount = 1;
      break;

    case CONNECTED:
      m_Tones[0] = {2000, 60, 20};
      m_Tones[1] = {3000, 60, 0};
      m_ToneCount = 2;
      m_LightOnDuration = 60;
      m_LightOffDuration = 80;
      m_LightCount = 2;
      m_Vibrations[0] = {100, 0, VIBRATION_LEVEL};
      m_VibrationCount = 1;
      break;

    case DISCONNECTED:
      m_Tones[0] = {3000, 60, 20};
      m_Tones[1] = {2000, 60, 0};
      m_ToneCount = 2;
      m_LightOnDuration = 60;
      m_LightOffDuration = 100;
      m_LightCount = 3;
      m_Vibrations[0] = {100, 100, VIBRATION_LEVEL};
      m_Vibrations[1] = {100, 0, VIBRATION_LEVEL};
      m_VibrationCount = 2;
      break;

    case LOW_BATTERY:
      m_Tones[0] = {1000, 200, 100};
      m_Tones[1] = {1000, 200, 0};
      m_ToneCount = 2;
      m_LightOnDuration = 100;
      m_LightOffDuration = 300;
      m_LightCount = 3;
      m_Vibrations[0] = {150, 150, VIBRATION_LEVEL};
      m_Vibrations[1] = {150, 0, VIBRATION_LEVEL};
      m_VibrationCount = 2;
      break;
  }

  if (!sound) {
    m_ToneCount = 0;
  }
  if (!light) {
    m_LightCount = 0;
  }
  if (!vibration) {
    m_VibrationCount = 0;
  }

  if (sound) {
    m_ToneIndex = 0;
    m_TonePlaying = false;
    m_ToneGap = false;
    startTone();
  }

  if (light) {
    startLight(m_LightOnDuration, m_LightOffDuration, m_LightCount);
  }

  if (vibration) {
    startVibration(m_Vibrations[0].duration, m_Vibrations[0].gap, m_VibrationCount);
  }

  ensureTimer();
}

void Feedback::startTone(void) {
  if (m_ToneIndex >= m_ToneCount) {
    m_ToneCount = 0;
    return;
  }

  if (!M5.Speaker.begin()) {
    m_ToneCount = 0;
    return;
  }
  M5.Speaker.setVolume(m_Volume);
  M5.Speaker.tone(m_Tones[m_ToneIndex].frequency, m_Tones[m_ToneIndex].duration);
  m_SpeakerActive = true;
  m_TonePlaying = true;
  m_ToneDeadline = Platform::getInstance().tick() + m_Tones[m_ToneIndex].duration;
}

void Feedback::stopSpeaker(void) {
  if (m_SpeakerActive) {
    M5.Speaker.stop();
    M5.Speaker.end();
    m_SpeakerActive = false;
  }
  m_TonePlaying = false;
  m_ToneGap = false;
}

void Feedback::setLight(bool on) {
  if (!m_Capabilities.light) {
    return;
  }

  const int level = on == m_Capabilities.led_active_low ? 0 : 1;
  (void)gpio_set_level(static_cast<gpio_num_t>(m_Capabilities.led_pin), level);
}

void Feedback::startLight(uint16_t on, uint16_t off, uint8_t count) {
  if (!m_Capabilities.light || count == 0) {
    return;
  }

  stopLight();
  m_LightOnDuration = on;
  m_LightOffDuration = off;
  m_LightCount = count;
  m_LightIndex = 0;
  m_LightOn = true;
  setLight(true);
  m_LightDeadline = Platform::getInstance().tick() + on;
}

void Feedback::stopLight(void) {
  m_LightCount = 0;
  m_LightIndex = 0;
  m_LightOn = false;
  setLight(false);
}

void Feedback::startVibration(uint16_t on, uint16_t off, uint8_t count) {
  if (!m_Capabilities.vibration || count == 0) {
    return;
  }

  stopVibration();
  m_VibrationCount = count;
  m_VibrationIndex = 0;
  m_VibrationOn = true;
  M5.Power.setVibration(m_Vibrations[0].level);
  m_VibrationDeadline = Platform::getInstance().tick() + on;
  (void)off;
}

void Feedback::stopVibration(void) {
  m_VibrationCount = 0;
  m_VibrationIndex = 0;
  m_VibrationOn = false;
  if (m_Capabilities.vibration) {
    M5.Power.setVibration(0);
  }
}

void Feedback::update(void) {
  uint32_t now = Platform::getInstance().tick();

  if (m_ToneCount > 0) {
    if (m_TonePlaying) {
      if (!isDue(now, m_ToneDeadline) || M5.Speaker.isPlaying()) {
        // Keep the amplifier powered until the speaker backend reports that
        // the tone has ended.
      } else {
        stopSpeaker();
        if (m_ToneIndex + 1 < m_ToneCount) {
          m_ToneGap = m_Tones[m_ToneIndex].gap > 0;
          if (m_ToneGap) {
            m_ToneDeadline = now + m_Tones[m_ToneIndex].gap;
          } else {
            m_ToneIndex++;
            startTone();
          }
        } else {
          m_ToneCount = 0;
        }
      }
    } else if (m_ToneGap && isDue(now, m_ToneDeadline)) {
      m_ToneGap = false;
      m_ToneIndex++;
      startTone();
    }
  }

  if (m_LightCount > 0 && isDue(now, m_LightDeadline)) {
    if (m_LightOn) {
      setLight(false);
      m_LightOn = false;
      if (m_LightIndex + 1 >= m_LightCount) {
        m_LightCount = 0;
      } else {
        m_LightDeadline = now + m_LightOffDuration;
      }
    } else {
      m_LightIndex++;
      setLight(true);
      m_LightOn = true;
      m_LightDeadline = now + m_LightOnDuration;
    }
  }

  if (m_VibrationCount > 0 && isDue(now, m_VibrationDeadline)) {
    if (m_VibrationOn) {
      M5.Power.setVibration(0);
      m_VibrationOn = false;
      const auto &step = m_Vibrations[m_VibrationIndex];
      if (m_VibrationIndex + 1 >= m_VibrationCount) {
        m_VibrationCount = 0;
      } else {
        m_VibrationDeadline = now + step.gap;
      }
    } else {
      m_VibrationIndex++;
      M5.Power.setVibration(m_Vibrations[m_VibrationIndex].level);
      m_VibrationOn = true;
      m_VibrationDeadline = now + m_Vibrations[m_VibrationIndex].duration;
    }
  }

  if ((m_ToneCount == 0) && (m_LightCount == 0) && (m_VibrationCount == 0)
      && (m_Timer != nullptr)) {
    lv_timer_pause(m_Timer);
  }
}

void Feedback::timerHandler(lv_timer_t *timer) {
  auto *feedback = static_cast<Feedback *>(lv_timer_get_user_data(timer));
  feedback->update();
}

void Feedback::updateBattery(uint8_t level, bool charging) {
  if (charging) {
    m_LowBatterySamples = 0;
    m_LowBatteryWarned = false;
    return;
  }

  if (level > LOW_BATTERY_LEVEL) {
    m_LowBatterySamples = 0;
    return;
  }

  if (m_LowBatterySamples < LOW_BATTERY_SAMPLES) {
    m_LowBatterySamples++;
  }

  if ((m_LowBatterySamples >= LOW_BATTERY_SAMPLES) && !m_LowBatteryWarned) {
    m_LowBatteryWarned = true;
    signal(LOW_BATTERY);
  }
}

}  // namespace Furble
