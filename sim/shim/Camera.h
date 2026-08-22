#ifndef FURBLE_SIM_CAMERA_H
#define FURBLE_SIM_CAMERA_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <esp_bt.h>
#include <freertos/FreeRTOS.h>

#include <FurbleTypes.h>

namespace Furble {

class Camera {
 public:
  enum class Type : uint32_t {
    FAUXNY = 5,
  };

  enum class PairType : uint8_t {
    NEW = 1,
    SAVED = 2,
  };

  enum class ConnProfile : uint8_t {
    FAST,
    IDLE,
    PEER,
  };

  enum class PairingType : uint8_t {
    NONE,
    PASSKEY_DISPLAY,
    NUMERIC_COMPARISON,
  };

  typedef void (*pairing_request_callback_t)(Camera *camera);

  typedef struct _gps_t {
    double latitude;
    double longitude;
    double altitude;
    unsigned int satellites;
  } gps_t;

  typedef struct _timesync_t {
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    unsigned int centisecond;
  } timesync_t;

  explicit Camera(std::string name = "FauxNY") : m_Name {std::move(name)} {}
  virtual ~Camera() = default;

  bool connect(esp_power_level_t power, uint32_t timeout);
  void disconnect(void);
  bool isConnected(void) const;
  bool isActive(void) const;
  void setActive(bool active);
  const Type &getType(void) const;
  const std::string &getName(void) const;
  uint8_t getConnectProgress(void) const;

  void setConnectProgress(uint8_t progress);
  void shutterPress(void);
  void shutterRelease(void);
  void focusPress(void);
  void focusRelease(void);
  void updateGeoData(const gps_t &gps, const timesync_t &timesync);

  // The connection statistics are a fixed snapshot so scripted captures of
  // the multiconnect page stay deterministic.
  bool getConnParams(uint16_t &interval, uint16_t &latency, uint16_t &timeout, int &rssi) const {
    if (!m_Connected) {
      return false;
    }
    interval = 12;
    latency = 0;
    timeout = 400;
    rssi = -60;
    return true;
  }

  ConnProfile getConnProfile(void) const { return ConnProfile::FAST; }

  static const char *connProfileName(ConnProfile profile) {
    switch (profile) {
      case ConnProfile::FAST:
        return "fast";
      case ConnProfile::IDLE:
        return "idle";
      case ConnProfile::PEER:
        return "peer";
    }
    return "peer";
  }

  // The simulator never drives a real BLE pairing handshake, so the pairing
  // prompt API is stubbed to a permanently idle state.
  static void setPairingRequestCallback(pairing_request_callback_t callback) { (void)callback; }
  bool hasPendingPairing(void) const { return false; }
  PairingType getPairingType(void) const { return PairingType::NONE; }
  uint32_t getPairingCode(void) const { return 0; }
  bool pairingTimedOut(void) const { return false; }
  bool answerPairing(bool accept) {
    (void)accept;
    return false;
  }
  void cancelPairing(void) {}

 private:
  Type m_Type = Type::FAUXNY;
  std::string m_Name;
  std::atomic<uint8_t> m_Progress {0};
  bool m_Active = false;
  bool m_Connected = false;
};

}  // namespace Furble

#endif
