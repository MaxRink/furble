#ifndef FUJIFILM_H
#define FUJIFILM_H

#include <NimBLERemoteCharacteristic.h>

#include <atomic>

#include "Camera.h"

// Bounded wait for the camera-side registration confirmation, in milliseconds.
// The golden X100VI capture (plans/95-engineering-lessons.md) shows the
// confirmation about 12 s into a healthy connect, so the default is generous
// to avoid rejecting a slow but genuine camera. Public and overridable via a
// build flag (-DFURBLE_HOST_REGISTRATION_TIMEOUT_MS=NNN) so host test builds
// keep their short deterministic timeouts and impatient users can tune the
// firmware wait without editing the class.
#ifndef FURBLE_HOST_REGISTRATION_TIMEOUT_MS
#define FURBLE_HOST_REGISTRATION_TIMEOUT_MS 25000
#endif

namespace Furble {
/**
 * Fujifilm X.
 */
class Fujifilm: public Camera {
 public:
  Fujifilm(Type type, const void *data, size_t len) : Camera(type, PairType::SAVED) {};
  Fujifilm(Type type, const NimBLEAdvertisedDevice *pDevice) : Camera(type, PairType::NEW) {};

  /** Is the advertised device a Fujifilm device? */
  static bool matches(const NimBLEAdvertisedDevice *pDevice);

  void shutterPress(void) override final;
  void shutterRelease(void) override final;
  void focusPress(void) override final;
  void focusRelease(void) override final;
  void updateGeoData(const gps_t &gps, const timesync_t &timesync) override final;

 protected:
  /**
   * Advertisement manufacturer data.
   */
  typedef struct __attribute__((packed)) {
    uint16_t company_id;
    uint8_t type;
  } fujifilm_adv_t;

  static constexpr uint16_t COMPANY_ID = 0x04d8;

  // 0x4001
  const NimBLEUUID SVC_PAIR_UUID {0x91f1de68, 0xdff6, 0x466e, 0x8b65ff13b0f16fb8};
  // 0x4042
  const NimBLEUUID CHR_PAIR_UUID {0xaba356eb, 0x9633, 0x4e60, 0xb73ff52516dbd671};
  // 0x4012
  const NimBLEUUID CHR_IDEN_UUID {0x85b9163e, 0x62d1, 0x49ff, 0xa6f5054b4630d4a1};

  // Subscriptions
  const NimBLEUUID SVC_CONF_UUID {0x4c0020fe, 0xf3b6, 0x40de, 0xacc977d129067b14};

  // 0x5013
  const NimBLEUUID CHR_IND1_UUID {0xa68e3f66, 0x0fcc, 0x4395, 0x8d4caa980b5877fa};
  // 0x5023
  const NimBLEUUID CHR_IND2_UUID {0xbd17ba04, 0xb76b, 0x4892, 0xa545b73ba1f74dae};
  // 0x5033
  const NimBLEUUID CHR_NOT1_UUID {0xf9150137, 0x5d40, 0x4801, 0xa8dcf7fc5b01da50};
  const NimBLEUUID CHR_IND3_UUID {0x049ec406, 0xef75, 0x4205, 0xa39008fe209c51f0};

  // Shutter characteristic
  const NimBLEUUID CHR_SHUTTER_UUID {0x7fcf49c6, 0x4ff0, 0x4777, 0xa03d1a79166af7a8};

  // Geo location characteristic
  const NimBLEUUID GEOTAG_UPDATE {0xad06c7b7, 0xf41a, 0x46f4, 0xa29a712055319122};

  // Geolocation sync interval
  const uint16_t GEOTAG_SYNC_INTERVAL = 10;

  void _disconnect(void) override final;
  bool subscribe(const NimBLEUUID &svc,
                 const NimBLEUUID &chr,
                 bool notification,
                 bool response = false);

  /**
   * Wait for the camera to confirm app-level registration.
   *
   * A GATT link and successful ATT operations do not prove that the camera
   * accepted furble as a remote. Fujifilm sends a notification on CHR_NOT1_UUID
   * after accepting the registration. The wait is bounded and runs outside the
   * Control mutex. Direct host camera tests do not set Camera::m_Active, so the
   * cancellation check is enabled only when the connection began active.
   */
  bool waitForRegistration(uint8_t progress, bool cancelOnInactive);

  static constexpr uint32_t REGISTRATION_TIMEOUT_MS = FURBLE_HOST_REGISTRATION_TIMEOUT_MS;
  static constexpr uint32_t REGISTRATION_POLL_MS = 20;

  // Incremented for every connection attempt. Subscription callbacks capture
  // the attempt they belong to, so a queued notification from an old NimBLE
  // client cannot confirm a later reconnect.
  std::atomic<uint32_t> m_RegistrationGeneration {0};
  std::atomic<bool> m_Configured {false};
  NimBLERemoteCharacteristic *m_Shutter = nullptr;

 private:
  // Currently unused
  // const NimBLEUUID SVC_READ_UUID {0x4e941240, 0xd01d, 0x46b9, 0xa5ea67636806830b};
  // const NimBLEUUID CHR_READ_UUID{0xbf6dc9cf, 0x3606, 0x4ec9, 0xa4c8d77576e93ea4};

  const NimBLEUUID SVC_GEOTAG_UUID {0x3b46ec2b, 0x48ba, 0x41fd, 0xb1b8ed860b60d22b};
  const NimBLEUUID CHR_GEOTAG_UUID {0x0f36ec14, 0x29e5, 0x411a, 0xa1b664ee8383f090};

  void notify(NimBLERemoteCharacteristic *, uint8_t *, size_t, bool, uint32_t generation);
  void sendGeoData(const gps_t &gps, const timesync_t &timesync);

  template <std::size_t N>
  void sendShutterCommand(const std::array<uint8_t, N> &cmd, const std::array<uint8_t, N> &param);

  volatile bool m_GeoRequested = false;
};

}  // namespace Furble
#endif
