#include <algorithm>
#include <utility>

#include "FurbleControl.h"

#include "FurblePower.h"
#include "FurbleSettings.h"
#include "clock.h"
#include "connect_model.h"

namespace Furble {
namespace {

// This is an explicitly synthetic simulator model. It is intentionally not a
// claim about radio or GATT timing. Keep the model task-owned so observation
// cannot advance a connection attempt.
}  // namespace

Control::Target::Target(std::shared_ptr<Camera> camera) : m_Camera {camera} {}

Control::Target::~Target() {
  if (m_Camera != nullptr) {
    m_Camera->disconnect();
  }
  m_Camera = nullptr;
}

std::shared_ptr<Camera> Control::Target::getCamera(void) const {
  return m_Camera;
}

Control::cmd_t Control::Target::getCommand(void) {
  return CMD_ERROR;
}

void Control::Target::sendCommand(cmd_t cmd) {
  if (m_Camera == nullptr) {
    return;
  }
  switch (cmd) {
    case CMD_SHUTTER_PRESS:
      m_Camera->shutterPress();
      break;
    case CMD_SHUTTER_RELEASE:
      m_Camera->shutterRelease();
      break;
    case CMD_FOCUS_PRESS:
      m_Camera->focusPress();
      break;
    case CMD_FOCUS_RELEASE:
      m_Camera->focusRelease();
      break;
    case CMD_GPS_UPDATE:
      m_Camera->updateGeoData(m_GPS, m_Timesync);
      break;
    default:
      break;
  }
}

void Control::Target::updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync) {
  m_GPS = gps;
  m_Timesync = timesync;
}

void Control::Target::task(void) {}

Control &Control::getInstance(void) {
  static Control instance;
  if (instance.m_Queue == nullptr) {
    instance.m_Queue = xQueueCreate(instance.m_QueueLength, sizeof(sim_cmd_t));
  }
  return instance;
}

BaseType_t Control::sendCommand(cmd_t cmd) {
  // Mirror the device: camera-action commands route per-target, gated on the
  // live link, so a press during a mid-session reconnect fires on the cameras
  // still connected and is dropped for the one that is down. Nothing is buffered,
  // so a dropped target never replays the press when it reconnects.
  switch (cmd) {
    case CMD_SHUTTER_PRESS:
    case CMD_SHUTTER_RELEASE:
    case CMD_FOCUS_PRESS:
    case CMD_FOCUS_RELEASE:
    {
      BaseType_t delivered = pdFALSE;
      for (const auto &target : m_Targets) {
        auto camera = target->getCamera();
        if (camera != nullptr && camera->isConnected()) {
          target->sendCommand(cmd);
          delivered = pdTRUE;
        }
      }
      return delivered;
    }
    default:
      break;
  }

  if (getState() != STATE_ACTIVE) {
    return pdFALSE;
  }
  for (const auto &target : m_Targets) {
    target->sendCommand(cmd);
  }
  return pdTRUE;
}

BaseType_t Control::updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync) {
  for (const auto &target : m_Targets) {
    target->updateGPS(gps, timesync);
  }
  return pdTRUE;
}

bool Control::allConnected(void) {
  for (const auto &target : m_Targets) {
    if (target->getCamera() == nullptr || !target->getCamera()->isConnected()) {
      return false;
    }
  }
  return true;
}

std::vector<Control::Target *> Control::getTargets(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  std::vector<Target *> targets;
  targets.reserve(m_Targets.size());
  for (const auto &target : m_Targets) {
    targets.push_back(target.get());
  }
  return targets;
}

size_t Control::getTargetCount(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Targets.size();
}

size_t Control::getConnectedTargetCount(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  size_t connected = 0;
  for (const auto &target : m_Targets) {
    if (target->getCamera() != nullptr && target->getCamera()->isConnected()) {
      connected++;
    }
  }
  return connected;
}

std::string Control::getDisconnectedName(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  for (const auto &target : m_Targets) {
    const auto camera = target->getCamera();
    if (camera != nullptr && !camera->isConnected()) {
      return camera->getName();
    }
  }
  return std::string();
}

void Control::setConnSaver(bool enabled) {
  // The simulated camera has no live BLE connection to retune.
  (void)enabled;
}

