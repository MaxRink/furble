#ifndef FURBLE_HOST_MQTT_DEVICE_H
#define FURBLE_HOST_MQTT_DEVICE_H

#include <string>

namespace Furble {

class Device {
 public:
  static const std::string getStringID(void) { return m_ID; }
  static void setStringID(const std::string &id) { m_ID = id; }

 private:
  inline static std::string m_ID = "host-device";
};

}  // namespace Furble

#endif
