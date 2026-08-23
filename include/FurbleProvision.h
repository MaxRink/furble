#ifndef FURBLE_PROVISION_H
#define FURBLE_PROVISION_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "../lib/furble/protocol/ProvisionTLV.h"

namespace Furble {
namespace Provision {

enum class ApplyError : uint8_t {
  NONE,
  UNKNOWN_SETTING_ID,
  BAD_SETTING,
  UNSUPPORTED_SETTING,
};

struct ApplyReport {
  bool ok = false;
  ApplyError error = ApplyError::NONE;
  uint8_t failedSettingId = 0;
  size_t settingsApplied = 0;
  size_t deferredFields = 0;
  std::string message;
};

/** Validate every setting, then persist the whole settings portion. */
bool apply(const ProvisionTLV::ProvisionBundle &bundle, ApplyReport &report);

const char *applyErrorString(ApplyError error);

}  // namespace Provision
}  // namespace Furble

#endif
