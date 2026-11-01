#ifndef FURBLE_OTA_ADMISSION_H
#define FURBLE_OTA_ADMISSION_H

#include <cstdint>

namespace Furble {
namespace OTA {
namespace MQTT {

/**
 * Minimum battery percentage for a firmware update.
 *
 * An update writes and verifies the inactive app slot and then reboots. The
 * board must survive that whole window; 30 percent is the same floor the
 * low-battery warning treats as "find power soon".
 */
constexpr uint8_t MIN_UPDATE_BATTERY_PERCENT = 30;

/**
 * Pure admission policy for a new update session.
 *
 * `controlIdle` is true only in Control::STATE_IDLE: a connect, an active
 * session, or a teardown all own the radio and must not share it with a
 * multi-megabyte transfer. `batteryKnown` is false on boards without a gauge;
 * refusing there would make OTA impossible, and there is no threshold to
 * evaluate, so the user-initiated update is allowed.
 */
constexpr bool updateAdmitted(bool controlIdle, bool batteryKnown, uint8_t batteryPercent) {
  if (!controlIdle) {
    return false;
  }
  return !batteryKnown || (batteryPercent >= MIN_UPDATE_BATTERY_PERCENT);
}

}  // namespace MQTT
}  // namespace OTA
}  // namespace Furble

#endif  // FURBLE_OTA_ADMISSION_H
