// Host coverage for the production provisioning apply path.

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "FurbleProvision.h"
#include "FurbleSettings.h"
#include "nvs.h"
#include "protocol/ProvisionTLV.h"

namespace {

using Furble::Provision::apply;
using Furble::Provision::ApplyOptions;
using Furble::Provision::ApplyReport;
using Furble::ProvisionTLV::COMPANION_PASSWORD_WIRE_ID;
using Furble::ProvisionTLV::ProvisionBundle;
using Furble::ProvisionTLV::SettingValue;
using Furble::ProvisionTLV::ValueType;

int failures = 0;
std::vector<uint8_t> appliedIds;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

void recordApplied(uint8_t wireId) {
  appliedIds.push_back(wireId);
}

void resetSettings() {
  appliedIds.clear();
  nvs_test_reset();
  Furble::Settings::init();
}

void testPreflightIsAtomic() {
  resetSettings();

  ProvisionBundle bundle;
  bundle.companionPassword = std::vector<uint8_t> {'n', 'e', 'w'};
  bundle.settings = {
      {1,  ValueType::U8, {77}},
      {26, ValueType::U8, {6} }, // GPS duty only accepts 0, 5, 10, or 15.
  };
  ApplyReport report;
  ApplyOptions options;
  options.onSettingApplied = recordApplied;

  check(!apply(bundle, report, options), "invalid later setting rejects the whole batch");
  check(report.error == Furble::Provision::ApplyError::BAD_SETTING,
        "invalid later setting reports BAD_SETTING");
  check(report.failedSettingId == 26, "atomic failure identifies the invalid setting");
  check(report.settingsApplied == 0, "atomic failure reports no settings applied");
  check(appliedIds.empty(), "atomic failure emits no runtime reload callbacks");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS) == 128,
        "atomic failure leaves the earlier setting unchanged");
  check(Furble::Settings::load<std::string>(Furble::Settings::COMPANION_PASSWORD).empty(),
        "atomic failure leaves the companion password unchanged");
}

void testValidatedApplyAndRuntimeHooks() {
  resetSettings();

  ProvisionBundle bundle;
  bundle.settings = {
      {1,                          ValueType::U8,     {77}                                              },
      {26,                         ValueType::U8,     {5}                                               },
      {33,                         ValueType::U8,     {4}                                               },
      {69,                         ValueType::U8,     {4}                                               },
      {27,                         ValueType::STRING, {'o', 'n', 'e', '-', 'b', 'u', 't', 't', 'o', 'n'}},
      {46,                         ValueType::BOOL,   {1}                                               },
      {COMPANION_PASSWORD_WIRE_ID,
       ValueType::STRING,
       {'s', 'e', 't', '-', 'b', 'y', '-', 'i', 'd'}                                                    },
      {72,                         ValueType::U8,     {3}                                               },
      {73,                         ValueType::BOOL,   {1}                                               },
  };
  ApplyReport report;
  ApplyOptions options;
  options.onSettingApplied = recordApplied;

  check(apply(bundle, report, options), "valid settings apply successfully");
  check(report.ok && report.settingsApplied == bundle.settings.size(),
        "valid settings report every write");
  check(appliedIds
            == std::vector<uint8_t>({1, 26, 33, 69, 27, 46, COMPANION_PASSWORD_WIRE_ID, 72, 73}),
        "runtime callback follows successful write order");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS) == 77,
        "validated uint8 setting is persisted");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::GPS_DUTY) == 5,
        "validated GPS duty setting is persisted");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::FB_OUTPUT) == 4,
        "validated feedback output setting is persisted");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::GPS_PLATFORM) == 4,
        "validated GPS platform setting is persisted");
  check(Furble::Settings::load<std::string>(Furble::Settings::BUTTON_MODE)
            == Furble::Settings::BUTTON_MODE_ONE_BUTTON_VALUE,
        "validated button mode setting is persisted");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::IMU_WAKE) == 3,
        "validated IMU wake setting is persisted");
  check(Furble::Settings::load<bool>(Furble::Settings::IMU), "validated IMU setting is persisted");
  check(Furble::Settings::load<bool>(Furble::Settings::IMU_TRIG),
        "validated IMU trigger setting is persisted");
  check(Furble::Settings::load<std::string>(Furble::Settings::COMPANION_PASSWORD) == "set-by-id",
        "validated companion password setting is persisted");
}

