// Deterministic permutation and random-sequence coverage for the WiFi
// fast-connect policy.
//
// The production WiFiPolicy helpers decide whether a remembered BSSID and
// channel can skip a scan and when a failed direct join must fall back. This
// test exhausts every channel and auth shape, then runs seeded sequences that
// model a moved AP, a wrong password, and a scan that does or does not recover
// the SSID.

#include <cstdint>
#include <cstdio>
#include <random>

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
  using namespace Furble::WiFiPolicy;

  // Every channel byte is either a valid 2.4 GHz station channel or not.
  for (uint32_t channel = 0; channel <= 255; channel++) {
    const bool valid = (channel >= 1) && (channel <= 14);
    check(validChannel(static_cast<uint8_t>(channel)) == valid,
          "validChannel disagrees at a channel boundary");
    for (int bssid = 0; bssid <= 1; bssid++) {
      const remembered_ap_t ap {bssid != 0, static_cast<uint8_t>(channel), true, 3};
      check(fastConnectUsable(ap) == ((bssid != 0) && valid),
            "fastConnectUsable disagrees at a BSSID or channel boundary");
    }
  }

  // The auth mode is optional for the direct join but never usable when unset
  // or marked unknown. A stale auth value must not block the remembered BSSID.
  for (uint32_t auth = 0; auth <= 255; auth++) {
    check(authUsable(false, static_cast<uint8_t>(auth)) == false,
          "unset auth mode was usable");
    check(authUsable(true, static_cast<uint8_t>(auth))
              == (auth != AUTH_UNKNOWN),
          "authUsable disagrees at the unknown sentinel");
  }
  for (int bssid = 0; bssid <= 1; bssid++) {
    for (uint32_t channel = 0; channel <= 20; channel++) {
      const remembered_ap_t unknown {bssid != 0, static_cast<uint8_t>(channel), false,
                                     AUTH_UNKNOWN};
      check(fastConnectUsable(unknown)
                == ((bssid != 0) && (channel >= 1) && (channel <= 14)),
            "an unknown auth mode changed the direct-join decision");
    }
  }

  // A scan runs at most once per retry cycle. A failed direct join forces it
  // immediately; two generic failures do the same on the next cycle.
  for (uint32_t failuresCount = 0; failuresCount <= 8; failuresCount++) {
    check(shouldScan(failuresCount, false, false) == (failuresCount >= 2),
          "generic failure threshold changed");
    check(!shouldScan(failuresCount, true, false),
          "a scan repeated in the same retry cycle");
    check(shouldScan(failuresCount, false, true),
          "a failed direct join did not force a scan");
    check(!shouldScan(failuresCount, true, true),
          "a failed direct join repeated an attempted scan");
  }

  // Seeded lifecycle permutations: a moved AP, a wrong password, and a scan
  // that recovers or does not. Keep the state machine small and explicit so a
  // policy change fails with the exact seed and step.
  for (uint32_t seed = 1; seed <= 64; seed++) {
    std::mt19937 rng(seed);
    remembered_ap_t ap {true, static_cast<uint8_t>(1 + (rng() % 14)), true,
                        static_cast<uint8_t>(rng() % 5)};
    uint32_t directFailures = 0;
    bool scanAttempted = false;
    bool fastConnectFailed = false;
    for (uint32_t step = 0; step < 24; step++) {
      const uint32_t action = rng() % 5;
      if (action == 0) {
        // The AP moved or the stored BSSID went stale.
        ap = {false, 0, false, AUTH_UNKNOWN};
        fastConnectFailed = true;
      } else if (action == 1) {
        // The user entered a wrong password: the join fails, the SSID remains.
        directFailures++;
      } else if (action == 2) {
        // A scan found the SSID again.
        ap = {true, static_cast<uint8_t>(1 + (rng() % 14)), true,
              static_cast<uint8_t>(rng() % 5)};
        scanAttempted = false;
        fastConnectFailed = false;
        directFailures = 0;
      } else if (action == 3) {
        // A successful join resets the retry cycle.
        directFailures = 0;
        scanAttempted = false;
        fastConnectFailed = false;
      } else {
        // The backoff expired and the worker is about to decide.
        const bool scan = shouldScan(directFailures, scanAttempted, fastConnectFailed);
        if (scan) {
          check(!scanAttempted, "seed repeated a scan without a recovery");
          scanAttempted = true;
          fastConnectFailed = false;
          if (rng() % 2 == 0) {
            ap = {true, static_cast<uint8_t>(1 + (rng() % 14)), true,
                  static_cast<uint8_t>(rng() % 5)};
          }
        } else {
          check(!(fastConnectFailed && !scanAttempted),
                "seed skipped a required fallback scan");
        }
      }
      check(!ap.bssid_set || validChannel(ap.channel),
            "seed kept an invalid direct-join channel");
    }
  }

  if (failures != 0) {
    std::fprintf(stderr, "wifi policy fuzz: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("wifi policy fuzz: PASS\n");
  return 0;
}
