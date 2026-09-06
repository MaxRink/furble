#include "FurbleIMU.h"

#include <algorithm>
#include <cmath>

#include <esp_log.h>
#include <esp_timer.h>

#include <M5Unified.h>

#if defined(FURBLE_SIM)
#include "driver.h"
#endif

#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {
namespace IMU {

namespace {

// All three backends share one motion contract: a slope threshold decides
// whether a sample is quiet, and a continuous quiet window of STATIONARY_HOLD_MS
// decides when the device is stationary. The BMI270 no-motion duration and the
// MPU6886 software hold both use this same 60 s, so the three backends report
// the same state at the same time.
constexpr uint32_t STATIONARY_HOLD_MS = 60 * 1000;
// The chip engines threshold a filtered acceleration slope. This backend only
// has raw magnitude, which is noisier, so its threshold is deliberately looser
// than the 83 mg the BMI270 any-motion engine uses. MotionSource::setScale is
// the runtime knob for a board whose noise floor disagrees.
constexpr float MOTION_DELTA_G = 0.20f;

uint32_t nowMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

// The same M5.Imu read boundary the spirit level and the diagnostics page use.
// The simulator has no sensor, so under FURBLE_SIM these route to the injected
// host state instead of the bus.
bool sensorEnabled(void) {
#if defined(FURBLE_SIM)
  return Sim::imuEnabled();
#else
  return M5.Imu.isEnabled();
#endif
}

bool readAccel(float *accel) {
#if defined(FURBLE_SIM)
  Sim::imuUpdate();
  return Sim::imuGetAccel(&accel[0], &accel[1], &accel[2]);
#else
  M5.Imu.update();
  return M5.Imu.getAccel(&accel[0], &accel[1], &accel[2]);
#endif
}

class SoftwareBackend final: public MotionBackend {
 public:
  bool arm(void) override {
    if (!sensorEnabled()) {
      return false;
    }

    m_State = MotionState::MOVING;
    m_QuietSince = 0;
    return true;
  }

  void disarm(void) override {}

  bool poll(MotionState &state) override {
    float accel[3] = {};
    if (!readAccel(accel)) {
      return false;
    }

    const float magnitude =
        std::sqrt((accel[0] * accel[0]) + (accel[1] * accel[1]) + (accel[2] * accel[2]));
    const uint32_t now = nowMs();

    if (std::fabs(magnitude - 1.0f) >= MotionSource::threshold()) {
      m_QuietSince = 0;
      if (m_State == MotionState::MOVING) {
        return false;
      }
      m_State = MotionState::MOVING;
      state = m_State;
      return true;
    }

    if (m_QuietSince == 0) {
      // nowMs() is zero only for the first millisecond after boot, which is
      // long before any backend is armed, so zero stays free as the sentinel.
      m_QuietSince = (now == 0) ? 1 : now;
      return false;
    }

    if ((m_State == MotionState::MOVING) && ((now - m_QuietSince) >= STATIONARY_HOLD_MS)) {
      m_State = MotionState::STATIONARY;
      state = m_State;
      return true;
    }

    return false;
  }

  MotionState state(void) const override { return m_State; }
  Backend backend(void) const override { return Backend::SOFTWARE; }
  const char *name(void) const override { return "software"; }
  bool usesInterrupt(void) const override { return false; }
  uint32_t interruptCount(void) const override { return 0; }

 private:
  uint32_t m_QuietSince = 0;
  MotionState m_State = MotionState::MOVING;
};

}  // namespace

std::atomic<float> MotionSource::s_Scale {1.0f};

void MotionSource::setScale(float scale) {
  if (!std::isfinite(scale)) {
    return;
  }
  s_Scale.store(std::clamp(scale, 0.25f, 4.0f), std::memory_order_relaxed);
}

float MotionSource::getScale(void) {
  return s_Scale.load(std::memory_order_relaxed);
}

float MotionSource::threshold(void) {
  return MOTION_DELTA_G * getScale();
}

MotionSource &MotionSource::getInstance(void) {
  static MotionSource instance;
  return instance;
}

MotionSource::~MotionSource() {
  disarm();
}

void MotionSource::init(void) {
  if (m_Initialized) {
    return;
  }

  m_Backend = createSoftwareBackend();
  m_Initialized = true;
}

bool MotionSource::arm(void) {
  init();
  disarm();

  const uint8_t mode = Settings::load<Settings::HW_MOTION>();
  std::unique_ptr<MotionBackend> selected;

  if (mode != Settings::HW_MOTION_SOFTWARE) {
    // Each hardware backend identifies its own chip from the bus and refuses to
    // arm on anything else, so the order here is a probe, not a board table.
    // That keeps the selection identical in the simulator, where the factories
    // report which engine the modelled board carries.
    for (const auto &create : {createBMI270Backend, createMPU6886Backend}) {
      selected = create();
      if (!selected) {
        continue;
      }
      if (selected->arm()) {
        break;
      }
      selected->disarm();
      selected.reset();
    }

    if (selected) {
      m_Backend = std::move(selected);
      m_State = m_Backend->state();
      m_Armed = true;
      ESP_LOGI(LOG_TAG, "Motion backend armed: %s%s", m_Backend->name(),
               m_Backend->usesInterrupt() ? " with interrupt" : " with polling");
      return true;
    }

    if (mode == Settings::HW_MOTION_HARDWARE) {
      ESP_LOGW(LOG_TAG, "Requested hardware motion backend is unavailable");
    }
  }

  m_Backend = createSoftwareBackend();
  if (!m_Backend || !m_Backend->arm()) {
    ESP_LOGW(LOG_TAG, "Software motion backend is unavailable");
    return false;
  }

  m_State = m_Backend->state();
  m_Armed = true;
  ESP_LOGI(LOG_TAG, "Motion backend armed: software");
  return true;
}

void MotionSource::disarm(void) {
  if (m_Backend && m_Armed) {
    m_Backend->disarm();
  }
  m_Armed = false;
}

void MotionSource::poll(void) {
  if (!m_Armed || !m_Backend) {
    return;
  }

  MotionState state = m_Backend->state();
  if (m_Backend->poll(state)) {
    notify(state);
  }
}

void MotionSource::setCallback(event_callback_t callback, void *context) {
  m_Callback = callback;
  m_CallbackContext = context;
}

bool MotionSource::isArmed(void) const {
  return m_Armed;
}

MotionState MotionSource::state(void) const {
  return m_State;
}

Backend MotionSource::backend(void) const {
  return m_Backend && m_Armed ? m_Backend->backend() : Backend::NONE;
}

const char *MotionSource::backendName(void) const {
  return m_Backend && m_Armed ? m_Backend->name() : "none";
}

bool MotionSource::usesInterrupt(void) const {
  return m_Backend && m_Armed && m_Backend->usesInterrupt();
}

uint32_t MotionSource::interruptCount(void) const {
  return m_Backend && m_Armed ? m_Backend->interruptCount() : 0;
}

void MotionSource::notify(MotionState state) {
  m_State = state;
  if (m_Callback) {
    m_Callback(state, m_CallbackContext);
  }
}

std::unique_ptr<MotionBackend> createSoftwareBackend(void) {
  return std::make_unique<SoftwareBackend>();
}

}  // namespace IMU
}  // namespace Furble
