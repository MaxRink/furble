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

// A new wire id is not provisionable until it has a row in SETTING_SCHEMAS.
// Without one the parser rejects UNKNOWN_SETTING_ID and the apply path reports
// UNSUPPORTED_SETTING before any validation runs, so the setting is silently
// unreachable over the companion link. Deleting the {74, U8, 1, 1} row fails
// the first check below.
void testMotionEngineProvisioning() {
  resetSettings();

  const auto *schema = Furble::ProvisionTLV::schemaForSetting(74);
  check(schema != nullptr, "wire id 74 has a provisioning schema row");
  if (schema != nullptr) {
    check(schema->type == ValueType::U8, "the motion engine schema is U8");
    check((schema->minLength == 1) && (schema->maxLength == 1),
          "the motion engine schema is exactly one byte");
  }

  // The whole roller range provisions through the real validate path.
  for (uint8_t value = 0; value <= 2; value++) {
    resetSettings();
    ProvisionBundle bundle;
    bundle.settings = {
        {74, ValueType::U8, {value}},
    };
    ApplyReport report;
    ApplyOptions options;
    options.onSettingApplied = recordApplied;
    check(apply(bundle, report, options),
          "motion engine value " + std::to_string(value) + " provisions");
    check(report.settingsApplied == 1, "the motion engine apply reports one setting");
    check(Furble::Settings::load<uint8_t>(Furble::Settings::HW_MOTION) == value,
          "the provisioned motion engine value reaches the store");
  }

  // Anything past Hardware is a domain error, not a silent clamp.
  resetSettings();
  ProvisionBundle outOfRange;
  outOfRange.settings = {
      {74, ValueType::U8, {3}},
  };
  ApplyReport report;
  ApplyOptions options;
  check(!apply(outOfRange, report, options), "motion engine value 3 is rejected");
  check(report.error == Furble::Provision::ApplyError::BAD_SETTING,
        "an out-of-range motion engine reports BAD_SETTING");
  check(report.failedSettingId == 74, "the rejection identifies wire id 74");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::HW_MOTION)
            == Furble::Settings::HW_MOTION_AUTO,
        "a rejected motion engine leaves the stored value alone");

  // A wrong wire type is refused before the range check.
  resetSettings();
  ProvisionBundle wrongType;
  wrongType.settings = {
      {74, ValueType::BOOL, {1}},
  };
  ApplyReport typeReport;
  check(!apply(wrongType, typeReport, options), "a BOOL motion engine field is rejected");
  check(typeReport.error == Furble::Provision::ApplyError::UNSUPPORTED_SETTING,
        "a wrong wire type reports UNSUPPORTED_SETTING");
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
      {27, ValueType::STRING, {'o', 'n', 'e', '-', 'b', 'u', 't', 't', 'o', 'n'}},
      {66, ValueType::BOOL,   {1}                                               },
  };
  ApplyReport report;
  ApplyOptions options;
  options.onSettingApplied = recordApplied;

  check(apply(bundle, report, options), "valid settings apply successfully");
  check(report.ok && report.settingsApplied == bundle.settings.size(),
        "valid settings report every write");
  check(appliedIds == std::vector<uint8_t>({1, 26, 33, 27, 66}),
        "runtime callback follows successful write order");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS) == 77,
        "validated uint8 setting is persisted");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::GPS_DUTY) == 5,
        "validated GPS duty setting is persisted");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::FB_OUTPUT) == 4,
        "validated feedback output setting is persisted");
  check(Furble::Settings::load<std::string>(Furble::Settings::BUTTON_MODE)
            == Furble::Settings::BUTTON_MODE_ONE_BUTTON_VALUE,
        "validated button mode setting is persisted");
  // Wire id 66 is only provisionable because it has a SETTING_SCHEMAS row in
  // lib/furble/protocol/ProvisionTLV.cpp. Provision::validateSetting() looks
  // the wire id up there before it validates anything, and the decoder rejects
  // an unknown id outright, so a missing row fails the whole batch before the
  // value is ever examined. This assertion is what catches that.
  check(Furble::Settings::load<bool>(Furble::Settings::GPS_MOTION),
        "validated motion adaptive setting is persisted");
}

void testDomainValidation() {
  resetSettings();

  for (const SettingValue &invalid : {
           SettingValue {26, ValueType::U8,     {6}                 },
           SettingValue {33, ValueType::U8,     {5}                 },
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
  testMotionEngineProvisioning();
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
