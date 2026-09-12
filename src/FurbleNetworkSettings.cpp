#include "FurbleSettings.h"

#include <cctype>

namespace Furble {
bool Settings::validNetworkString(type_t type, const std::string &value) {
  if (value.find(static_cast<char>(0)) != std::string::npos) {
    return false;
  }
  switch (type) {
    case WIFI_SSID:
      return value.size() <= WIFI_SSID_MAX_LENGTH;
    case WIFI_PSK:
      return value.size() <= WIFI_PSK_MAX_LENGTH;
    case NTP_SERVER:
      return !value.empty() && (value.size() <= NTP_SERVER_MAX_LENGTH);
    default:
      return false;
  }
}
#if defined(FURBLE_MQTT) && FURBLE_MQTT
bool Settings::validMQTTString(type_t type, const std::string &value) {
  constexpr size_t MAX_LENGTH = 255;
  constexpr size_t MAX_BASE_LENGTH = 64;
  if ((value.size() > MAX_LENGTH) || (value.find(static_cast<char>(0)) != std::string::npos)) {
    return false;
  }
  for (const unsigned char character : value) {
    if (std::iscntrl(character)
        || (((type == MQTT_URI) || (type == MQTT_BASE)) && std::isspace(character))) {
      return false;
    }
  }
  if (type == MQTT_URI) {
    const std::string tls = "mqtts://";
    const std::string plain = "mqtt://";
    return ((value.compare(0, tls.size(), tls) == 0) && (value.size() > tls.size()))
           || ((value.compare(0, plain.size(), plain) == 0) && (value.size() > plain.size()));
  }
  if (type == MQTT_BASE) {
    size_t first = 0;
    size_t last = value.size();
    while ((first < last) && (value[first] == '/')) {
      ++first;
    }
    while ((last > first) && (value[last - 1] == '/')) {
      --last;
    }
    const std::string base = value.substr(first, last - first);
    return (base.size() <= MAX_BASE_LENGTH) && (base.find_first_of("+#") == std::string::npos)
           && (base.find("//") == std::string::npos);
  }
  return (type == MQTT_USER) || (type == MQTT_PASS);
}
#endif
}  // namespace Furble
