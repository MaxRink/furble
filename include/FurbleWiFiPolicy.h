#ifndef FURBLE_WIFI_POLICY_H
#define FURBLE_WIFI_POLICY_H

#include <cstdint>

namespace Furble {
namespace WiFiPolicy {

/** 2.4 GHz channels are the only ones the ESP32-S3 station supports. */
constexpr uint8_t CHANNEL_MIN = 1;
constexpr uint8_t CHANNEL_MAX = 14;

/** A stored auth mode has not been observed for this access point yet. */
constexpr uint8_t AUTH_UNKNOWN = 0xFF;

/** The remembered values that make a direct join possible without a scan. */
struct remembered_ap_t {
  bool bssid_set;
  uint8_t channel;
  bool auth_set;
  uint8_t auth_mode;
};

/** True when the channel is one the station can use. */
constexpr bool validChannel(uint8_t channel) {
  return (channel >= CHANNEL_MIN) && (channel <= CHANNEL_MAX);
}

/**
 * True when a direct join can skip the scan.
 *
 * The BSSID and a usable channel are required. The auth mode is optional: an
 * absent or unknown value must not block a join that already has the BSSID.
 */
constexpr bool fastConnectUsable(const remembered_ap_t &ap) {
  return ap.bssid_set && validChannel(ap.channel);
}

/** True when a remembered auth mode can be handed to the driver threshold. */
constexpr bool authUsable(bool auth_set, uint8_t auth_mode) {
  return auth_set && (auth_mode != AUTH_UNKNOWN);
}

/**
 * Decide whether the worker should scan before its next join.
 *
 * A failed direct join forces one scan as soon as the backoff expires instead
 * of waiting for two generic failures. The scan runs at most once per retry
 * cycle, which is what g_ScanAttempted tracks.
 */
constexpr bool shouldScan(uint32_t direct_failures, bool scan_attempted, bool fast_connect_failed) {
  return !scan_attempted && (fast_connect_failed || (direct_failures >= 2));
}

}  // namespace WiFiPolicy
}  // namespace Furble

#endif  // FURBLE_WIFI_POLICY_H
