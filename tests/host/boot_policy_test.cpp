#include <cstdint>
#include <cstdio>

#include "FurbleSettingsBootstrap.h"
#include "FurbleWiFiPolicy.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    failures++;
  }
}

}  // namespace

int main() {
  using Furble::SettingsBootstrap::DEFAULTS_VERSION;
  using Furble::SettingsBootstrap::MAX_BATCH_NAMESPACES;
  using Furble::SettingsBootstrap::needsDefaultScan;

  check(MAX_BATCH_NAMESPACES >= 2, "both current settings namespaces fit one batch");
  check(needsDefaultScan(0, DEFAULTS_VERSION), "a missing marker always scans");
  check(!needsDefaultScan(DEFAULTS_VERSION, DEFAULTS_VERSION), "the current marker skips the scan");
  check(needsDefaultScan(DEFAULTS_VERSION - 1, DEFAULTS_VERSION), "an old marker rescans");
  check(needsDefaultScan(DEFAULTS_VERSION + 1, DEFAULTS_VERSION), "a future marker rescans");

  using Furble::WiFiPolicy::AUTH_UNKNOWN;
  using Furble::WiFiPolicy::authUsable;
  using Furble::WiFiPolicy::fastConnectUsable;
  using Furble::WiFiPolicy::remembered_ap_t;
  using Furble::WiFiPolicy::shouldScan;
  using Furble::WiFiPolicy::validChannel;

  check(!validChannel(0), "channel zero is not a station channel");
  check(validChannel(1), "channel 1 is valid");
  check(validChannel(14), "channel 14 is valid");
  check(!validChannel(15), "channel 15 is outside 2.4 GHz");

  const remembered_ap_t complete = {true, 6, true, 3};
  check(fastConnectUsable(complete), "a BSSID with a valid channel skips the scan");
  check(fastConnectUsable({true, 6, false, AUTH_UNKNOWN}),
        "an unknown auth mode does not block the direct join");
  check(!fastConnectUsable({false, 6, true, 3}), "no BSSID means no direct join");
  check(!fastConnectUsable({true, 0, true, 3}), "an unset channel means no direct join");
  check(!fastConnectUsable({true, 15, true, 3}), "an out-of-range channel means no direct join");

  check(authUsable(true, 0), "an open network has a usable stored auth mode");
  check(authUsable(true, 3), "a WPA2 auth mode is usable");
  check(!authUsable(false, 3), "an unset auth mode is not usable");
  check(!authUsable(true, AUTH_UNKNOWN), "the unknown sentinel is not usable");

  check(!shouldScan(0, false, false), "a healthy first direct join does not scan");
  check(!shouldScan(1, false, false), "one generic failure still does not scan");
  check(shouldScan(2, false, false), "two generic failures trigger the scan");
  check(shouldScan(0, false, true), "a failed direct join triggers the scan immediately");
  check(!shouldScan(2, true, true), "the scan runs once per retry cycle");
  check(!shouldScan(0, true, false), "an attempted scan is not repeated");

  if (failures != 0) {
    return 1;
  }
  std::printf("Boot settings and WiFi policy: PASS\n");
  return 0;
}