void Control::connectAll(bool infiniteReconnect) {
  uint64_t generation;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_InfiniteReconnect = infiniteReconnect;
    generation = ++m_SimConnectGeneration;
    m_SimConnectRequestPending = true;
  }
  // Match production Control: requesting a connection only publishes a
  // command. The control task owns the generation, start time, camera choice,
  // progress, and state transition.
  sim_cmd_t cmd {CMD_CONNECT, generation};
  if (xQueueSend(m_Queue, &cmd, 0) != pdTRUE) {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (generation == m_SimConnectGeneration && m_SimConnectRequestPending) {
      // The generation was advanced before enqueue so an older attempt cannot
      // publish. If this request cannot be queued either, fail closed instead
      // of leaving the old camera fenced forever behind a stuck pending flag.
      m_SimConnectRequestPending = false;
      m_ConnectCamera = nullptr;
      setStateLocked(STATE_CONNECT_FAILED);
    }
  }
}

bool Control::disconnect(uint32_t timeout_ms, bool forRestart) {
  (void)timeout_ms;
  (void)forRestart;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    ++m_SimConnectGeneration;
    m_ConnectCamera = nullptr;
    m_Targets.clear();
    m_SimConnectPublishHook = nullptr;
    m_SimConnectRequestPending = false;
    setStateLocked(STATE_IDLE);
  }
  return true;
}

bool Control::disconnectComplete(void) {
  return true;
}

void Control::addActive(std::shared_ptr<Camera> camera) {
  if (camera == nullptr) {
    return;
  }
  const std::lock_guard<std::mutex> lock(m_Mutex);
  for (const auto &target : m_Targets) {
    if (target->getCamera() == camera) {
      return;
    }
  }
  m_Targets.push_back(std::make_unique<Control::Target>(camera));
}

std::shared_ptr<Camera> Control::getConnectingCamera(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_ConnectCamera;
}

Control::state_t Control::getState(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_State;
}

void Control::setState(state_t state) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  setStateLocked(state);
}

void Control::setStateLocked(state_t state) {
  // Keep the simulator's lock order identical to the shared Control contract:
  // generation/target state under m_Mutex, then the state publication lock.
  const std::lock_guard<std::mutex> stateLock(m_StateMutex);
  if (state == m_State) {
    return;
  }

  m_State = state;
  const bool hold = (state == STATE_ACTIVE) && !Settings::load<Settings::SLEEP_CONN>();
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
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_Power = power;
}

#if defined(FURBLE_SIM)
size_t Control::simConnectCompletions(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_SimConnectCompletions;
}

bool Control::simConnectRequestPending(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_SimConnectRequestPending;
}

Control::sim_state_snapshot_t Control::simStateSnapshot(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return {m_State, m_SimConnectRequestPending};
}

void Control::simShutdown(void) {
  QueueHandle_t queue = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    queue = m_Queue;
    m_Queue = nullptr;
  }
  // Called only after furble_sim_stop_all_tasks() has joined the control task;
  // no callback or worker can use the detached queue now.
  vQueueDelete(queue);
}

bool Control::simQueueAlive(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Queue != nullptr;
}

void Control::simSetConnectPublishHook(std::function<void(void)> hook) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_SimConnectPublishHook = std::move(hook);
}
#endif

#if defined(FURBLE_SIM)
void Control::simDropActiveLink(int index) {
  if (getState() != STATE_ACTIVE) {
    return;
  }

  // Drop the selected link(s), mirroring a supervision timeout. index < 0 drops
  // every active camera; index >= 0 drops only that target so a multi-connect
  // session can lose a single camera.
  std::shared_ptr<Camera> dropped;
  std::vector<std::shared_ptr<Camera>> cameras;
  bool infiniteReconnect = false;
  int i = 0;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    infiniteReconnect = m_InfiniteReconnect;
    for (const auto &target : m_Targets) {
      auto camera = target->getCamera();
      if (camera != nullptr) {
        cameras.push_back(camera);
        if (index < 0 || index == i) {
          if (dropped == nullptr) {
            dropped = camera;
          }
        }
      }
      i++;
    }
  }
  for (const auto &camera : cameras) {
    if (index < 0 || camera == dropped) {
      camera->disconnect();
    }
  }

  if (dropped == nullptr) {
    return;
  }

  // A still-connected camera keeps the session live: dropping one link in a
  // multi-connect session must not tear down the others.
  bool othersConnected = false;
  for (const auto &camera : cameras) {
    if (camera != nullptr && camera->isConnected()) {
      othersConnected = true;
      break;
    }
  }

  if (infiniteReconnect) {
    // Reconnect mode re-enters connecting without passing through idle, exactly
    // like the on-device control task after a dropped supervision timeout. Any
    // camera still connected keeps its link through the reconnect window.
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      ++m_SimConnectGeneration;
      m_SimConnectRequestPending = false;
      m_SimConnectStart = Sim::clockMillis();
      dropped->setActive(true);
      dropped->setConnectProgress(0);
      m_ConnectCamera = dropped;
      setStateLocked(STATE_CONNECTING);
    }
    return;
  }

  if (othersConnected) {
    // Reconnect is off but other links remain: stay active to keep serving them.
    return;
  }

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    ++m_SimConnectGeneration;
    m_ConnectCamera = nullptr;
    m_SimConnectRequestPending = false;
    setStateLocked(STATE_IDLE);
  }
}
#endif

