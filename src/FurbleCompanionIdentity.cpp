#include "FurbleCompanionIdentity.h"

#include <Preferences.h>
#include <esp_log.h>

#include "Device.h"
#include "FurbleTypes.h"

namespace Furble {
namespace CompanionIdentity {

namespace {

// ESP-IDF NVS keys are at most 15 characters. Keep this distinct from the
// persisted peer-bond key "companion_addr".
constexpr const char *IDENTITY_KEY = "companion_bleid";
constexpr size_t IDENTITY_BYTES = 6;

bool load(uint8_t (&address)[IDENTITY_BYTES]) {
  Preferences preferences;
  if (!preferences.begin(FURBLE_STR, true)) {
    return false;
  }
  const size_t length = preferences.getBytesLength(IDENTITY_KEY);
  if (length != IDENTITY_BYTES) {
    preferences.end();
    return false;
  }
  const size_t read = preferences.get(IDENTITY_KEY, address, IDENTITY_BYTES);
  preferences.end();
  return read == IDENTITY_BYTES;
}

bool store(const uint8_t (&address)[IDENTITY_BYTES]) {
  Preferences preferences;
  if (!preferences.begin(FURBLE_STR, false)) {
    return false;
  }
  const size_t written = preferences.put(IDENTITY_KEY, address, IDENTITY_BYTES);
  preferences.end();
  return written == IDENTITY_BYTES;
}

bool restore(bool havePrevious, const uint8_t (&previous)[IDENTITY_BYTES]) {
  const bool restored =
      havePrevious ? Device::applyRandomStaticIdentity(previous) : Device::applyDefaultIdentity();
  if (!restored) {
    ESP_LOGE(FURBLE_STR, "Companion identity rollback failed after a rotation error");
  }
  return restored;
}

}  // namespace

bool applyPersisted(void) {
  uint8_t address[IDENTITY_BYTES] = {};
  if (!load(address)) {
    return false;
  }
  return Device::applyRandomStaticIdentity(address);
}

bool rotate(void) {
  uint8_t previous[IDENTITY_BYTES] = {};
  const bool havePrevious = load(previous);

  uint8_t address[IDENTITY_BYTES] = {};
  if (!Device::generateRandomStaticIdentity(address)) {
    return false;
  }

  // Apply first, then persist. If the controller rejects the new identity, NVS
  // still describes the old one. If persistence fails after a successful
  // apply, restore the old controller identity so the next boot and the live
  // radio agree.
  if (!Device::applyRandomStaticIdentity(address)) {
    ESP_LOGW(FURBLE_STR, "Companion identity apply failed; keeping the persisted identity");
    (void)restore(havePrevious, previous);
    return false;
  }
  if (!store(address)) {
    ESP_LOGW(FURBLE_STR, "Companion identity persist failed; restoring the previous identity");
    (void)restore(havePrevious, previous);
    return false;
  }
  return true;
}

#if defined(FURBLE_HOST_TEST)
void clearPersisted(void) {
  Preferences preferences;
  if (preferences.begin(FURBLE_STR, false)) {
    preferences.remove(IDENTITY_KEY);
    preferences.end();
  }
}
#endif

}  // namespace CompanionIdentity
}  // namespace Furble
