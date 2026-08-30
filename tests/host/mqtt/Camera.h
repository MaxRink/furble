#ifndef FURBLE_HOST_MQTT_CAMERA_H
#define FURBLE_HOST_MQTT_CAMERA_H

#include <cstdint>
#include <string>
#include <utility>

namespace Furble {

class Camera {
 public:
  enum class Type : uint32_t {
    FUJIFILM_BASIC = 1,
    CANON_EOS_SMART,
    CANON_EOS_REMOTE,
    MOBILE_DEVICE,
    FAUXNY,
    NIKON,
    SONY,
    FUJIFILM_SECURE,
    RICOH,
    PANASONIC_LUMIX,
    DJI_OSMO,
  };

  struct gps_t {
    double latitude = 0;
    double longitude = 0;
    double altitude = 0;
    unsigned int satellites = 0;
  };

  struct timesync_t {
    unsigned int year = 0;
    unsigned int month = 0;
    unsigned int day = 0;
    unsigned int hour = 0;
    unsigned int minute = 0;
    unsigned int second = 0;
    unsigned int centisecond = 0;
  };

  Camera(std::string id, std::string name, Type type)
      : m_ID(std::move(id)), m_Name(std::move(name)), m_Type(type) {}

  const std::string &getID(void) const { return m_ID; }
  const std::string &getName(void) const { return m_Name; }
  Type getType(void) const { return m_Type; }
  uint8_t getConnectProgress(void) const { return m_Progress; }
  int getRSSI(void) const { return m_RSSI; }
  bool isActive(void) const { return m_Active; }
  void setActive(bool active) { m_Active = active; }

  void setConnectProgress(uint8_t progress) { m_Progress = progress; }
  void setRSSI(int rssi) { m_RSSI = rssi; }

 private:
  std::string m_ID;
  std::string m_Name;
  Type m_Type;
  uint8_t m_Progress = 0;
  int m_RSSI = -127;
  bool m_Active = false;
};

}  // namespace Furble

#endif
