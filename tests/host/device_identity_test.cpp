// Durable companion controller identity coverage.
//
// Companion reidentify rotates the random-static LE address so a host rebuilds
// its cached GATT handle map. The address must survive a reboot, because the
// host pairs against it after the rotation, while every camera bond in the
// shared NimBLE store must stay untouched.

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Device.h"
#include "FurbleCompanionIdentity.h"
#include "NimBLEDevice.h"
#include "Preferences.h"
#include "advertisement_preferences_stub.h"

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
}

bool randomStatic(const std::array<uint8_t, 6> &address) {
  return (address[5] & 0xC0) == 0xC0;
}

}  // namespace

int main(void) {
  Furble::Host::clearPreferences();
  NimBLEDevice::resetMock();

  const NimBLEAddress cameraBond(0x112233445566ULL, 0);
  NimBLEDevice::setBondedAddress(cameraBond);

  Furble::Device::init(ESP_PWR_LVL_P3);
  check(NimBLEDevice::mockOwnAddressType() == BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT,
        "a wiped device keeps the default address policy");
  check(!Furble::CompanionIdentity::applyPersisted(),
        "a wiped device has no persisted companion identity");

  check(Furble::CompanionIdentity::rotate(), "the identity rotates");
  const auto first = NimBLEDevice::mockOwnAddress();
  check(randomStatic(first), "the rotated address is random static");
  check(NimBLEDevice::mockOwnAddressType() == BLE_OWN_ADDR_RANDOM,
        "the rotated address selects the random-static policy");
  check(NimBLEDevice::isBonded(cameraBond), "the camera bond survives the rotation");
  check(NimBLEDevice::deleteBondCount() == 0, "the rotation does not delete any bond");
  check(Furble::Host::preferencesKeyCount() == 1, "the rotated identity is persisted");
  const std::vector<std::string> keys = Furble::Host::preferenceKeys();
  check(keys.size() == 1 && keys[0] == "companion_bleid",
        "the identity uses the distinct companion_bleid key");
  for (const auto &key : keys) {
    check(key.size() <= 15, "the identity key fits the ESP-IDF NVS 15-character limit");
    check(key != "companion_addr", "the identity key does not collide with the peer bond");
  }

  // The host stub enforces the same NVS key bound as ESP-IDF, so a future
  // regression fails before a device would silently truncate the key.
  {
    Furble::Preferences preferences;
    check(preferences.begin("furble", false), "the stub namespace opens");
    const uint8_t value = 1;
    check(preferences.put("this_key_is_too_long", &value, sizeof(value)) == 0,
          "the host Preferences stub rejects keys longer than 15 characters");
    check(!preferences.isKey("this_key_is_too_long"), "the rejected long key was not stored");
    preferences.end();
  }

  // A boot re-runs Device::init() and then CompanionGatt::init() applies the
  // stored identity before advertising. It must not fall back to the default
  // and strand the bond the host made after the rotation.
  Furble::Device::init(ESP_PWR_LVL_P3);
  check(NimBLEDevice::mockOwnAddressType() == BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT,
        "Device::init starts from the default policy");
  check(Furble::CompanionIdentity::applyPersisted(), "the persisted identity applies");
  check(NimBLEDevice::mockOwnAddress() == first, "the persisted address survives a reboot");
  check(NimBLEDevice::mockOwnAddressType() == BLE_OWN_ADDR_RANDOM,
        "the reboot restores the random-static policy");

  check(Furble::CompanionIdentity::rotate(), "a second rotation succeeds");
  const auto second = NimBLEDevice::mockOwnAddress();
  check(second != first, "the second rotation changes the address");
  Furble::Device::init(ESP_PWR_LVL_P3);
  check(Furble::CompanionIdentity::applyPersisted(), "the second identity applies");
  check(NimBLEDevice::mockOwnAddress() == second,
        "the second address replaces the first across a reboot");
  check(NimBLEDevice::isBonded(cameraBond), "camera bonds remain after a second rotation");

  // An invalid stored record is rejected instead of installing a malformed
  // controller identity.
  const uint8_t invalid[6] = {0, 0, 0, 0, 0, 0};
  check(!Furble::Device::applyRandomStaticIdentity(invalid), "an all-zero identity is rejected");

  // A wiped store reverts to the default policy instead of adopting a stale
  // address from RAM.
  Furble::CompanionIdentity::clearPersisted();
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  check(!Furble::CompanionIdentity::applyPersisted(),
        "a wiped store has no companion identity to apply");
  check(NimBLEDevice::mockOwnAddressType() == BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT,
        "a wiped store returns to the default policy");

  if (g_Failures != 0) {
    std::cerr << "companion identity: " << g_Failures << " FAILED\n";
    return 1;
  }
  std::cout << "companion identity: PASS\n";
  return 0;
}
