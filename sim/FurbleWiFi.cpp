#include "FurbleWiFi.h"

#include <atomic>
#include <string>

namespace {

std::atomic<bool> simWifiConnected {false};
std::string simWifiIp = "192.0.2.10";

}  // namespace

namespace Furble::Sim {

void setWifiConnected(bool connected) {
  simWifiConnected.store(connected, std::memory_order_release);
}

bool wifiConnected(void) {
  return simWifiConnected.load(std::memory_order_acquire);
}

}  // namespace Furble::Sim

namespace Furble {

void WiFi::init(void) {}

bool WiFi::connect(void) {
  return false;
}

void WiFi::disconnect(void) {}

bool WiFi::setEnabled(bool enabled) {
  Furble::Sim::setWifiConnected(enabled);
  return true;
}

void WiFi::forget(void) {}

void WiFi::clearRememberedAccessPoint(void) {}

bool WiFi::setNtpEnabled(bool enabled) {
  (void)enabled;
  return true;
}

bool WiFi::reloadNtp(void) {
  return false;
}

bool WiFi::syncNtp(void) {
  return false;
}

WiFi::status_t WiFi::getStatus(void) {
  status_t status = {};
  if (Furble::Sim::wifiConnected()) {
    status.state = STATE_CONNECTED;
    status.connected = true;
    status.ip = simWifiIp;
    status.rssi = -55;
  } else {
    status.state = STATE_DISABLED;
  }
  return status;
}

bool WiFi::getNtpTimesync(Camera::timesync_t &) {
  return false;
}

}  // namespace Furble
