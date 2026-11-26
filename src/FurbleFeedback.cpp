#include <M5Unified.h>
#include <driver/gpio.h>
#include <esp_log.h>

#include "FurbleBoard.h"
#include "FurbleBoardCaps.h"
#include "FurbleFeedback.h"
#include "FurbleFeedbackPolicy.h"
#include "FurblePinShare.h"
#include "FurbleTime.h"

#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

/*
 * DFS interaction (see the clock family trap in the root CLAUDE.md):
 *
 * - Sound: every sound path goes through I2S via M5Unified, and the IDF i2s
 *   driver holds ESP_PM_APB_FREQ_MAX while the channel is enabled. Tones are
 *   therefore DFS-safe without any extra pm lock here.
 * - LED: plain GPIO, DFS-immune. PM_SLP_DISABLE_GPIO is unset, so the pads
 *   hold their level through light sleep.
 * - Vibration: an I2C write to the PMIC, and the I2C driver recomputes its
 *   clock divider per transaction.
 */

namespace Furble {

namespace {
constexpr uint8_t LOW_BATTERY_LEVEL = 10;
constexpr uint8_t LOW_BATTERY_SAMPLES = 6;
constexpr uint8_t VIBRATION_LEVEL = 255;
constexpr uint32_t TIMER_PERIOD_MS = 10;

// M5PM1 low-power guide, section 1 "SPK AMP": allow about one second after the
// amplifier is enabled before playback. The rail and the amplifier come up
// well before the codec can drive them, so a tone started immediately loses
// its attack. This is the documented settle, not a measured one; the v1 round
// is 1,000 ms and a hardware sweep can move it.
constexpr uint32_t AMP_SETTLE_MS = 1000;

// How long the amplifier stays enabled after its last tone. The settle above is
// paid per amplifier enable, so power-cycling the session after every pattern
// delayed the first tone of every pattern by the settle window and put the beep
// a second behind the LED and the vibration. Keeping the session open across a
// burst of feedback pays the settle once, while an idle device still returns the
// rail off. The longest burst the UI produces is the intervalometer: it
// announces its countdown on a one second cadence over the last three seconds
// and then fires the shutter. Ten seconds spans that and leaves room for a
// connect or disconnect on the same action.
constexpr uint32_t AMP_IDLE_TIMEOUT_MS = 10000;

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

  // The output selection is frozen here for the whole boot. It decides the
  // M5Unified speaker configuration in Platform::getInstance(), so a change
  // applies on restart only. The menu offers a restart button for it.
  m_Output = static_cast<output_t>(Settings::load<uint8_t>(Settings::FB_OUTPUT));

  // G19 is also the IR pin on the Plus2, so every LED write goes through the
  // PinShare arbitration: IR takes the pin for a frame and restores this level
  // afterwards. The Tough has the Core2 NS4168 1 W amplifier (AXP192 GPIO2 is
  // SPK_EN on the Tough pinmap) but no LED or vibration motor.
  // The LED stage decides the polarity: the StickC and Plus drive the cathode of
  // an LED whose anode is on 3.3 V, while the Plus2 drives an NPN base from
  // G19, so a high pin lights it. Getting this backwards would drive the Plus2
  // pin high at boot, which keys that board's IR emitter through the same
  // transistor (plan 350).
  const auto &board = Board::profile();
  m_Capabilities = {board.feedbackSound, board.feedbackLight, board.feedbackVibration, board.ledPin,
                    board.ledActiveLow};

  // Leave the LED pin untouched unless the boot output actually blinks it. On
  // the Plus2 G19 also drives the IR emitter through the same transistor, so
  // with the feature off the pad stays an input and neither device is keyed.
  // The arbitration still hands the pin back after an IR frame; with no
  // recorded level it releases the pad as an input instead of choosing one.
  if (m_Capabilities.light && outputIncludesLight(m_Output)) {
    (void)gpio_set_direction(static_cast<gpio_num_t>(m_Capabilities.led_pin), GPIO_MODE_OUTPUT);
    m_LightReady = true;
    setLight(false);
  }

