#include "FurbleControl.h"
#include "FurblePower.h"
#include "FurbleSettings.h"

namespace Furble {
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
          setState(STATE_CONNECT);
          continue;
        }

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

void Control::setPower(esp_power_level_t power) {
  m_Power = power;
}

};  // namespace Furble

void control_task(void *param) {
  Furble::Control *control = static_cast<Furble::Control *>(param);

  control->task();
}
