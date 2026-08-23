#ifndef SONY_H
#define SONY_H

#include "Camera.h"
#include "Device.h"

namespace Furble {
/**
 * Sony.
 */
class Sony: public Camera {
 public:
  Sony(const void *data, size_t len);
  Sony(const NimBLEAdvertisedDevice *pDevice);

  /**
   * Determine if the advertised BLE device is a Sony.
   */
  static bool matches(const NimBLEAdvertisedDevice *pDevice);

  void shutterPress(void) override final;
  void shutterRelease(void) override final;
  void focusPress(void) override final;
  void focusRelease(void) override final;
  void updateGeoData(const gps_t &gps, const timesync_t &timesync) override final;

  size_t getSerialisedBytes(void) const override;
  bool serialise(void *buffer, size_t bytes) const override;

 private:
  typedef struct _sony_t {
    char name[MAX_NAME];    /** Human readable device name. */
    uint64_t address;       /** Device MAC address. */
    uint8_t type;           /** Address type. */
    Device::uuid128_t uuid; /** Our UUID. */
  } sony_t;

  typedef struct __attribute__((packed)) {
    uint8_t prefix[11];  // fixed prefix (005d0802fc030000101010)
    int32_t latitude;    // *1e7
    int32_t longitude;   // *1e7
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t zero0[65];  // zero bytes
    uint16_t offset;    // UTC offset in minutes
    uint8_t zero1[2];   // zero bytes
  } sony_geo_t;

  // Primary service
  const NimBLEUUID PRI_SVC_UUID {0x8000cc00, 0xcc00, 0xffff, 0xffffffffffffffff};

  // Control service and characteristic (focus, shutter)
  const NimBLEUUID CTRL_SVC_UUID {0x8000ff00, 0xff00, 0xffff, 0xffffffffffffffff};
  const NimBLEUUID CTRL_CHR_UUID {(uint16_t)0xff01};

  // Location service and characteristic
  const NimBLEUUID GEO_SVC_UUID {0x8000dd00, 0xdd00, 0xffff, 0xffffffffffffffff};
  const NimBLEUUID GEO_CHR_UUID {(uint16_t)0xdd01};
  const NimBLEUUID GEO_UPDATE_UUID {(uint16_t)0xdd11};
  const NimBLEUUID GEO_INFO_CHR_UUID {(uint16_t)0xdd21};
  const NimBLEUUID GEO_ALLOW_CHR_UUID {(uint16_t)0xdd30};
  const NimBLEUUID GEO_ENABLE_CHR_UUID {(uint16_t)0xdd31};

  // Control commands
  const uint16_t FOCUS_UP = 0x0601;
  const uint16_t FOCUS_DOWN = 0x0701;
  const uint16_t SHUTTER_UP = 0x0801;
  const uint16_t SHUTTER_DOWN = 0x0901;

  // Location commands
  const uint8_t LOCATION_ALLOW = 0x01;
  const uint8_t LOCATION_ENABLE = 0x01;

  NimBLERemoteCharacteristic *m_Control = nullptr;
  NimBLERemoteService *m_GeoSvc = nullptr;

  Device::uuid128_t m_Uuid;

  bool _connect(void) override final;
  void _disconnect(void) override final;

  // Check and enable location service
  bool locationEnabled(void);
};  // namespace Furble

}  // namespace Furble
#endif
