#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "FurbleSettings.h"
#include "FurbleSettingsBootstrap.h"
#include "FurbleTypes.h"
#include "Preferences.h"
#include "nvs.h"

namespace {

using Furble::Preferences;
using Furble::Settings;

constexpr const char *MARKER_KEY = "cfg_def_ver";

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    failures++;
  }
}

bool keyExists(const char *name, const char *key) {
  Preferences prefs;
  if (!prefs.begin(name, true)) {
    return false;
  }
  const bool exists = prefs.isKey(key);
  prefs.end();
  return exists;
}

bool eraseKey(const char *name, const char *key) {
  Preferences prefs;
  if (!prefs.begin(name, false)) {
    return false;
  }
  const bool removed = !prefs.isKey(key) || prefs.remove(key);
  prefs.end();
  return removed;
}

uint8_t loadU8(Settings::type_t type) {
  return Settings::load<uint8_t>(type);
}

uint32_t storedMarker(void) {
  Preferences prefs;
  if (!prefs.begin(FURBLE_STR, true)) {
    return 0;
  }
  const uint32_t version = prefs.get<uint32_t>(MARKER_KEY, 0);
  prefs.end();
  return version;
}

}  // namespace

int main() {
  nvs_test_reset();
  Settings::init();

  // A fresh store owns two namespaces, so the seed must be one commit for the
  // M5ez defaults, one for the FURBLE defaults, and one for the marker. The old
  // per-setting path committed once per missing key.
  check(nvs_test_commit_count() == 3, "the fresh seed uses three commits");
  check(nvs_test_open_count() == 6, "the fresh seed opens the two readers, two writers and marker");

  for (const auto &entry : Settings::all()) {
    const Settings::setting_t &setting = entry.second;
    if (setting.type == Settings::COMPANION_PASSWORD) {
      check(!keyExists(setting.nvs_namespace, setting.key),
            std::string("password key stays unset: ") + setting.key);
      continue;
    }
    check(keyExists(setting.nvs_namespace, setting.key),
          std::string("default seeded: ") + setting.key);
  }

  check(loadU8(Settings::BRIGHTNESS) == 128, "brightness default is 128");
  check(Settings::load<Settings::BOOT_SPLASH>(), "boot splash defaults on");
  check(Settings::load<Settings::RECON_BACKOFF>(), "reconnect backoff defaults on");
  check(Settings::load<std::string>(Settings::NTP_SERVER) == "pool.ntp.org",
        "NTP server default is pool.ntp.org");
  check(storedMarker() == Furble::SettingsBootstrap::DEFAULTS_VERSION,
        "the version marker is written last");
#if defined(FURBLE_M5STICKS3)
  check(Settings::load<Settings::WATCHDOG>(), "StickS3 watchdog defaults on");
#endif

  // A second boot with the current marker must not touch any key.
  const size_t commitsAfterSeed = nvs_test_commit_count();
  const size_t opensAfterSeed = nvs_test_open_count();
  Settings::init();
  check(nvs_test_commit_count() == commitsAfterSeed, "the current marker performs no writes");
  check((nvs_test_open_count() - opensAfterSeed) <= 1,
        "the current marker opens only itself on boot");

  // An older or missing marker runs the scan once. Existing values must survive
  // and only the marker is written.
  Settings::save<Settings::BRIGHTNESS>(42);
  check(eraseKey(FURBLE_STR, MARKER_KEY), "the marker can be erased to simulate an upgrade");
  const size_t commitsBeforeUpgrade = nvs_test_commit_count();
  Settings::init();
  check(loadU8(Settings::BRIGHTNESS) == 42, "an upgrade keeps the stored brightness");
  check(storedMarker() == Furble::SettingsBootstrap::DEFAULTS_VERSION,
        "the upgrade writes the current marker");
  check(nvs_test_commit_count() == commitsBeforeUpgrade + 1,
        "the upgrade scan writes only the marker when every key exists");

  // A failed default commit must not mark the seed complete.
  nvs_test_reset();
  nvs_test_fail_commit_on(1);
  Settings::init();
  check(!keyExists(FURBLE_STR, MARKER_KEY), "a failed seed leaves the marker unset");
  check(nvs_test_value_type(FURBLE_STR, MARKER_KEY) == NVS_TEST_INVALID,
        "a failed seed stores no marker value");

  if (failures != 0) {
    return 1;
  }
  std::printf("Settings boot seed: PASS\n");
  return 0;
}