  m_Initialized = true;
  reload();

  // M5.begin() opens the speaker when internal_spk is enabled. Keep the
  // amplifier rail off until the first event instead. The stored volume is
  // handed over unchanged here because nothing is playing: the battery ceiling
  // is applied where a pattern actually sounds (applyVolume()), and the stored
  // setting is never rewritten.
  if (m_Capabilities.sound && outputIncludesSound(m_Output)) {
    M5.Speaker.setVolume(m_Volume);
    M5.Speaker.end();
  }

  ESP_LOGI(TAG, "capabilities: sound=%u light=%u vibration=%u", m_Capabilities.sound,
           m_Capabilities.light, m_Capabilities.vibration);
}

void Feedback::reload(void) {
  // FB_OUTPUT is deliberately not re-read. The output selection configures
  // the M5Unified speaker at boot, so re-reading it here would half-apply a
  // pending change (selected Sound, silent speaker). Only the event mask and
  // volume are live. That also keeps this safe to call from any task: it is
  // two byte stores into the cache.
  m_Events = Settings::load<uint8_t>(Settings::FB_EVENTS);
  m_Volume = Settings::load<uint8_t>(Settings::FB_VOLUME);
}

void Feedback::setVolume(uint8_t volume) {
  m_Volume = volume;
  if (m_SpeakerActive) {
    applyVolume();
  }
}

void Feedback::setDisplayOff(bool off) {
  m_DisplayOff = off;
  if (off) {
    stopLight();
  }
}

void Feedback::setTimerPathActive(bool active) {
  // Console mode skips lv_task_handler(), so no LVGL timer fires and nothing can
  // retry a parked tone. The UI sets this when the display mode changes.
  m_TimerPathActive = active;
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
  return Time::deadlineReached(now, deadline);
}

void Feedback::signal(event_t event, bool force) {
  if (!m_Initialized || (m_Output == OUTPUT_OFF) || !supports(m_Output)
      || (!force && ((m_Events & eventMask(event)) == 0))) {
    return;
  }

  const bool sound = outputIncludesSound(m_Output);
  const bool light = !m_DisplayOff && outputIncludesLight(m_Output);
  const bool vibration = outputIncludesVibration(m_Output);

  // A new pattern silences the current tone but keeps the speaker session
  // open. Ending it here would power-cycle the amplifier for nothing.
  if (m_SpeakerActive) {
    M5.Speaker.stop();
    if (!m_TimerPathActive) {
      // Nothing runs update() in console mode, so the idle close there can
      // never fire and the amplifier would stay enabled for the rest of the
      // session. The settle is skipped in that mode anyway, so ending the
      // session here costs no tone and returns the rail.
      stopSpeaker();
    }
  }
  m_TonePlaying = false;
  m_ToneGap = false;
  m_ToneSettlePending = false;
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
    // One power-source reading per pattern, not per tone. The slider can
    // otherwise drive applyVolume() at menu speed, and every reading is M5PM1
    // I2C traffic. The sample runs here, on the signalling task, so the timer
    // path never pays for it and a pattern that reuses the open amplifier still
    // sees the current source.
#if FURBLE_HAS_M5PM1
    m_SessionOnBattery = onBatteryPower();
#endif
    m_ToneIndex = 0;
    m_TonePlaying = false;
    m_ToneGap = false;
    startTone();
  }

  if (light) {
    startLight(m_LightOnDuration, m_LightOffDuration, m_LightCount);
  }

  if (vibration) {
    startVibration(m_Vibrations[0].duration, m_VibrationCount);
  }

  ensureTimer();
}

