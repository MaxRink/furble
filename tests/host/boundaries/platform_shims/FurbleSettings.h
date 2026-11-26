#ifndef FURBLE_PLATFORM_SHIM_SETTINGS_H
#define FURBLE_PLATFORM_SHIM_SETTINGS_H

#include <cstdint>

namespace Furble {
class Settings {
 public:
  enum type_t {
    IMU = 0,
    FB_OUTPUT = 1,
  };

  template <type_t S>
  struct storage_type;

  template <type_t S>
  static typename storage_type<S>::type load() {
    return typename storage_type<S>::type();
  }

  template <typename T>
  static T load(type_t type);
};
template <>
struct Settings::storage_type<Settings::IMU> {
  using type = bool;
};

template <>
struct Settings::storage_type<Settings::FB_OUTPUT> {
  using type = uint8_t;
};

}  // namespace Furble

#endif