void Control::task(void) {
  while (!Sim::schedulerStopping()) {
    sim_cmd_t command {CMD_ERROR, 0};
    const bool commandReceived = xQueueReceive(m_Queue, &command, 0) == pdTRUE;
    if (commandReceived && command.command == CMD_CONNECT) {
      bool hasCamera = false;
      {
        const std::lock_guard<std::mutex> lock(m_Mutex);
        if (command.generation == m_SimConnectGeneration) {
          m_SimConnectRequestPending = false;
          m_ConnectCamera = nullptr;
          for (const auto &target : m_Targets) {
            if (target->getCamera() != nullptr && target->getCamera()->isActive()) {
              m_ConnectCamera = target->getCamera();
              m_SimConnectStart = Sim::clockMillis();
              m_ConnectCamera->setConnectProgress(0);
              hasCamera = true;
              break;
            }
          }
          setStateLocked(hasCamera ? STATE_CONNECTING : STATE_CONNECT_FAILED);
        }
      }
    }

    std::shared_ptr<Camera> camera;
    uint64_t generation = 0;
    uint32_t start = 0;
    esp_power_level_t power = ESP_PWR_LVL_P3;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      if (!m_SimConnectRequestPending && m_ConnectCamera != nullptr
          && m_State == STATE_CONNECTING) {
        camera = m_ConnectCamera;
        generation = m_SimConnectGeneration;
        start = m_SimConnectStart;
        power = m_Power;
      }
    }

    if (camera != nullptr) {
      const uint32_t elapsed = Sim::clockElapsed(Sim::clockMillis(), start);
      const uint8_t progress =
          static_cast<uint8_t>(std::min<uint32_t>(100, elapsed * 100 / Sim::CONNECT_DURATION_MS));
      camera->setConnectProgress(progress);

      if (Sim::connectDeadlineReached(Sim::clockMillis(), start)) {
        const bool connected = camera->connect(power, Sim::CONNECT_DURATION_MS);
        std::function<void(void)> publishHook;
        {
          const std::lock_guard<std::mutex> lock(m_Mutex);
          if (generation == m_SimConnectGeneration && m_State == STATE_CONNECTING
              && m_ConnectCamera == camera) {
            publishHook = std::move(m_SimConnectPublishHook);
          }
        }
        // This test-only seam runs immediately before the publication lock. It
        // permits cancellation at the exact generation/state check, proving
        // that check—not a reader or timing convention—owns publication.
        if (publishHook) {
          publishHook();
        }
        bool publish = false;
        {
          const std::lock_guard<std::mutex> lock(m_Mutex);
          // A disconnect or a newer connect invalidates this completion. Do
          // not publish a stale state or clear a newer connection attempt.
          publish = generation == m_SimConnectGeneration && m_State == STATE_CONNECTING
                    && m_ConnectCamera == camera;
          if (publish && connected) {
            // Bring up every other selected target too, so multi-connect
            // scenarios retain the existing synthetic behavior. This remains
            // task-owned and is fenced by the same generation.
            for (const auto &target : m_Targets) {
              auto other = target->getCamera();
              if (other != nullptr && other != camera && other->isActive()
                  && !other->isConnected()) {
                other->connect(power, Sim::CONNECT_DURATION_MS);
              }
            }
          } else if (!publish && connected) {
            // The owner cancelled while connect() was completing. Do not leave
            // a stale synthetic link alive after the target was removed. If a
            // newer generation still owns this same camera, its progression
            // must not be torn down by the older completion.
            const bool stillOwned = std::any_of(
                m_Targets.begin(), m_Targets.end(), [&camera](const auto &target) {
                  return target->getCamera() == camera;
                });
            if (!stillOwned) {
              camera->disconnect();
            }
          }
          if (publish) {
            m_ConnectCamera = nullptr;
            ++m_SimConnectCompletions;
            setStateLocked(connected ? STATE_ACTIVE : STATE_CONNECT_FAILED);
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace Furble

extern "C" void control_task(void *param) {
  static_cast<Furble::Control *>(param)->task();
}