bool Feedback::beginSpeaker(void) {
#if FURBLE_HAS_M5PM1
  // The M5PM1 powers the amplifier and the first I2C transaction after its
  // idle sleep only wakes it and fails (root CLAUDE.md trap). The speaker is
  // closed between patterns, so the enable path can hit exactly that first
  // transaction, and M5.Speaker.begin() does not surface the I2C status.
  // Pre-wake the PMIC with a harmless retried read.
  Platform::getInstance().wakeM5PM1();
#endif

  bool ok = M5.Speaker.begin();
  if (!ok) {
    // One retry, the begin path may touch a PMIC on other boards too.
    ok = M5.Speaker.begin();
  }
  if (!ok) {
    ESP_LOGW(TAG, "Speaker begin failed, dropping tone pattern");
    return false;
  }

  m_SpeakerActive = true;
#if FURBLE_HAS_M5PM1
  // The M5PM1 low-power guide, section 1 "SPK AMP", is about the StickS3
  // amplifier, which is the only one this image drives. Record the deadline
  // and let startTone() wait it out on the feedback timer: nothing blocks this
  // task and no mutex is held. The window is per amplifier enable, so a session
  // held open across patterns by AMP_IDLE_TIMEOUT_MS does not pay it again.
  m_AmpSettleDeadline = Platform::getInstance().tick() + AMP_SETTLE_MS;
  m_AmpSettlePending = true;
  m_SpeakerIdleDeadline = m_AmpSettleDeadline + AMP_IDLE_TIMEOUT_MS;
#endif
  return true;
}

void Feedback::startTone(void) {
  if (m_ToneIndex >= m_ToneCount) {
    m_ToneCount = 0;
    stopSpeaker();
    return;
  }

  if (!m_SpeakerActive && !beginSpeaker()) {
    m_ToneCount = 0;
    return;
  }

  const uint32_t now = Platform::getInstance().tick();
  if (m_AmpSettlePending) {
    if (m_TimerPathActive && !isDue(now, m_AmpSettleDeadline)) {
      // Still inside the amplifier's settle window. Park the first tone and
      // let the feedback timer retry: update() keeps running while m_ToneCount
      // is non-zero, so no delay happens on this task and no mutex is held.
      m_TonePlaying = false;
      m_ToneSettlePending = true;
      m_ToneDeadline = m_AmpSettleDeadline;
      return;
    }
    // Either the window has passed, or the timer path cannot run and a parked
    // tone would never be retried. Console mode skips lv_task_handler(), so
    // play now and accept the lost attack: a beep that never sounds is worse
    // than a beep without its attack.
    m_AmpSettlePending = false;
  }
  m_ToneSettlePending = false;

  applyVolume();
  M5.Speaker.tone(m_Tones[m_ToneIndex].frequency, m_Tones[m_ToneIndex].duration);
  m_TonePlaying = true;
  m_ToneDeadline = now + m_Tones[m_ToneIndex].duration;
}

void Feedback::stopSpeaker(void) {
  if (m_SpeakerActive) {
    M5.Speaker.stop();
    M5.Speaker.end();
    m_SpeakerActive = false;
  }
  m_TonePlaying = false;
  m_ToneGap = false;
  // The amplifier is closed, so the next session enables it again and owes a
  // fresh settle window. Nothing is owed for a session that never opened.
  m_ToneSettlePending = false;
  m_AmpSettlePending = false;
  m_AmpSettleDeadline = 0;
  m_SpeakerIdleDeadline = 0;
}

void Feedback::applyVolume(void) {
  // M5Stack StickS3 documentation, "Speaker Volume Notice": keep the speaker
  // volume at or below 75 percent while the device runs from the battery, or
  // the amplifier's current can brown the supply out and reboot the device.
  // The notice is about that board's rail, so only the M5PM1 image samples the
  // power source and only that image can set m_SessionOnBattery.
  // Only what reaches the amplifier is capped; m_Volume keeps the configured
  // setting and the NVS record is never rewritten.
  const uint8_t volume = FeedbackPolicy::effectiveVolume(m_Volume, m_SessionOnBattery);
  if ((volume != m_Volume) && !m_VolumeClampLogged) {
    m_VolumeClampLogged = true;
    ESP_LOGW(TAG, "Volume %u capped to %u on battery (StickS3 speaker notice)", m_Volume, volume);
  }
  M5.Speaker.setVolume(volume);
}

