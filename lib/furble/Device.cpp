#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <esp_random.h>

#include "Device.h"
#include "FurbleTypes.h"

namespace Furble {

namespace {

constexpr size_t IDENTITY_BYTES = 6;

bool validRandomStatic(const uint8_t (&address)[IDENTITY_BYTES]) {
  // A random-static address has the two most-significant bits set and is not
  // all zero.
  if ((address[5] & 0xC0) != 0xC0) {
    return false;
  }
  for (const uint8_t byte : address) {
    if (byte != 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

int8_t Device::powerLevelToDbm(esp_power_level_t power) {
  switch (power) {
    case ESP_PWR_LVL_P3:
      return 3;
    case ESP_PWR_LVL_P6:
      return 6;
    case ESP_PWR_LVL_P9:
      return 9;
    default:
      return 3;
  }
}

Device::uuid128_t Device::m_Uuid;
char Device::m_StringID[DEVICE_ID_STR_MAX];
std::string Device::m_ID;

/**
 * Generate a 32-bit PRNG.
 */
static uint32_t xorshift(uint32_t x) {
  /* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
  x ^= x << 13;
  x ^= x << 17;
  x ^= x << 5;
  return x;
}

uint64_t Device::getEfuseMac(void) {
  uint64_t _chipmacid = 0LL;
  esp_efuse_mac_get_default(reinterpret_cast<uint8_t *>(&_chipmacid));
  return _chipmacid;
}

void Device::init(esp_power_level_t power) {
  uint32_t chip_id = (uint32_t)getEfuseMac();
  for (size_t i = 0; i < UUID128_AS_32_LEN; i++) {
    chip_id = xorshift(chip_id);
    m_Uuid.uint32[i] = chip_id;
  }

  // truncate ID to 5 hex characters (arbitrary, just make it 'nice' to read)
  snprintf(m_StringID, DEVICE_ID_STR_MAX, "%s-%05lx", FURBLE_STR,
           static_cast<unsigned long>(m_Uuid.uint32[0] & 0xFFFFF));

  m_ID = std::string(m_StringID);

  NimBLEDevice::init(m_ID);
  NimBLEDevice::setPower(powerLevelToDbm(power));

  // configure RPA (where possible) and distribute encryption key and ID key (IRK)
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT);
}

bool Device::applyRandomStaticIdentity(const uint8_t address[6]) {
  if (address == nullptr) {
    return false;
  }
  uint8_t copy[IDENTITY_BYTES] = {};
  for (size_t index = 0; index < IDENTITY_BYTES; index++) {
    copy[index] = address[index];
  }
  if (!validRandomStatic(copy)) {
    return false;
  }
  if (!NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)) {
    return false;
  }
  return NimBLEDevice::setOwnAddr(copy);
}

bool Device::generateRandomStaticIdentity(uint8_t address[6]) {
  if (address == nullptr) {
    return false;
  }
  esp_fill_random(address, IDENTITY_BYTES);
  // Random static: top two bits set. A zero address is not a usable identity.
  address[5] = static_cast<uint8_t>((address[5] & 0x3F) | 0xC0);
  uint8_t copy[IDENTITY_BYTES] = {};
  for (size_t index = 0; index < IDENTITY_BYTES; index++) {
    copy[index] = address[index];
  }
  return validRandomStatic(copy);
}

Device::uuid128_t Device::getUUID128(void) {
  return m_Uuid;
}

const std::string &Device::getStringID(void) {
  return m_ID;
}

}  // namespace Furble
