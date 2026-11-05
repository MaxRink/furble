#include "FurbleWiFi.h"

#include <string>

#include "hardware_model.h"
#include "platform_state.h"

namespace Furble {

void WiFi::init(void) {}

bool WiFi::connect(void) {
  Sim::hwAction("wifi-connect");
  return true;
}

void WiFi::disconnect(void) {
  Sim::hwAction("wifi-disconnect");
}

bool WiFi::setEnabled(bool enabled) {
  Sim::hwWifiSetConnected(enabled);
  return true;
}

void WiFi::forget(void) {
  Sim::hwWifiSetConnected(false);
}

void WiFi::clearRememberedAccessPoint(void) {}

bool WiFi::setNtpEnabled(bool enabled) {
  (void)enabled;
  return true;
}

bool WiFi::reloadNtp(void) {
  return false;
}

bool WiFi::syncNtp(void) {
  Sim::hwAction("wifi-ntp-sync");
  return true;
}

WiFi::status_t WiFi::getStatus(void) {
  status_t status = {};
  const std::string state = Sim::hwWifiState();
  if (state == "connected") {
    status.state = STATE_CONNECTED;
    status.connected = true;
    status.enabled = true;
    status.ip = Sim::hwWifiIp();
    status.ssid = Sim::hwWifiSsid();
    status.rssi = static_cast<int8_t>(Sim::hwWifiRssi());
    status.ntp_enabled = true;
    status.ntp_running = true;
    status.ntp_synced = Sim::hwNtpSynced();
  } else if (state == "failed") {
    status.state = STATE_IDLE;
    status.attempts = Sim::hwWifiAttempts();
    status.retry_ms = Sim::hwWifiRetryMs();
  } else {
    status.state = STATE_DISABLED;
  }
  return status;
}

bool WiFi::getNtpTimesync(Camera::timesync_t &) {
  return Sim::hwNtpSynced();
}

}  // namespace Furble