bool Feedback::onBatteryPower(void) {
  auto &platform = Platform::getInstance();
  // A board that reports neither VBUS nor a charge state cannot tell its power
  // source apart, so its configured volume stands. VBUS sense is a compile-time
  // capability, so the charge-state capability is only asked for when VBUS is
  // not readable. A single `||` over both stored the capability and then could
  // not read it on the boards that do sense VBUS, which clang-analyzer reports
  // as a dead store; asking only when it matters removes both the warning and a
  // needless platform call.
  bool telemetry = FURBLE_HAS_VBUS_SENSE != 0;
  if (!telemetry) {
    telemetry = platform.getBatteryCaps().charging;
  }

  // A failed PMIC read must never be read as mains power. M5Unified's
  // readRegister8 returns value_or(0), so a failed VBUS read is 0 mV, and the
  // StickS3 charge flag is a GPIO input whose failing read returns false, which
  // isCharging() maps to is_charging. A failed read therefore looks exactly
  // like "on USB, charging". Only a positive VBUS reading is evidence of mains
  // power, and a zero reading is retried whatever the charge flag says: the
  // first transaction after the PMIC idle sleep only wakes it, and the safe
  // direction for the amplifier is the cap.
  int16_t vbus = platform.getBatteryStatus().vbus;
  if (telemetry && (vbus == 0)) {
    vbus = platform.getBatteryStatus().vbus;
  }
  return FeedbackPolicy::onBatteryPower(telemetry, vbus > 0);
}

void Feedback::setLight(bool on) {
  // Only drive the pin when initialize() configured it, see the Plus2 IR
  // emitter note there.
  if (!m_LightReady) {
    return;
  }

  // The request is recorded even while an IR frame owns the pin, so the frame
  // hand-over restores the blink state instead of the RMT idle level.
  (void)PinShare::requestLed(PinShare::ledLevel(on, m_Capabilities.led_active_low));
}

void Feedback::startLight(uint16_t on, uint16_t off, uint8_t count) {
  if (!m_Capabilities.light || m_DisplayOff || count == 0) {
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

void Feedback::startVibration(uint16_t on, uint8_t count) {
  if (!m_Capabilities.vibration || count == 0) {
    return;
  }

  stopVibration();
  m_VibrationCount = count;
  m_VibrationIndex = 0;
  m_VibrationOn = true;
  M5.Power.setVibration(m_Vibrations[0].level);
  m_VibrationDeadline = Platform::getInstance().tick() + on;
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
        m_TonePlaying = false;
        if (m_ToneIndex + 1 < m_ToneCount) {
          // Keep the speaker session open across the whole pattern. Ending
          // it between tones power-cycles the amplifier mid-pattern, which
          // pops audibly and can miss the short gap deadlines.
          m_ToneGap = m_Tones[m_ToneIndex].gap > 0;
          if (m_ToneGap) {
            m_ToneDeadline = now + m_Tones[m_ToneIndex].gap;
          } else {
            m_ToneIndex++;
            startTone();
          }
        } else {
          // Final tone of the pattern. The session stays open for the idle
          // window below instead of ending here: ending it now powers the
          // amplifier down, and every later pattern would then re-pay the
          // settle window before its first tone.
          m_ToneCount = 0;
          m_SpeakerIdleDeadline = now + AMP_IDLE_TIMEOUT_MS;
        }
      }
    } else if (m_ToneSettlePending) {
      // The first tone was held for the amplifier settle window. startTone()
      // decides again, and clears the flag when the deadline has passed.
      if (isDue(now, m_ToneDeadline)) {
        startTone();
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

  if (m_SpeakerActive && (m_ToneCount == 0) && isDue(now, m_SpeakerIdleDeadline)) {
    // No pattern used the open amplifier inside the idle window. Close it so
    // the rail is off between bursts, which is the whole reason the session is
    // not simply left open for the lifetime of the UI.
    stopSpeaker();
  }

  if ((m_ToneCount == 0) && (m_LightCount == 0) && (m_VibrationCount == 0) && (m_Timer != nullptr)
      && !m_SpeakerActive) {
    // The open session keeps the timer running so the idle close above can
    // happen. Once the amplifier is closed there is nothing left to service.
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
