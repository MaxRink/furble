#include <algorithm>

#include "FurbleControl.h"

#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSettings.h"
#include "clock.h"

namespace Furble {
namespace {

constexpr uint32_t CONNECT_DURATION_MS = 750;
uint32_t connectStart = 0;

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

void Control::setConnSaver(bool enabled) {
  // The simulated camera has no live BLE connection to retune.
  (void)enabled;
}

void Control::connectAll(bool infiniteReconnect) {
  m_InfiniteReconnect = infiniteReconnect;
  m_ConnectCamera = nullptr;
  for (const auto &target : m_Targets) {
    if (target->getCamera() != nullptr && target->getCamera()->isActive()) {
      m_ConnectCamera = target->getCamera();
      break;
    }
  }

  if (m_ConnectCamera == nullptr) {
    setState(STATE_CONNECT_FAILED);
    return;
  }

  connectStart = Sim::clockMillis();
  m_ConnectCamera->setConnectProgress(0);
  setState(STATE_CONNECTING);
}

bool Control::disconnect(uint32_t timeout_ms, bool forRestart) {
  (void)timeout_ms;
  (void)forRestart;
  m_ConnectCamera = nullptr;
  m_Targets.clear();
  m_State = STATE_IDLE;
  return true;
}

bool Control::disconnectComplete(void) {
  return true;
}

void Control::addActive(std::shared_ptr<Camera> camera) {
  if (camera != nullptr) {
    m_Targets.push_back(std::make_unique<Control::Target>(camera));
  }
}

std::shared_ptr<Camera> Control::getConnectingCamera(void) const {
  (void)getState();
  return m_ConnectCamera;
}

Control::state_t Control::getState(void) const {
  auto *control = const_cast<Control *>(this);
  if (control->m_State == STATE_CONNECTING && control->m_ConnectCamera != nullptr) {
    const uint32_t elapsed = Platform::getInstance().tick() - connectStart;
    const uint8_t progress =
        static_cast<uint8_t>(std::min<uint32_t>(100, elapsed * 100 / CONNECT_DURATION_MS));
    control->m_ConnectCamera->setConnectProgress(progress);
    if (elapsed >= CONNECT_DURATION_MS) {
      const bool connected =
          control->m_ConnectCamera->connect(control->m_Power, CONNECT_DURATION_MS);
      if (connected) {
        // Bring up every other selected target too, so multi-connect scenarios
        // finish with all cameras live, mirroring the on-device sequential
        // connect. Cameras already connected (e.g. the survivors of a one-link
        // drop) are left untouched.
        for (const auto &target : control->m_Targets) {
          auto camera = target->getCamera();
          if (camera != nullptr && camera != control->m_ConnectCamera && camera->isActive()
              && !camera->isConnected()) {
            camera->connect(control->m_Power, CONNECT_DURATION_MS);
          }
        }
      }
      control->m_ConnectCamera = nullptr;
      control->setState(connected ? STATE_ACTIVE : STATE_CONNECT_FAILED);
    }
  }
  return m_State;
}

void Control::setState(state_t state) {
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
  m_Power = power;
}

#if defined(FURBLE_SIM)
void Control::simDropActiveLink(int index) {
  if (getState() != STATE_ACTIVE) {
    return;
  }

  // Drop the selected link(s), mirroring a supervision timeout. index < 0 drops
  // every active camera; index >= 0 drops only that target so a multi-connect
  // session can lose a single camera.
  std::shared_ptr<Camera> dropped;
  int i = 0;
  for (const auto &target : m_Targets) {
    auto camera = target->getCamera();
    if (camera != nullptr && (index < 0 || index == i)) {
      camera->disconnect();
      if (dropped == nullptr) {
        dropped = camera;
      }
    }
    i++;
  }

  if (dropped == nullptr) {
    return;
  }

  // A still-connected camera keeps the session live: dropping one link in a
  // multi-connect session must not tear down the others.
  bool othersConnected = false;
  for (const auto &target : m_Targets) {
    auto camera = target->getCamera();
    if (camera != nullptr && camera->isConnected()) {
      othersConnected = true;
      break;
    }
  }

  if (m_InfiniteReconnect) {
    // Reconnect mode re-enters connecting without passing through idle, exactly
    // like the on-device control task after a dropped supervision timeout. Any
    // camera still connected keeps its link through the reconnect window.
    connectStart = Sim::clockMillis();
    dropped->setActive(true);
    dropped->setConnectProgress(0);
    m_ConnectCamera = dropped;
    setState(STATE_CONNECTING);
    return;
  }

  if (othersConnected) {
    // Reconnect is off but other links remain: stay active to keep serving them.
    m_ConnectCamera = nullptr;
    return;
  }

  m_ConnectCamera = nullptr;
  setState(STATE_IDLE);
}
#endif

void Control::task(void) {}

}  // namespace Furble

extern "C" void control_task(void *param) {
  static_cast<Furble::Control *>(param)->task();
}