void testDedicatedPasswordField() {
  resetSettings();

  ProvisionBundle bundle;
  bundle.companionPassword = std::vector<uint8_t> {'d', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'd'};
  ApplyReport report;
  ApplyOptions options;
  options.onSettingApplied = recordApplied;
  check(apply(bundle, report, options), "dedicated companion password applies");
  check(report.settingsApplied == 1 && report.deferredFields == 0,
        "dedicated companion password is counted as an applied setting");
  check(Furble::Settings::load<std::string>(Furble::Settings::COMPANION_PASSWORD) == "dedicated",
        "dedicated companion password is persisted");
  check(appliedIds == std::vector<uint8_t>({COMPANION_PASSWORD_WIRE_ID}),
        "dedicated companion password invokes the wire-id callback");

  resetSettings();
  bundle.companionPassword = std::vector<uint8_t> {};
  check(!apply(bundle, report), "empty dedicated companion password is rejected");
  check(report.failedSettingId == COMPANION_PASSWORD_WIRE_ID && report.settingsApplied == 0,
        "empty dedicated companion password reports wire id 47 without writing");

  resetSettings();
  bundle.companionPassword = std::vector<uint8_t> {'a', 0, 'b'};
  check(!apply(bundle, report), "NUL-containing dedicated companion password is rejected");
  check(report.failedSettingId == COMPANION_PASSWORD_WIRE_ID && report.settingsApplied == 0,
        "malformed dedicated companion password reports wire id 47 without writing");

  resetSettings();
  bundle.companionPassword = std::vector<uint8_t> {'d', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'd'};
  bundle.settings = {
      {COMPANION_PASSWORD_WIRE_ID, ValueType::STRING, {'o', 't', 'h', 'e', 'r'}}
  };
  check(!apply(bundle, report), "duplicate companion password sources are rejected");
  check(report.failedSettingId == COMPANION_PASSWORD_WIRE_ID && report.settingsApplied == 0,
        "duplicate password rejection happens before either source is written");
}

void testDomainValidation() {
  resetSettings();

  // The expected message matters as much as the rejection. A setting id with no
  // row in SETTING_SCHEMAS is also rejected, but as UNSUPPORTED_SETTING before
  // the domain rule is ever reached, so asserting only "it failed" would pass
  // for a setting the bundle can never carry at all.
  const std::pair<SettingValue, const char *> cases[] = {
      {SettingValue {26, ValueType::U8, {6}},                      "GPS duty must be 0, 5, 10 or 15"         },
      {SettingValue {33, ValueType::U8, {5}},                      "feedback output is out of range"         },
      {SettingValue {27, ValueType::STRING, {'n', 'o', 'p', 'e'}}, "button mode is not recognised"           },
      {SettingValue {67, ValueType::U8, {5}},                      "GPS fix hold must be 0 through 4"        },
      {SettingValue {68, ValueType::BOOL, {2}},                    "boolean setting must be 0 or 1"          },
      {SettingValue {72, ValueType::U8, {4}},                      "IMU wake gesture must be 0, 1, 2 or 3"   },
      {SettingValue {69, ValueType::U8, {5}},                      "GPS platform setting must be 0 through 4"},
  };

  for (const auto &entry : cases) {
    ProvisionBundle bundle;
    bundle.settings = {entry.first};
    ApplyReport report;
    check(!apply(bundle, report), "domain-invalid setting is rejected");
    check(report.error == Furble::Provision::ApplyError::BAD_SETTING,
          std::string("setting ") + std::to_string(entry.first.wireId)
              + " reports BAD_SETTING, not a missing schema row");
    check(report.failedSettingId == entry.first.wireId, "the rejected setting id is reported");
    check(report.message == entry.second,
          std::string("setting ") + std::to_string(entry.first.wireId) + " names its own rule");
    check(report.settingsApplied == 0, "domain-invalid setting writes nothing");
  }
}

// Every setting the companion can name by wire id needs a row in
// SETTING_SCHEMAS, or schemaForSetting() returns nullptr and the whole bundle
// is rejected as UNSUPPORTED_SETTING before any domain rule runs. That failure
// is silent from the settings table's point of view: nothing in FurbleSettings
// knows the mirror exists. Adding a setting and forgetting the row is therefore
// the easy mistake, and this is the guard for it.
//
// The two ids below are already missing on master. Registering them changes the
// provisioning surface for settings this test's PR did not add, so they are
// named here as a known gap rather than quietly fixed. Do not extend this list
// to cover a new setting: add the schema row instead.
void testEverySettingHasASchemaRow() {
  static constexpr uint8_t KNOWN_MISSING[] = {
      43,  // AUTO_OFF_CHARGING
  };

  for (const auto &entry : Furble::Settings::all()) {
    const uint8_t wireId = entry.second.wire_id;
    if (wireId == 0) {
      // Off-wire settings are deliberately unreachable by id.
      continue;
    }
    const bool known = std::find(std::begin(KNOWN_MISSING), std::end(KNOWN_MISSING), wireId)
                       != std::end(KNOWN_MISSING);
    const bool registered = Furble::ProvisionTLV::schemaForSetting(wireId) != nullptr;
    if (known) {
      check(!registered, std::string("wire id ") + std::to_string(wireId)
                             + " is still the known gap, drop it from KNOWN_MISSING if fixed");
      continue;
    }
    check(registered, std::string("wire id ") + std::to_string(wireId) + " (" + entry.second.key
                          + ") has a SETTING_SCHEMAS row");
  }
}

}  // namespace

int main() {
  testPreflightIsAtomic();
  testValidatedApplyAndRuntimeHooks();
  testDedicatedPasswordField();
  testDomainValidation();
  testEverySettingHasASchemaRow();

  if (failures != 0) {
    std::cerr << "provision apply tests: " << failures << " FAILED\n";
    return 1;
  }
  std::cout << "provision apply tests: PASS\n";
  return 0;
}
