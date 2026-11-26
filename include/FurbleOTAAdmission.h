#ifndef FURBLE_OTA_ADMISSION_H
#define FURBLE_OTA_ADMISSION_H

#include <cstdint>

namespace Furble {
namespace OTA {

/**
 * Minimum battery percentage for a firmware update on a gauge-equipped board.
 *
 * An update writes and verifies the inactive app slot and then reboots. The
 * board must survive that whole window; 30 percent is the same floor the
 * low-battery warning treats as "find power soon". A board on external power
 * is exempt, because the write and reboot window no longer drains the cell.
 */
constexpr uint8_t MIN_UPDATE_BATTERY_PERCENT = 30;

/**
 * Why an update or check request is refused.
 *
 * The order of the enumerators is the evaluation order documented by
 * evaluateUpdate(). A caller reports the first failing gate, so a user can fix
 * one condition and see the next one instead of a compound message.
 */
enum class Refusal : uint8_t {
  None,
  RetainedCommand,
  Busy,
  CameraSession,
  Intervalometer,
  LinkDown,
  BatteryLow,
  SlotUnavailable,
  SourceUnavailable,
  NoNewerVersion,
  InvalidSource,
  TransportUnavailable,
};

/** Stable topic and console label for a refusal. */
const char *refusalName(Refusal refusal);

/**
 * Runtime observations for the user-facing preconditions.
 *
 * Every field is a value snapshot taken by the composition. The policy reads
 * no hardware, task or setting itself, so the same rules run in the host
 * tests, the simulator and the firmware.
 */
struct Conditions {
  /** An update is already running on this device. */
  bool updateBusy = false;
  /** A camera owns the radio: connect, active session or teardown. */
  bool cameraSessionActive = false;
  /** The intervalometer or timer page is running. */
  bool intervalometerRunning = false;
  /** The station link that carries the download is up. */
  bool linkReady = false;
  /** The board reports a battery gauge. A board without one is not refused. */
  bool batteryKnown = false;
  uint8_t batteryPercent = 0;
  /** External power is present, which exempts the battery floor. */
  bool charging = false;
  /** The board has a second app slot this image can be written to. */
  bool slotAvailable = true;
};

/**
 * Decide whether an update may start.
 *
 * The first failing gate names the reason, in this order: busy, camera
 * session, intervalometer, link, battery, slot. A board without a battery
 * gauge has no threshold to evaluate and is allowed. A charging board below
 * the floor is allowed, because the update no longer has to finish on the
 * cell.
 */
Refusal evaluateUpdate(const Conditions &conditions);

/** Result of compareVersions() when a string is not a dotted numeric version. */
constexpr int VERSION_INVALID = 2;

/**
 * Compare two dotted numeric versions.
 *
 * Accepts an optional leading `v`, one to four decimal components, and
 * ignores a `-prerelease` or `+build` suffix after the numeric part. Missing
 * trailing components count as zero, so `1.2` equals `1.2.0`. Returns -1 when
 * installed is older, 0 when equal, 1 when newer, and VERSION_INVALID when
 * either string is empty, longer than 63 bytes or not numeric.
 *
 * An installed `dev+g<hash>` build is not comparable. The caller treats that
 * as older than any valid available release, so a development device can
 * still install a release image.
 */
int compareVersions(const char *installed, const char *available);

/** True when available is strictly newer than installed. */
constexpr bool versionIsNewer(int comparison) {
  return comparison < 0;
}

namespace MQTT {

/**
 * The plan 130 admission gate keeps the plan 34 constant name.
 *
 * The value is the same policy floor as `Furble::OTA::MIN_UPDATE_BATTERY_PERCENT`;
 * the alias exists so an existing caller does not need a namespace change.
 */
constexpr uint8_t MIN_UPDATE_BATTERY_PERCENT = ::Furble::OTA::MIN_UPDATE_BATTERY_PERCENT;

/**
 * Pure admission policy for a new signed-envelope OTA session.
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
