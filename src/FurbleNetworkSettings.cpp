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
      // WPA2-PSK and WPA3-SAE both take 8 to 63 characters. Empty is an open
      // network, which the console and the provisioning step both allow on
      // purpose. Anything between 1 and 7 characters is stored nowhere: it can
      // never associate, so it is refused here rather than written.
      return value.empty()
             || ((value.size() >= WIFI_PSK_MIN_LENGTH) && (value.size() <= WIFI_PSK_MAX_LENGTH));
    case NTP_SERVER:
      return !value.empty() && (value.size() <= NTP_SERVER_MAX_LENGTH);
    default:
      return false;
  }
}
bool Settings::validTheme(const std::string &value) {
  return (value == "Dark") || (value == "Default") || (value == "Mono Furble");
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
    const std::string secure = "mqtts://";
    const std::string plain = "mqtt://";
    // The firmware enables the esp-mqtt SSL transport for every MQTT build but
    // not the WebSocket transport, so wss:// would be accepted by the parser
    // and then fail at esp_mqtt_client_init(). Keep the product contract to
    // mqtt:// and mqtts://.
    return ((value.compare(0, secure.size(), secure) == 0) && (value.size() > secure.size()))
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
