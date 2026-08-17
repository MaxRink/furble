#include <array>

#include "Device.h"

#include "FurbleControl.h"
#include "FurblePower.h"
#include "FurbleSettings.h"

namespace Furble {

namespace {

constexpr uint32_t RSSI_SAMPLE_INTERVAL_MS = 5000;
constexpr float RSSI_FILTER_ALPHA = 0.25f;
constexpr int8_t RSSI_STEP_DOWN_DBM = -60;
constexpr int8_t RSSI_STEP_UP_DBM = -80;
constexpr uint8_t RSSI_STEP_DOWN_SAMPLES = 3;
constexpr uint8_t RSSI_STEP_UP_SAMPLES = 2;

constexpr std::array<esp_power_level_t, 3> POWER_LEVELS = {
    ESP_PWR_LVL_P3,
    ESP_PWR_LVL_P6,
    ESP_PWR_LVL_P9,
};

int powerIndex(esp_power_level_t power) {
  for (size_t i = 0; i < POWER_LEVELS.size(); i++) {
    if (POWER_LEVELS[i] == power) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace

Control::Target::Target(Camera *camera) {
  m_Camera = camera;
  m_Queue = xQueueCreate(m_QueueLength, sizeof(cmd_t));
}

Control::Target::~Target() {
  vQueueDelete(m_Queue);
  m_Queue = NULL;
  m_Camera->disconnect();
  m_Camera = NULL;
}

Camera *Control::Target::getCamera(void) const {
  return m_Camera;
}

void Control::Target::sendCommand(cmd_t cmd) {
  BaseType_t ret = xQueueSend(m_Queue, &cmd, 0);
  if (ret != pdTRUE) {
    ESP_LOGE(LOG_TAG, "Failed to send command to target.");
  }
}

Control::cmd_t Control::Target::getCommand(void) {
  cmd_t cmd = CMD_ERROR;
  BaseType_t ret = xQueueReceive(m_Queue, &cmd, pdMS_TO_TICKS(50));
  if (ret != pdTRUE) {
    return CMD_ERROR;
  }
  return cmd;
}

void Control::Target::updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync) {
  m_GPS = gps;
  m_Timesync = timesync;
}

void Control::Target::task(void) {
  const char *name = m_Camera->getName().c_str();

  while (true) {
    cmd_t cmd = this->getCommand();
    switch (cmd) {
      case CMD_SHUTTER_PRESS:
        ESP_LOGI(LOG_TAG, "shutterPress(%s)", name);
        m_Camera->shutterPress();
        break;
      case CMD_SHUTTER_RELEASE:
        ESP_LOGI(LOG_TAG, "shutterRelease(%s)", name);
        m_Camera->shutterRelease();
        break;
      case CMD_FOCUS_PRESS:
        ESP_LOGI(LOG_TAG, "focusPress(%s)", name);
        m_Camera->focusPress();
        break;
      case CMD_FOCUS_RELEASE:
        ESP_LOGI(LOG_TAG, "focusRelease(%s)", name);
        m_Camera->focusRelease();
        break;
      case CMD_GPS_UPDATE:
        ESP_LOGI(LOG_TAG, "updateGeoData(%s)", name);
        m_Camera->updateGeoData(m_GPS, m_Timesync);
        break;
      case CMD_DISCONNECT:
        m_Camera->setActive(false);
        goto task_exit;
      case CMD_ERROR:
        // ignore continue
        break;
      default:
        ESP_LOGE(LOG_TAG, "Invalid control command %d.", cmd);
    }
  }
task_exit:
  m_Stopped = true;
  vTaskDelete(NULL);
}

Control &Control::getInstance(void) {
  static Control instance;
  if (instance.m_Queue == NULL) {
    instance.m_Queue = xQueueCreate(m_QueueLength, sizeof(cmd_t));
    if (instance.m_Queue == NULL) {
      ESP_LOGE(LOG_TAG, "Failed to create control queue.");
      abort();
    }

    // First call runs after Settings::init() in app_main. Load the user
    // transmit power cap so boot matches Device::init() instead of pinning
    // the compile-time default.
    instance.m_Power = Settings::load<esp_power_level_t>(Settings::TX_POWER);
    instance.m_AdaptivePower = instance.m_Power;
  }

  return instance;
}

Control::state_t Control::connectAll(void) {
  static uint32_t failcount = 0;
  uint32_t timeout = m_InfiniteReconnect ? TIMEOUT_INFINITE_MS : TIMEOUT_DEFAULT_MS;
  std::vector<Camera *> cameras;

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);

    // Snapshot cameras so the mutex is not held during connection attempts.
    for (const auto &target : m_Targets) {
      Camera *camera = target->getCamera();
      if (!camera->isConnected()) {
        cameras.push_back(camera);
      }
    }
    m_ConnectInProgress = true;
  }

