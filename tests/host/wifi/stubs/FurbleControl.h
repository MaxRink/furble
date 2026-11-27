#pragma once
namespace Furble {
class Control {
 public:
  // Mirrors include/FurbleControl.h: real_wifi_test feeds every value.
  enum state_t {
    STATE_IDLE,
    STATE_CONNECT,
    STATE_CONNECTING,
    STATE_CONNECT_FAILED,
    STATE_ACTIVE,
    STATE_DISCONNECTING,
  };
  static Control &getInstance();
  state_t getState() const;
  // The adaptive transmit floor is a Control-side policy and is covered by
  // control-adaptive-floor. This target only needs src/FurbleWiFi.cpp to
  // compile, so the install is a no-op here.
  static void setWifiConnectedProbe(bool (*probe)(void)) { (void)probe; }
};
}  // namespace Furble
