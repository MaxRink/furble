#include <algorithm>

#include "FurbleControl.h"

#include "FurblePlatform.h"
#include "clock.h"

namespace Furble {
namespace {

constexpr uint32_t CONNECT_DURATION_MS = 750;
uint32_t connectStart = 0;

}  // namespace

Control::Target::Target(Camera *camera) : m_Camera {camera} {}

Control::Target::~Target() {
  if (m_Camera != nullptr) {
    m_Camera->disconnect();
  }
  m_Camera = nullptr;
}

Camera *Control::Target::getCamera(void) const {
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

const std::vector<std::unique_ptr<Control::Target>> &Control::getTargets(void) {
  return m_Targets;
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
    m_State = STATE_CONNECT_FAILED;
    return;
  }

  connectStart = Sim::clockMillis();
  m_ConnectCamera->setConnectProgress(0);
  m_State = STATE_CONNECTING;
}

void Control::disconnect(void) {
  m_ConnectCamera = nullptr;
  m_Targets.clear();
  m_State = STATE_IDLE;
}

void Control::addActive(Camera *camera) {
  if (camera != nullptr) {
    m_Targets.push_back(std::make_unique<Control::Target>(camera));
  }
}

Camera *Control::getConnectingCamera(void) const {
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
      control->m_ConnectCamera->connect(control->m_Power, CONNECT_DURATION_MS);
      control->m_ConnectCamera = nullptr;
      control->m_State = STATE_ACTIVE;
    }
  }
  return m_State;
}

void Control::setPower(esp_power_level_t power) {
  m_Power = power;
}

void Control::task(void) {}

}  // namespace Furble

extern "C" void control_task(void *param) {
  static_cast<Furble::Control *>(param)->task();
}