  for (auto *camera : cameras) {
    if (m_ConnectAbort) {
      break;
    }

    m_ConnectCamera = camera;
    if (!camera->connect(m_Power, timeout)) {
      failcount++;
      break;
    } else {
      m_ConnectCamera = nullptr;
    }
  }

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_ConnectInProgress = false;

    if (m_ConnectAbort || m_State == STATE_DISCONNECTING) {
      m_ConnectCamera = nullptr;
      return m_State;
    }

    if (allConnected()) {
      failcount = 0;
      m_ReconnectAttempt = 0;
      return STATE_ACTIVE;
    }
  }

  if (m_InfiniteReconnect || (failcount < 2)) {
    if (m_InfiniteReconnect) {
      uint32_t delay = SLEEP_INFINITE_MS;
      if (m_ReconnectBackoff) {
        const uint32_t shift = m_ReconnectAttempt < 5 ? m_ReconnectAttempt : 5;
        delay = SLEEP_INFINITE_MS << shift;
        if (delay > BACKOFF_MAX_MS) {
          delay = BACKOFF_MAX_MS;
        }
      }

      ESP_LOGI(LOG_TAG, "Reconnect retry %lu, waiting %lu ms.", m_ReconnectAttempt + 1, delay);
      m_ReconnectAttempt++;

      // Sleep in short slices so disconnect can interrupt the retry wait.
      uint32_t remaining = delay;
      while (remaining > 0 && !m_ConnectAbort && m_State != STATE_DISCONNECTING) {
        const uint32_t slice = remaining < BACKOFF_SLICE_MS ? remaining : BACKOFF_SLICE_MS;
        vTaskDelay(pdMS_TO_TICKS(slice));
        remaining -= slice;
      }
    }
    return m_ConnectAbort ? m_State
                          : (m_State == STATE_DISCONNECTING ? STATE_DISCONNECTING : STATE_CONNECT);
  }

  return STATE_CONNECT_FAILED;
}

void Control::task(void) {
  while (true) {
    cmd_t cmd;
    BaseType_t ret = xQueueReceive(m_Queue, &cmd, pdMS_TO_TICKS(50));

    switch (m_State) {
      case STATE_IDLE:
        if (ret == pdTRUE) {
          if (cmd == CMD_CONNECT) {
            setState(STATE_CONNECT);
            continue;
          }
        }
        break;

      case STATE_CONNECT:
        setState(STATE_CONNECTING);
        setState(connectAll());
        break;

      case STATE_CONNECTING:
      case STATE_CONNECT_FAILED:
        break;

      case STATE_ACTIVE:
        if (!allConnected()) {
          {
            const std::lock_guard<std::mutex> lock(m_Mutex);
            resetAdaptiveState();
          }
          setState(STATE_CONNECT);
          continue;
        }

        sampleAdaptivePower();

        if (ret == pdTRUE) {
          for (const auto &target : m_Targets) {
            switch (cmd) {
              case CMD_SHUTTER_PRESS:
              case CMD_SHUTTER_RELEASE:
              case CMD_FOCUS_PRESS:
              case CMD_FOCUS_RELEASE:
              case CMD_GPS_UPDATE:
                target->sendCommand(cmd);
                break;
              default:
                ESP_LOGE(LOG_TAG, "Invalid control command %d.", cmd);
                break;
            }
          }
        }
        break;

      case STATE_DISCONNECTING:
        break;
    }
  }
}

BaseType_t Control::sendCommand(cmd_t cmd) {
  return xQueueSend(m_Queue, &cmd, 0);
}

BaseType_t Control::updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync) {
  for (const auto &target : m_Targets) {
    target->updateGPS(gps, timesync);
  }

  cmd_t cmd = CMD_GPS_UPDATE;
  return xQueueSend(m_Queue, &cmd, 0);
}

