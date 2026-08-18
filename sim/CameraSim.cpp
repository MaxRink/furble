#include <algorithm>

#include "Camera.h"
#include "power_profiler.h"

namespace Furble {

bool Camera::connect(esp_power_level_t, uint32_t) {
  m_Progress = 100;
  m_Connected = true;
  Sim::profilerSetRadioConnected(true);
  return true;
}

void Camera::disconnect(void) {
  m_Connected = false;
  m_Progress = 0;
  Sim::profilerSetRadioConnected(false);
}

bool Camera::isConnected(void) const {
  return m_Connected;
}

bool Camera::isActive(void) const {
  return m_Active;
}

void Camera::setActive(bool active) {
  m_Active = active;
}

const Camera::Type &Camera::getType(void) const {
  return m_Type;
}

const std::string &Camera::getName(void) const {
  return m_Name;
}

uint8_t Camera::getConnectProgress(void) const {
  return m_Progress.load();
}

void Camera::setConnectProgress(uint8_t progress) {
  m_Progress = std::min<uint8_t>(progress, 100);
}

void Camera::shutterPress(void) {
  Sim::profilerRadioEvent("shutter_press");
}

void Camera::shutterRelease(void) {
  Sim::profilerRadioEvent("shutter_release");
}

void Camera::focusPress(void) {
  Sim::profilerRadioEvent("focus_press");
}

void Camera::focusRelease(void) {
  Sim::profilerRadioEvent("focus_release");
}

void Camera::updateGeoData(const gps_t &, const timesync_t &) {
  Sim::profilerRadioEvent("gps_update");
}

}  // namespace Furble
