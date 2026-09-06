#ifndef FURBLE_UI_H
#define FURBLE_UI_H

#include <cstdint>

namespace Furble {

class UI {
 public:
  static int32_t getBatteryLevel(void);
  static int16_t getBatteryVoltage(void);
  static int32_t getBatteryCurrent(void);
  static int16_t getBatteryVBUSVoltage(void);
  static bool isBatteryCharging(void);
  static uint8_t getIntervalometerState(void);
  static uint16_t getIntervalometerRemaining(void);
  static void notifyGestureSettingsChanged(void) {}
};

namespace Host {

void setBatteryStatus(int32_t level, int16_t voltage, int32_t current, int16_t vbus, bool charging);

}  // namespace Host
}  // namespace Furble

#endif
