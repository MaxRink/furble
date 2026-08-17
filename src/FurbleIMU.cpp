#include "FurbleIMU.h"

#include <array>
#include <cmath>

#include <esp_log.h>
#include <esp_timer.h>

#include <M5Unified.h>

#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {
namespace IMU {

namespace {

constexpr uint32_t SOFTWARE_POLL_MS = 100;
constexpr uint32_t STATIONARY_HOLD_MS = 60 * 1000;
constexpr size_t VARIANCE_SAMPLES = 50;
constexpr float VARIANCE_THRESHOLD = 0.02f;
constexpr float MOTION_MAGNITUDE_DELTA = 0.20f;

uint32_t nowMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

class SoftwareBackend final: public MotionBackend {
 public:
  bool arm(void) override {
    if (!M5.Imu.isEnabled()) {
      return false;
    }

    m_State = MotionState::MOVING;
    m_LastPoll = 0;
    m_QuietSince = 0;
    m_SampleCount = 0;
    m_SampleNext = 0;
    m_Values.fill(0.0f);
    return true;
  }

  void disarm(void) override {}

  bool poll(MotionState &state) override {
    const uint32_t now = nowMs();
    if ((now - m_LastPoll) < SOFTWARE_POLL_MS) {
      return false;
    }
    m_LastPoll = now;

    M5.Imu.update();

    float accel[3] = {};
    if (!M5.Imu.getAccel(&accel[0], &accel[1], &accel[2])) {
      return false;
    }

    const float magnitude =
        std::sqrt((accel[0] * accel[0]) + (accel[1] * accel[1]) + (accel[2] * accel[2]));
    m_Values[m_SampleNext] = magnitude;
    m_SampleNext = (m_SampleNext + 1) % VARIANCE_SAMPLES;
    if (m_SampleCount < VARIANCE_SAMPLES) {
      m_SampleCount++;
    }

    const bool movingSample = std::fabs(magnitude - 1.0f) >= MOTION_MAGNITUDE_DELTA;
    if (movingSample) {
      m_QuietSince = 0;
      if (m_State != MotionState::MOVING) {
        m_State = MotionState::MOVING;
        state = m_State;
        return true;
      }
      return false;
    }

    if (m_SampleCount < VARIANCE_SAMPLES) {
      return false;
    }

    float mean = 0.0f;
    for (size_t index = 0; index < m_SampleCount; index++) {
      mean += m_Values[index];
    }
    mean /= static_cast<float>(m_SampleCount);

    float variance = 0.0f;
    for (size_t index = 0; index < m_SampleCount; index++) {
      const float delta = m_Values[index] - mean;
      variance += delta * delta;
    }
    variance /= static_cast<float>(m_SampleCount);

    if (variance >= VARIANCE_THRESHOLD) {
      m_QuietSince = 0;
      if (m_State != MotionState::MOVING) {
        m_State = MotionState::MOVING;
        state = m_State;
        return true;
      }
      return false;
    }

    if (m_QuietSince == 0) {
      m_QuietSince = now;
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
  std::array<float, VARIANCE_SAMPLES> m_Values = {};
  size_t m_SampleCount = 0;
  size_t m_SampleNext = 0;
  uint32_t m_LastPoll = 0;
  uint32_t m_QuietSince = 0;
  MotionState m_State = MotionState::MOVING;
};

}  // namespace

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
  const bool forceSoftware = mode == Settings::HW_MOTION_SOFTWARE;
  std::unique_ptr<MotionBackend> selected;

  if (!forceSoftware) {
    if (M5.Imu.getType() == m5::imu_t::imu_bmi270) {
      selected = createBMI270Backend();
    } else if (M5.Imu.getType() == m5::imu_t::imu_mpu6886) {
      selected = createMPU6886Backend();
    }

    if (selected && selected->arm()) {
      m_Backend = std::move(selected);
      m_State = m_Backend->state();
      m_Armed = true;
      ESP_LOGI(LOG_TAG, "Motion backend armed: %s%s", m_Backend->name(),
               m_Backend->usesInterrupt() ? " with interrupt" : " with polling");
      return true;
    }

    if (selected) {
      selected->disarm();
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
