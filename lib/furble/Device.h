#ifndef DEVICE_H
#define DEVICE_H

#include <cstdint>

#include <NimBLEDevice.h>

namespace Furble {

/**
 * Bluetooth transmit power levels furble selects.
 *
 * Device.cpp maps each level to dBm for the controller, so this header does
 * not depend on the ESP-IDF power level enum.
 */
enum class TxPower : uint8_t {
  P3,  // +3 dBm
  P6,  // +6 dBm
  P9,  // +9 dBm
};

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
  static void init(TxPower power);

  /**
   * Map a transmit power level to dBm for the NimBLE API.
   */
  static int8_t powerLevelToDbm(TxPower power);

  /**
   * Apply a random-static LE identity to the controller.
   *
   * The caller owns persistence. The address must have the random-static top
   * bits set; an invalid value is rejected instead of being truncated.
   */
  static bool applyRandomStaticIdentity(const uint8_t address[6]);

  /** Restore the default resolvable-private/public address policy. */
  static bool applyDefaultIdentity(void);

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
