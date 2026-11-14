// Seeded migration fuzzer for the batched settings default seeder.
//
// The production Settings::init runs against the checked in-memory NVS double.
// Each seed permutes the cfg_def_ver marker state, removes a random subset of
// default keys, and injects a commit failure at a random point. The contract is
// that a current marker never rewrites stored values, a missing/old/corrupt
// marker repairs every missing key, a failed seed never publishes the marker,
// and one clean boot after a failure repairs the store.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

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

bool removeKey(const char *name, const char *key) {
  Preferences prefs;
  if (!prefs.begin(name, false)) {
    return false;
  }
  const bool removed = !prefs.isKey(key) || prefs.remove(key);
  prefs.end();
  return removed;
}

void setMarker(uint32_t value) {
  Preferences prefs;
  if (prefs.begin(FURBLE_STR, false)) {
    prefs.put<uint32_t>(MARKER_KEY, value);
    prefs.end();
  }
}

uint32_t markerValue(void) {
  Preferences prefs;
  if (!prefs.begin(FURBLE_STR, true)) {
    return 0;
  }
  const uint32_t value = prefs.get<uint32_t>(MARKER_KEY, 0);
  prefs.end();
  return value;
}

bool allDefaultsPresent(void) {
  for (const auto &entry : Settings::all()) {
    const Settings::setting_t &setting = entry.second;
    if (setting.type == Settings::COMPANION_PASSWORD) {
      continue;
    }
    if (!keyExists(setting.nvs_namespace, setting.key)) {
      return false;
    }
  }
  return true;
}

void initialise(void) {
  nvs_test_reset();
  Settings::init();
}

void runSeed(uint32_t seed) {
  std::mt19937 rng(seed);
  initialise();
  check(markerValue() == Furble::SettingsBootstrap::DEFAULTS_VERSION,
        "seed " + std::to_string(seed) + ": fresh seed marker");

  // A user value must survive every migration mode.
  Settings::save<uint8_t>(Settings::BRIGHTNESS, 42);

  const uint32_t markerMode = rng() % 6;
  switch (markerMode) {
    case 0:
      check(removeKey(FURBLE_STR, MARKER_KEY), "marker removed");
      break;
    case 1:
      setMarker(Furble::SettingsBootstrap::DEFAULTS_VERSION - 1);
      break;
    case 2:
      setMarker(Furble::SettingsBootstrap::DEFAULTS_VERSION + 1);
      break;
    case 3:
    {
      Preferences prefs;
      if (prefs.begin(FURBLE_STR, false)) {
        prefs.putString(MARKER_KEY, "corrupt");
        prefs.end();
      }
      break;
    }
    case 4:
    {
      Preferences prefs;
      if (prefs.begin(FURBLE_STR, false)) {
        const uint8_t bytes[3] = {1, 2, 3};
        prefs.put(MARKER_KEY, bytes, sizeof(bytes));
        prefs.end();
      }
      break;
    }
    default:
      break;
  }

  // Remove a random subset of default keys to model a partial seed or a
  // damaged namespace. The marker stays as selected above.
  std::vector<const Settings::setting_t *> keys;
  for (const auto &entry : Settings::all()) {
    if (entry.second.type != Settings::COMPANION_PASSWORD
        && entry.second.type != Settings::BRIGHTNESS) {
      keys.push_back(&entry.second);
    }
  }
  const size_t removeCount = rng() % 6;
  for (size_t n = 0; n < removeCount; n++) {
    const auto *setting = keys[rng() % keys.size()];
    check(removeKey(setting->nvs_namespace, setting->key), "subset key removed");
  }

  const bool currentMarker = markerMode == 5;
  // Count the namespace commits this scan will actually make. A missing key
  // in each namespace stages one commit, then the marker is one more. Failing
  // past the last commit would not inject a failure at all.
  size_t namespaceCommits = 0;
  for (const auto &entry : Settings::all()) {
    const Settings::setting_t &setting = entry.second;
    if (setting.type == Settings::COMPANION_PASSWORD) {
      continue;
    }
    if (!keyExists(setting.nvs_namespace, setting.key) && namespaceCommits == 0) {
      namespaceCommits = 1;
      break;
    }
  }
  if (namespaceCommits != 0) {
    for (const auto &entry : Settings::all()) {
      const Settings::setting_t &setting = entry.second;
      if (setting.type != Settings::COMPANION_PASSWORD
          && !keyExists(setting.nvs_namespace, setting.key)
          && std::strcmp(setting.nvs_namespace, "furble") != 0) {
        namespaceCommits = 2;
        break;
      }
    }
  }
  const bool failCommit = !currentMarker && (rng() % 3) == 0;
  if (failCommit) {
    nvs_test_fail_commit_on(1 + (rng() % (namespaceCommits + 1)));
  }
  Settings::init();
  nvs_test_fail_commit_on(1000);

  if (!failCommit) {
    if (currentMarker) {
      check(markerValue() == Furble::SettingsBootstrap::DEFAULTS_VERSION,
            "seed " + std::to_string(seed) + ": current marker survives");
      check(Settings::load<uint8_t>(Settings::BRIGHTNESS) == 42,
            "seed " + std::to_string(seed) + ": current marker keeps user value");
    } else {
      check(markerValue() == Furble::SettingsBootstrap::DEFAULTS_VERSION,
            "seed " + std::to_string(seed) + ": migration writes the current marker");
      check(allDefaultsPresent(), "seed " + std::to_string(seed) + ": migration fills every key");
      check(Settings::load<uint8_t>(Settings::BRIGHTNESS) == 42,
            "seed " + std::to_string(seed) + ": migration keeps user value");
    }
  } else {
    // A failed batch must not publish a marker that claims the seed is whole.
    check(markerValue() != Furble::SettingsBootstrap::DEFAULTS_VERSION || currentMarker,
          "seed " + std::to_string(seed) + ": failed seed did not publish a current marker");
    // One clean boot repairs the store, whether the failure left a partial
    // namespace or only the marker missing.
    Settings::init();
    check(markerValue() == Furble::SettingsBootstrap::DEFAULTS_VERSION,
          "seed " + std::to_string(seed) + ": recovery writes the marker");
    check(allDefaultsPresent(), "seed " + std::to_string(seed) + ": recovery fills every key");
    check(Settings::load<uint8_t>(Settings::BRIGHTNESS) == 42,
          "seed " + std::to_string(seed) + ": recovery keeps user value");
  }
}

}  // namespace

int main() {
  for (uint32_t seed = 1; seed <= 48; seed++) {
    runSeed(seed);
  }
  if (failures != 0) {
    std::fprintf(stderr, "settings migration fuzz: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("settings migration fuzz: PASS\n");
  return 0;
}
