#include "FurbleWiFi.h"

namespace Furble {

void WiFi::init(void) {}

bool WiFi::connect(void) {
  return false;
}

void WiFi::disconnect(void) {}

bool WiFi::setEnabled(bool enabled) {
  return !enabled;
}

void WiFi::forget(void) {}

void WiFi::clearRememberedAccessPoint(void) {}

bool WiFi::setNtpEnabled(bool enabled) {
  return !enabled;
}

bool WiFi::reloadNtp(void) {
  return false;
}

bool WiFi::syncNtp(void) {
  return false;
}

WiFi::status_t WiFi::getStatus(void) {
  status_t status = {};
  status.state = STATE_DISABLED;
  return status;
}

bool WiFi::getNtpTimesync(Camera::timesync_t &) {
  return false;
}

}  // namespace Furble
