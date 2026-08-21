// Host shim for include/FurbleSettings.h.
//
// The real header pulls in Preferences and the NVS-backed store. Control only
// reads a handful of settings, so the shim returns fixed host defaults that
// match a fresh device: reconnect off, transmit power P3, connection saver off,
// sleep-while-connected on, adaptive power off, reconnect backoff off. The
// include guard matches the real header exactly.
#ifndef SETTINGS_H
#define SETTINGS_H

#include <cstdint>

// Brings esp_power_level_t / ESP_PWR_LVL_P3 from the mock so load<>() defaults
// are self-contained regardless of include order.
#include <NimBLEDevice.h>

namespace Furble {
class Settings {
 public:
  Settings() = delete;
  ~Settings() = delete;

  // Only the members Control names are modelled. Values are arbitrary; the
  // load<>() defaults below key off the enumerator, not its integer value.
  typedef enum {
    TX_POWER,
    TX_ADAPTIVE,
    RECONNECT,
    RECON_BACKOFF,
    SLEEP_CONN,
    CONN_SAVER,
  } type_t;

  // Bind each setting to its storage type, mirroring the real header so
  // Control's load<Settings::CONN_SAVER>() form deduces the same type.
  template <type_t S>
  struct storage_type;

  template <type_t S>
  static typename storage_type<S>::type load() {
    return load<typename storage_type<S>::type>(S);
  }

  template <typename T>
  static T load(type_t type);
};

template <>
struct Settings::storage_type<Settings::TX_ADAPTIVE> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::RECONNECT> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::RECON_BACKOFF> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::SLEEP_CONN> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::CONN_SAVER> {
  using type = bool;
};

template <>
inline bool Settings::load<bool>(type_t type) {
  // SLEEP_CONN defaults on so setState() never holds the (no-op) sleep lock.
  // Every other boolean Control reads defaults off on a fresh device.
  return type == SLEEP_CONN;
}

template <>
inline esp_power_level_t Settings::load<esp_power_level_t>(type_t type) {
  (void)type;
  return ESP_PWR_LVL_P3;
}

}  // namespace Furble

#endif
