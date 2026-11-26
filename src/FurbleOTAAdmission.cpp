#include "FurbleOTAAdmission.h"

#include <cstring>

namespace Furble {
namespace OTA {

namespace {

constexpr size_t MAX_VERSION_LENGTH = 63;
constexpr size_t MAX_VERSION_COMPONENTS = 4;

bool versionComponent(char digit, uint32_t &value) {
  if ((digit < '0') || (digit > '9')) {
    return false;
  }
  value = static_cast<uint32_t>(digit - '0');
  return true;
}

/**
 * Parse one dotted numeric version into at most four components.
 *
 * The whole string must be consumed, apart from an optional `-` or `+`
 * suffix. A non-numeric component or a fifth component is invalid rather than
 * silently truncated, so a new versioning scheme fails closed instead of
 * sorting arbitrarily.
 */
bool parseVersion(const char *text, uint32_t (&components)[MAX_VERSION_COMPONENTS]) {
  if ((text == nullptr) || (text[0] == '\0')) {
    return false;
  }

  const size_t length = std::strlen(text);
  if ((length == 0) || (length > MAX_VERSION_LENGTH)) {
    return false;
  }

  size_t index = 0;
  if ((text[0] == 'v') || (text[0] == 'V')) {
    index = 1;
    if (index == length) {
      return false;
    }
  }

  size_t component = 0;
  bool haveDigit = false;
  uint32_t value = 0;
  for (; index < length; index++) {
    const char character = text[index];
    if ((character == '-') || (character == '+')) {
      // A suffix is allowed only after at least one complete component. The
      // pending component still counts, so 1.2.3-rc1 stays 1.2.3.
      if (!haveDigit) {
        return false;
      }
      components[component++] = value;
      return true;
    }
    if (character == '.') {
      if (!haveDigit || (component + 1 >= MAX_VERSION_COMPONENTS)) {
        return false;
      }
      components[component++] = value;
      value = 0;
      haveDigit = false;
      continue;
    }
    uint32_t digit = 0;
    if (!versionComponent(character, digit)) {
      return false;
    }
    if (value > ((UINT32_MAX - digit) / 10)) {
      return false;
    }
    value = (value * 10) + digit;
    haveDigit = true;
  }

  if (!haveDigit) {
    return false;
  }
  components[component++] = value;
  return true;
}

}  // namespace

const char *refusalName(Refusal refusal) {
  switch (refusal) {
    case Refusal::None:
      return "none";
    case Refusal::RetainedCommand:
      return "retained_command";
    case Refusal::Busy:
      return "busy";
    case Refusal::CameraSession:
      return "camera_session";
    case Refusal::Intervalometer:
      return "intervalometer";
    case Refusal::LinkDown:
      return "link_down";
    case Refusal::BatteryLow:
      return "battery_low";
    case Refusal::SlotUnavailable:
      return "slot_unavailable";
    case Refusal::SourceUnavailable:
      return "source_unavailable";
    case Refusal::NoNewerVersion:
      return "no_newer_version";
    case Refusal::InvalidSource:
      return "invalid_source";
    case Refusal::TransportUnavailable:
      return "transport_unavailable";
  }
  return "unknown";
}

Refusal evaluateUpdate(const Conditions &conditions) {
  if (conditions.updateBusy) {
    return Refusal::Busy;
  }
  if (conditions.cameraSessionActive) {
    return Refusal::CameraSession;
  }
  if (conditions.intervalometerRunning) {
    return Refusal::Intervalometer;
  }
  if (!conditions.linkReady) {
    return Refusal::LinkDown;
  }
  if (!conditions.slotAvailable) {
    return Refusal::SlotUnavailable;
  }
  if (conditions.batteryKnown && !conditions.charging
      && (conditions.batteryPercent < MIN_UPDATE_BATTERY_PERCENT)) {
    return Refusal::BatteryLow;
  }
  return Refusal::None;
}

int compareVersions(const char *installed, const char *available) {
  uint32_t left[MAX_VERSION_COMPONENTS] = {};
  uint32_t right[MAX_VERSION_COMPONENTS] = {};

  const bool leftValid = parseVersion(installed, left);
  const bool rightValid = parseVersion(available, right);
  if (!rightValid) {
    return VERSION_INVALID;
  }
  if (!leftValid) {
    // A development build has no release number to compare. Treat it as older
    // than any valid release so the device can still install one.
    return -1;
  }

  for (size_t i = 0; i < MAX_VERSION_COMPONENTS; i++) {
    if (left[i] < right[i]) {
      return -1;
    }
    if (left[i] > right[i]) {
      return 1;
    }
  }
  return 0;
}

}  // namespace OTA
}  // namespace Furble
