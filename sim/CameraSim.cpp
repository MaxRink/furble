#include <algorithm>

#include "Camera.h"

namespace Furble {

bool Camera::connect(esp_power_level_t, uint32_t) {
  m_Progress = 100;
  m_Connected = true;
  return true;
}

void Camera::disconnect(void) {
  m_Connected = false;
  m_Progress = 0;
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

void Camera::shutterPress(void) {}

void Camera::shutterRelease(void) {}

void Camera::focusPress(void) {}

void Camera::focusRelease(void) {}

void Camera::updateGeoData(const gps_t &, const timesync_t &) {}

}  // namespace Furble
