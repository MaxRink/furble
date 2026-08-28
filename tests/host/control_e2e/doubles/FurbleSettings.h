#ifndef SETTINGS_H
#define SETTINGS_H

// Slim Settings double for the control end-to-end harness. The real Settings
// reads NVS through Preferences; the harness backs the handful of keys the real
// Control reads with an in-memory store the scenario can seed. Guard name
// matches the real header. esp_power_level_t is already defined by the time
// this header is parsed (Control.cpp includes Device.h, hence NimBLEDevice.h,
// first).

#include <cstdint>

namespace Furble {

class Settings {
 public:
  Settings() = delete;
  ~Settings() = delete;

  typedef enum {
    TX_POWER,
    TX_ADAPTIVE,
    RECON_BACKOFF,
    SLEEP_CONN,
    CONN_SAVER,
  } type_t;

  static void init(void);

  template <type_t S>
  struct storage_type;

  template <type_t S>
  static typename storage_type<S>::type load() {
    return load<typename storage_type<S>::type>(S);
  }

  template <type_t S>
  static void save(const typename storage_type<S>::type &value) {
    save<typename storage_type<S>::type>(S, value);
  }

  template <typename T>
  static T load(type_t type);

  template <typename T>
  static void save(type_t type, const T &value);

  // Harness helpers: seed a boolean setting before a scenario drives Control.
  static void setBool(type_t type, bool value);

  // Battery Saver is not modelled in this harness, so the effective power
  // accessors the real Control now calls return the seeded stored value. They
  // are defined in doubles.cpp, after the load<bool> specialization.
  static bool sleepConnEffective(void);
  static bool connSaverEffective(void);
  static bool reconBackoffEffective(void);
  static bool consumeCleanRestart(void) { return false; }
};

template <>
struct Settings::storage_type<Settings::TX_ADAPTIVE> {
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

}  // namespace Furble

#endif
