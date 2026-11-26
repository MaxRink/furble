#ifndef SONY_H
#define SONY_H

#include <cstdint>

#include "Camera.h"
#include "Device.h"
#include "protocol/AdvertisementProtocol.h"

namespace Furble {
/**
 * Sony.
 *
 * Two advertisement generations are supported. Upstream issue 316 reported an
 * ILCE-7RM4 (2019) that was invisible to discovery, refused pairing and never
 * received location data; the 2020 and later bodies (the ILCE-7RM5 firmware
 * image) behave differently in three places, all keyed off the advertised
 * generation and the discovered DD00 profile.
 */
class Sony: public Camera {
 public:
  /**
   * Advertisement generation.
   *
   * Unknown is a saved record: the vendor record stores no advertisement and
   * Sony does not scan on reconnect, so the GATT profile decides for those.
   */
  enum class Generation {
    Unknown,
    Current,
    Legacy,
  };

  /**
   * How the location transfer is gated on this body.
   *
   * Strict is the DD30 allow plus DD31 enable handshake the current generation
   * exposes and the 7RM5 firmware confirms. BestEffort writes DD11 directly,
   * which is the only option on a body without the pair.
   */
  enum class GeoGate {
    Strict,
    BestEffort,
  };

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

  /** FOCUS_DOWN and FOCUS_UP exist. Bulb uses the release pair. */
  Capability capabilities(void) const override final {
    return Camera::Capability {true, true, false};
  }

  size_t getSerialisedBytes(void) const override;
  bool serialise(void *buffer, size_t bytes) const override;

  /** Advertisement generation this instance was built from. */
  Generation generation(void) const { return m_Generation; }
  /** Location gate selected from the discovered DD00 profile. */
  GeoGate geoGate(void) const { return m_GeoGate; }

  /**
   * Consecutive refused DD11 writes before a best-effort session stops trying.
   *
   * A refusal can be an application error the body clears by itself, so one
   * failure must not end the session's attempts. A body that lacks the packet
   * format refuses every write, so the attempt still ends well below one write
   * per GPS tick.
   */
  static constexpr uint8_t GEO_FAILURE_LIMIT = 3;

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
    uint8_t zero0[65];    // zero bytes
    uint16_t offset;      // Difference from UTC to the current timezone, minutes
    uint16_t dst_offset;  // DST difference in minutes, zero outside DST
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

  /** ATT write header the DD11 payload is charged for. */
  static constexpr size_t ATT_WRITE_HEADER_BYTES = 3;

  NimBLERemoteCharacteristic *m_Control = nullptr;
  NimBLERemoteService *m_GeoSvc = nullptr;
  Generation m_Generation = Generation::Unknown;
  GeoGate m_GeoGate = GeoGate::Strict;
  /** Set once a session has reported why it cannot deliver a location. */
  bool m_GeoReported = false;
  /** Set when this body refused DD11 without an enable step, so it is not retried. */
  bool m_GeoUnsupported = false;
  /** Consecutive refused DD11 writes in this session, see GEO_FAILURE_LIMIT. */
  uint8_t m_GeoFailures = 0;

  Device::uuid128_t m_Uuid;

  bool _connect(void) override final;
  void _disconnect(void) override final;

  // Check and enable location service
  bool locationEnabled(void);
};  // namespace Furble

}  // namespace Furble
#endif
