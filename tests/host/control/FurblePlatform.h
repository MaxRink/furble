// Host shim for include/FurblePlatform.h.
//
// The real header pulls in M5PM1 and esp_err, which are hardware only. Control
// touches just Platform::getInstance().watchdogFeed(), so the shim models that
// one call and nothing else. The include guard matches the real header exactly,
// so if the real header is reached through any transitive include it is a no-op
// here instead of a duplicate definition.
#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

#include <cstdint>

// ble_gap_conn_cancel() cancels an in-flight NimBLE connection attempt. On the
// device it is declared by a transitively included NimBLE host header; the mock
// NimBLE does not declare it, so the shim provides the declaration here (the
// first Furble header Control includes after Device.h and FurbleControl.h). It
// is defined as a no-op stub in control_shim.cpp.
extern "C" int ble_gap_conn_cancel(void);

namespace Furble {
class Platform {
 public:
  static Platform &getInstance();

  Platform(Platform const &) = delete;
  Platform(Platform &&) = delete;
  Platform &operator=(Platform const &) = delete;
  Platform &operator=(Platform &&) = delete;

  void watchdogFeed(void) {}

 private:
  Platform() {}
};
}  // namespace Furble

#endif
