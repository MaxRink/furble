#include "FurbleCompanionIdentity.h"

#include <Preferences.h>

#include "Device.h"
#include "FurbleTypes.h"

namespace Furble {
namespace CompanionIdentity {

namespace {

constexpr const char *IDENTITY_KEY = "companion_own_addr";
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

}  // namespace

bool applyPersisted(void) {
  uint8_t address[IDENTITY_BYTES] = {};
  if (!load(address)) {
    return false;
  }
  return Device::applyRandomStaticIdentity(address);
}

bool rotate(void) {
  uint8_t address[IDENTITY_BYTES] = {};
  if (!Device::generateRandomStaticIdentity(address)) {
    return false;
  }
  // Persist before applying: a reboot between the two steps then restores the
  // identity the host is about to pair with, instead of the old address.
  if (!store(address)) {
    return false;
  }
  return Device::applyRandomStaticIdentity(address);
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