bool Control::allConnected(void) {
  for (const auto &target : m_Targets) {
    if (!target->getCamera()->isConnected()) {
      return false;
    }
  }

  return true;
}

const std::vector<std::unique_ptr<Control::Target>> &Control::getTargets(void) {
  return m_Targets;
}

void Control::connectAll(bool infiniteReconnect) {
  m_InfiniteReconnect = infiniteReconnect;
  m_ReconnectBackoff = Settings::load<Settings::RECON_BACKOFF>();
  m_ReconnectAttempt = 0;
  m_ConnectAbort = false;

  this->sendCommand(CMD_CONNECT);
}

void Control::disconnect(void) {
  m_ConnectAbort = true;
  setState(STATE_DISCONNECTING);
  m_ReconnectAttempt = 0;

  // Force cancel any active connection attempts
  ble_gap_conn_cancel();

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);

    // State only, no radio calls under m_Mutex.
    resetAdaptiveState();

    // send disconnect
    for (const auto &target : m_Targets) {
      target->sendCommand(CMD_DISCONNECT);
    }
  }

  // wait for tasks to finish
  for (const auto &target : m_Targets) {
    do {
      vTaskDelay(pdMS_TO_TICKS(1));
    } while (!target.get()->m_Stopped);
  }

  while (m_ConnectInProgress) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Targets.clear();
    m_ConnectCamera = nullptr;
  }
  setState(STATE_IDLE);
}

void Control::addActive(Camera *camera) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  auto target = std::make_unique<Control::Target>(camera);

  // Create per-target task that will self-delete on disconnect
  BaseType_t ret = xTaskCreate(
      [](void *param) {
        auto *target = static_cast<Furble::Control::Target *>(param);
        target->task();
      },
      camera->getName().c_str(), 4096, target.get(), 3, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create task for '%s'.", camera->getName().c_str());
  } else {
    m_Targets.push_back(std::move(target));
  }
}

Camera *Control::getConnectingCamera(void) const {
  return m_ConnectCamera;
}

Control::state_t Control::getState(void) const {
  return m_State;
}

size_t Control::getTargetCount(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Targets.size();
}

size_t Control::getConnectedTargetCount(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  size_t connected = 0;
  for (const auto &target : m_Targets) {
    if (target->getCamera()->isConnected()) {
      connected++;
    }
  }
  return connected;
}

void Control::setState(state_t state) {
  const std::lock_guard<std::mutex> lock(m_StateMutex);

  if (state == m_State) {
    return;
  }

  m_State = state;

  // The setting is read once per connect, on the transition into STATE_ACTIVE.
  // Holding the lock keeps the device awake for the whole connection, which is
  // what furble did before the controller could modem sleep.
  bool hold = (state == STATE_ACTIVE) && !Settings::load<Settings::SLEEP_CONN>();
  if (hold == m_SleepLockHeld) {
    return;
  }

  auto &power = Power::getInstance();
  if (hold) {
    power.acquire(Power::LockType::NO_LIGHT_SLEEP, POWER_LOCK_OWNER);
  } else {
    power.release(Power::LockType::NO_LIGHT_SLEEP, POWER_LOCK_OWNER);
  }

  m_SleepLockHeld = hold;
}

