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

  explicit Camera(std::string name = "FauxNY", uint64_t address = 0, uint8_t addressType = 0)
      : m_Address {address == 0 ? allocateAddress() : address},
        m_AddressType {addressType},
        m_Name {std::move(name)} {}
  virtual ~Camera() = default;

  bool connect(esp_power_level_t power, uint32_t timeout);
  void disconnect(void);
  bool isConnected(void) const;
  bool isActive(void) const;
  void setActive(bool active);
  const Type &getType(void) const;
  const std::string &getName(void) const;
  uint64_t getAddress(void) const { return m_Address; }
  uint8_t getAddressType(void) const { return m_AddressType; }
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

 private:
  static uint64_t allocateAddress(void) {
    static std::atomic<uint64_t> nextAddress {0x100000000000ULL};
    return nextAddress.fetch_add(1, std::memory_order_relaxed);
  }

  uint64_t m_Address;
  uint8_t m_AddressType;
  Type m_Type = Type::FAUXNY;
  std::string m_Name;
  std::atomic<uint8_t> m_Progress {0};
  bool m_Active = false;
  bool m_Connected = false;
};

}  // namespace Furble

#endif
