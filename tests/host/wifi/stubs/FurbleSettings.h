#pragma once
#include <string>
namespace Furble {
// Mutable so the host test can prove the lazy init path with Wi-Fi disabled.
inline bool wifiEnabledSetting = true;
class Settings {
 public:
  enum type_t { WIFI, WIFI_SSID, WIFI_PSK, NTP, NTP_SERVER };
  template <type_t>
  static auto load();
  template <type_t, typename T>
  static bool save(const T &) {
    return true;
  }
  static bool validNetworkString(type_t, const std::string &);
};
template <>
inline auto Settings::load<Settings::WIFI>() {
  return wifiEnabledSetting;
}
template <>
inline auto Settings::load<Settings::WIFI_SSID>() {
  return std::string("ssid");
}
template <>
inline auto Settings::load<Settings::WIFI_PSK>() {
  return std::string("password");
}
template <>
inline auto Settings::load<Settings::NTP>() {
  return true;
}
template <>
inline auto Settings::load<Settings::NTP_SERVER>() {
  return std::string("pool.ntp.org");
}
}  // namespace Furble
