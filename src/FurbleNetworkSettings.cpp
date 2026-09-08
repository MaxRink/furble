#include "FurbleSettings.h"

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
}  // namespace Furble
