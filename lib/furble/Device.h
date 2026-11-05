#ifndef DEVICE_H
#define DEVICE_H

#include <cstdint>

#include <NimBLEDevice.h>

namespace Furble {

class Device {
 public:
  static constexpr size_t UUID128_LEN = 16;
  static constexpr size_t UUID128_AS_32_LEN = (UUID128_LEN / sizeof(uint32_t));
  /**
   * UUID type.
   */
  typedef struct _uuid128_t {
    union {
      uint32_t uint32[UUID128_AS_32_LEN];
      uint8_t uint8[UUID128_LEN];
    };
  } uuid128_t;

  /**
   * Initialise the device.
   */
  static void init(esp_power_level_t power);

  /**
   * Map a supported transmit power level to dBm for the NimBLE API.
   */
  static int8_t powerLevelToDbm(esp_power_level_t power);

  /**
   * Apply a random-static LE identity to the controller.
   *
   * The caller owns persistence. The address must have the random-static top
   * bits set; an invalid value is rejected instead of being truncated.
   */
  static bool applyRandomStaticIdentity(const uint8_t address[6]);

  /**
   * Fill a buffer with a valid, non-zero random-static LE identity.
   *
   * This does not touch the controller or NVS.
   */
  static bool generateRandomStaticIdentity(uint8_t address[6]);

  /**
   * Return a device consistent 128-bit UUID.
   */
  static uuid128_t getUUID128(void);

  /**
   * Return pseudo-unique identifier string of this device.
   */
  static const std::string &getStringID(void);

 private:
  static constexpr size_t DEVICE_ID_STR_MAX = 16;

  static uint64_t getEfuseMac(void);

  static uuid128_t m_Uuid;
  static char m_StringID[DEVICE_ID_STR_MAX];
  static std::string m_ID;
};
}  // namespace Furble

#endif
