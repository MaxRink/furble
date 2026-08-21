#include <algorithm>
#include <atomic>

#include "Camera.h"
#include "driver.h"
#include "power_profiler.h"

namespace Furble {
namespace {

std::atomic<uint32_t> shutterPresses {0};
std::atomic<uint32_t> shutterReleases {0};
std::atomic<uint32_t> focusPresses {0};
std::atomic<uint32_t> focusReleases {0};

}  // namespace

bool Camera::connect(esp_power_level_t, uint32_t) {
  // A scenario can request a camera that never establishes a link. The connect
  // reports failure and leaves the camera disconnected, mirroring the
  // stale-connected hardware bug at the UI layer.
  if (Sim::connectShouldFail()) {
    m_Connected = false;
    m_Progress = 0;
    Sim::profilerSetRadioConnected(false);
    return false;
  }
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
  shutterPresses.fetch_add(1);
  Sim::profilerRadioEvent("shutter_press");
}

void Camera::shutterRelease(void) {
  shutterReleases.fetch_add(1);
  Sim::profilerRadioEvent("shutter_release");
}

void Camera::focusPress(void) {
  focusPresses.fetch_add(1);
  Sim::profilerRadioEvent("focus_press");
}

void Camera::focusRelease(void) {
  focusReleases.fetch_add(1);
  Sim::profilerRadioEvent("focus_release");
}

void Camera::updateGeoData(const gps_t &, const timesync_t &) {
  Sim::profilerRadioEvent("gps_update");
}

namespace Sim {

uint32_t cameraShutterPresses(void) {
  return shutterPresses.load();
}

uint32_t cameraShutterReleases(void) {
  return shutterReleases.load();
}

uint32_t cameraFocusPresses(void) {
  return focusPresses.load();
}

uint32_t cameraFocusReleases(void) {
  return focusReleases.load();
}

}  // namespace Sim

}  // namespace Furble
