// Host coverage for the production provisioning apply path.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "FurbleProvision.h"
#include "FurbleSettings.h"
#include "nvs.h"

namespace {

using Furble::Provision::apply;
using Furble::Provision::ApplyOptions;
using Furble::Provision::ApplyReport;
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
}

void testValidatedApplyAndRuntimeHooks() {
  resetSettings();

  ProvisionBundle bundle;
  bundle.settings = {
      {1,  ValueType::U8,     {77}                                              },
      {26, ValueType::U8,     {5}                                               },
      {33, ValueType::U8,     {4}                                               },
      {42, ValueType::U8,     {4}                                               },
      {27, ValueType::STRING, {'o', 'n', 'e', '-', 'b', 'u', 't', 't', 'o', 'n'}},
  };
  ApplyReport report;
  ApplyOptions options;
  options.onSettingApplied = recordApplied;

  check(apply(bundle, report, options), "valid settings apply successfully");
  check(report.ok && report.settingsApplied == bundle.settings.size(),
        "valid settings report every write");
  check(appliedIds == std::vector<uint8_t>({1, 26, 33, 42, 27}),
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
}

void testDomainValidation() {
  resetSettings();

  for (const SettingValue &invalid : {
           SettingValue {26, ValueType::U8,     {6}                 },
           SettingValue {33, ValueType::U8,     {5}                 },
           SettingValue {42, ValueType::U8,     {5}                 },
           SettingValue {27, ValueType::STRING, {'n', 'o', 'p', 'e'}},
  }) {
    ProvisionBundle bundle;
    bundle.settings = {invalid};
    ApplyReport report;
    check(!apply(bundle, report), "domain-invalid setting is rejected");
    check(report.error == Furble::Provision::ApplyError::BAD_SETTING,
          "domain-invalid setting reports BAD_SETTING");
    check(report.settingsApplied == 0, "domain-invalid setting writes nothing");
  }
}

}  // namespace

int main() {
  testPreflightIsAtomic();
  testValidatedApplyAndRuntimeHooks();
  testDomainValidation();

  if (failures != 0) {
    std::cerr << "provision apply tests: " << failures << " FAILED\n";
    return 1;
  }
  std::cout << "provision apply tests: PASS\n";
  return 0;
}