void Control::sampleAdaptivePower(void) {
  struct sample_t {
    Camera *camera;
    int8_t rssi;
  };

  std::vector<Camera *> cameras;
  esp_power_level_t cap;
  bool restoreCap = false;

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    const uint32_t now = xTaskGetTickCount();
    if ((now - m_LastRssiSample) < pdMS_TO_TICKS(RSSI_SAMPLE_INTERVAL_MS)) {
      return;
    }
    m_LastRssiSample = now;
  }

  // NVS read, keep it off m_Mutex.
  const bool enabled = Settings::load<Settings::TX_ADAPTIVE>();

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    cap = m_Power;
    if (!enabled) {
      if (m_AdaptiveActive) {
        restoreCap = (m_AdaptivePower != m_Power);
        resetAdaptiveState();
      }
    } else {
      m_AdaptiveActive = true;
      // Snapshot cameras so the RSSI reads below run without the mutex.
      cameras.reserve(m_Targets.size());
      for (const auto &target : m_Targets) {
        cameras.push_back(target->getCamera());
      }
    }
  }

  if (!enabled) {
    if (restoreCap) {
      applyPower(cap);
    }
    return;
  }

  // Each getRssi() is a synchronous HCI round trip, keep m_Mutex released.
  std::vector<sample_t> samples;
  samples.reserve(cameras.size());
  for (auto *camera : cameras) {
    const int8_t rssi = camera->getRssi();
    if (rssi != 0) {
      samples.push_back({camera, rssi});
    }
  }

  bool step = false;
  int nextIndex = 0;
  esp_power_level_t next = cap;

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);

    bool haveRssi = false;
    float weakestRssi = 0.0f;
    for (const auto &target : m_Targets) {
      auto *camera = target->getCamera();
      for (const auto &sample : samples) {
        if (sample.camera != camera) {
          continue;
        }

        if (target->m_HasRssi) {
          target->m_RssiAverage += RSSI_FILTER_ALPHA * (sample.rssi - target->m_RssiAverage);
        } else {
          target->m_RssiAverage = sample.rssi;
          target->m_HasRssi = true;
        }

        if (!haveRssi || (target->m_RssiAverage < weakestRssi)) {
          weakestRssi = target->m_RssiAverage;
          haveRssi = true;
        }
        break;
      }
    }

    if (!haveRssi) {
      ESP_LOGD(LOG_TAG, "Adaptive transmit power skipped, no RSSI sample");
      return;
    }

    ESP_LOGD(LOG_TAG, "Adaptive RSSI %.1f dBm", weakestRssi);

    int capIndex = powerIndex(m_Power);
    if (capIndex < 0) {
      capIndex = 0;
    }
    int current = powerIndex(m_AdaptivePower);
    if ((current < 0) || (current > capIndex)) {
      current = capIndex;
    }

    if (weakestRssi > RSSI_STEP_DOWN_DBM) {
      m_RssiWeakSamples = 0;
      if (m_RssiStrongSamples < UINT8_MAX) {
        m_RssiStrongSamples++;
      }
      if (m_RssiStrongSamples < RSSI_STEP_DOWN_SAMPLES) {
        return;
      }

      m_RssiStrongSamples = 0;
      if (current > 0) {
        nextIndex = current - 1;
        next = POWER_LEVELS[nextIndex];
        m_AdaptivePower = next;
        step = true;
      }
    } else if (weakestRssi < RSSI_STEP_UP_DBM) {
      m_RssiStrongSamples = 0;
      if (m_RssiWeakSamples < UINT8_MAX) {
        m_RssiWeakSamples++;
      }
      if (m_RssiWeakSamples < RSSI_STEP_UP_SAMPLES) {
        return;
      }

      m_RssiWeakSamples = 0;
      if (current < capIndex) {
        nextIndex = current + 1;
        next = POWER_LEVELS[nextIndex];
        m_AdaptivePower = next;
        step = true;
      }
    } else {
      m_RssiStrongSamples = 0;
      m_RssiWeakSamples = 0;
    }
  }

  if (step) {
    // Radio call, outside m_Mutex.
    applyPower(next);
    ESP_LOGI(LOG_TAG, "Adaptive transmit power stepped to level %d", nextIndex);
  }
}

void Control::resetAdaptiveState(void) {
  m_LastRssiSample = xTaskGetTickCount();
  m_RssiStrongSamples = 0;
  m_RssiWeakSamples = 0;
  m_AdaptiveActive = false;
  m_AdaptivePower = m_Power;

  for (const auto &target : m_Targets) {
    target->m_RssiAverage = 0.0f;
    target->m_HasRssi = false;
  }
}

void Control::applyPower(esp_power_level_t power) {
  // NimBLETxPowerType::Connection maps to the shared default connection power,
  // so one call covers every link. The controller offers no clean
  // per-connection readback, log the request only.
  const int8_t dbm = Device::powerLevelToDbm(power);
  const bool set = NimBLEDevice::setPower(dbm, NimBLETxPowerType::Connection);
  ESP_LOGI(LOG_TAG, "Transmit power requested %d dBm (level %d), set %s", dbm,
           static_cast<int>(power), set ? "ok" : "failed");
}

void Control::setPower(esp_power_level_t power) {
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Power = power;
    resetAdaptiveState();
  }

  // Radio call, outside m_Mutex.
  applyPower(power);
}

};  // namespace Furble

void control_task(void *param) {
  Furble::Control *control = static_cast<Furble::Control *>(param);

  control->task();
}
