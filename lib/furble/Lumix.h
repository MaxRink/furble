#ifndef LUMIX_H
#define LUMIX_H

#include <array>

#include <NimBLERemoteCharacteristic.h>

#include "Camera.h"

namespace Furble {
/**
 * Panasonic LUMIX BLE remote.
 *
 * Reverse engineering is based on S5II and BGH1 HCI snoop logs. The
 * implementation covers the MEI0 session path from those traces. G9II
 * generation XOR login is not implemented.
 *
 * Protocol references:
 * - https://github.com/tobiasbrummer/lux-lat-long-log
 * - https://github.com/Aikhjarto/LUMIX-G9II-Remote-Control
 */
class Lumix: public Camera {
 public:
  Lumix(const void *data, size_t len);
  Lumix(const NimBLEAdvertisedDevice *pDevice);

  /** Determine if the advertised BLE device is a Panasonic LUMIX. */
  static bool matches(const NimBLEAdvertisedDevice *pDevice);

  void shutterPress(void) override final;
  void shutterRelease(void) override final;
  void focusPress(void) override final;
  void focusRelease(void) override final;
  void updateGeoData(const gps_t &gps, const timesync_t &timesync) override final;

  size_t getSerialisedBytes(void) const override final;
  bool serialise(void *buffer, size_t bytes) const override final;

 private:
  typedef struct _lumix_t {
    char name[MAX_NAME]; /** Human readable device name. */
    uint64_t address;    /** Device MAC address. */
    uint8_t type;        /** Address type. */
  } lumix_t;

  /** Panasonic manufacturer-specific advertising data. */
  typedef struct __attribute__((packed)) _adv_t {
    uint16_t company_id; /** Bluetooth SIG company ID. */
    uint8_t flags;       /** State or pairing flags. */
    uint8_t addr[5];     /** Bottom five bytes of the address. */
  } adv_t;

  static constexpr uint16_t ADV_PANASONIC_ID = 0x003a;

  // Session service, advertised by LUMIX cameras.
  static const NimBLEUUID SESSION_SVC_UUID;
  // Session-init MEI0 handshake and controller device name.
  static const NimBLEUUID SESSION_INIT_UUID;
  static const NimBLEUUID DEVICE_NAME_UUID;
  // Remote control service and command characteristic.
  static const NimBLEUUID CONTROL_SVC_UUID;
  static const NimBLEUUID CONTROL_UUID;

  // Location service and clock service.
  static const NimBLEUUID LOCATION_SVC_UUID;
  static const NimBLEUUID LOCATION_CHR_UUID;
  static const NimBLEUUID CLOCK_SVC_UUID;
  static const NimBLEUUID CLOCK_CHR_UUID;

  /** GPS push payload, 16 bytes, little-endian. */
  typedef struct __attribute__((packed)) _geo_t {
    uint32_t gps_time; /** Seconds since 1980-01-06, the GPS epoch. */
    int32_t latitude;  /** Latitude multiplied by 1e7. */
    int32_t longitude; /** Longitude multiplied by 1e7. */
    uint16_t altitude; /** Altitude in metres. */
    uint8_t fix;       /** Fix status, 0x41 means a valid NMEA fix. */
    uint8_t reserved;  /** Constant zero. */
  } geo_t;

  /** Clock-sync payload, 10 bytes, little-endian. */
  typedef struct __attribute__((packed)) _clock_t {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t zero;
    int16_t tz_offset; /** UTC offset in minutes. */
  } clock_t;

  /** Single-byte remote control commands. */
  static constexpr uint8_t CMD_FOCUS_PRESS = 0x01;
  static constexpr uint8_t CMD_FOCUS_RELEASE = 0x02;
  static constexpr uint8_t CMD_SHUTTER_PRESS = 0x04;
  static constexpr uint8_t CMD_SHUTTER_RELEASE = 0x05;

  /** MEI0 session-init payload from the S5II and BGH1 traces. */
  static constexpr std::array<uint8_t, 16> MEI0 = {
      0x4d, 0x45, 0x49, 0x30, 0x01, 0x00, 0x10, 0x00,
      0x80, 0x01, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00,
  };

  NimBLERemoteCharacteristic *m_Control = nullptr;
  NimBLERemoteCharacteristic *m_Location = nullptr;
  NimBLERemoteCharacteristic *m_Clock = nullptr;

  bool _connect(void) override final;
  void _disconnect(void) override final;

  bool writeCommand(uint8_t command);
};
}  // namespace Furble

#endif  // LUMIX_H
