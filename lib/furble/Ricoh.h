#ifndef RICOH_H
#define RICOH_H

#include <atomic>

#include <NimBLERemoteCharacteristic.h>

#include "Camera.h"

namespace Furble {
/**
 * Ricoh Imaging BLE remote for the RICOH GR IV series.
 *
 * GR III / IIIx and PENTAX bodies share parts of the Ricoh Imaging protocol,
 * but this implementation does not claim compatibility with them.
 *
 * Shutter control uses the ShootingFlavor + OperationRequest characteristics
 * (single-write capture; no shutter hold). Every capture is gated on a fresh
 * OperationMode read: a GR IV in BLE standby keeps the link up and reports
 * CameraPower ON while OperationMode is BLE_STARTUP, and a capture write in
 * that state cold boots the camera and can wedge its firmware. Focus is
 * unsupported: the documented Focus Mode characteristic configures a mode but
 * does not trigger autofocus, and no separate half-press command has been
 * verified. GPS geotagging uses the shared Ricoh Imaging GPS and
 * location-control characteristics. Pairing uses MITM LE Secure Connections
 * with numeric comparison.
 *
 * Protocol reference: dm-zharov/ricoh-gr-bluetooth-api, Android HCI snoop analysis.
 */
class Ricoh: public Camera {
 public:
  Ricoh(const void *data, size_t len);
  Ricoh(const NimBLEAdvertisedDevice *pDevice);

  static bool matches(const NimBLEAdvertisedDevice *pDevice);

  void shutterPress(void) override final;
  void shutterRelease(void) override final;
  void focusPress(void) override final;
  void focusRelease(void) override final;
  void updateGeoData(const gps_t &gps, const timesync_t &timesync) override final;

  size_t getSerialisedBytes(void) const override final;
  bool serialise(void *buffer, size_t bytes) const override final;

  SecurityMode securityMode() const override final;

 private:
  typedef struct _ricoh_t {
    char name[MAX_NAME];
    uint64_t address;
    uint8_t type;
  } ricoh_t;

  enum class OperationCode : uint8_t {
    NOP = 0,
    START = 1,
    STOP = 2,
  };

  enum class OperationParameter : uint8_t {
    NO_AF = 0,
    AF = 1,
    GREEN_BUTTON = 2,
  };

  enum class ShootingFlavor : uint8_t {
    IMMEDIATE = 0,
    TIMER_2S = 2,
  };

  enum class OperationMode : uint8_t {
    CAPTURE = 0x00,
    PLAYBACK = 0x01,
    BLE_STARTUP = 0x02,
    OTHER = 0x03,
    POWER_OFF_TRANSFER = 0x04,
  };

  static constexpr uint8_t STATE_UNKNOWN = 0xFF;

  // GPS Information payload
  typedef struct __attribute__((packed)) _ricoh_geo_t {
    double latitude;
    double longitude;
    double altitude;
    uint8_t year_lsb;
    uint8_t year_msb;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t centisecond;
  } ricoh_geo_t;

  // Camera Information Service
  static const NimBLEUUID INFO_SVC_UUID;
  static const NimBLEUUID MODEL_CHR_UUID;

  // Camera Service
  static const NimBLEUUID CAMERA_SVC_UUID;
  static const NimBLEUUID POWER_CHR_UUID;
  static const NimBLEUUID OPERATION_MODE_CHR_UUID;

  // Shooting Service
  static const NimBLEUUID SHOOTING_SVC_UUID;
  static const NimBLEUUID SHOOTING_FLAVOR_CHR_UUID;
  static const NimBLEUUID OPERATION_REQUEST_CHR_UUID;
  static const NimBLEUUID CAPTURE_STATUS_CHR_UUID;
  static const NimBLEUUID SELF_TIMER_CHR_UUID;

  // Bluetooth Control Service
  static const NimBLEUUID BT_CONTROL_SVC_UUID;
  static const NimBLEUUID PAIRED_DEVICE_NAME_CHR_UUID;

  // GPS Information Service
  static const NimBLEUUID GPS_SVC_UUID;
  static const NimBLEUUID GPS_INFO_CHR_UUID;

  // Location Control Service
  static const NimBLEUUID LOCATION_CONTROL_SVC_UUID;
  static const NimBLEUUID LOCATION_CONTROL_CHR_UUID;

  NimBLERemoteCharacteristic *m_Power = nullptr;
  NimBLERemoteCharacteristic *m_OperationMode = nullptr;
  NimBLERemoteCharacteristic *m_ShootingFlavor = nullptr;
  NimBLERemoteCharacteristic *m_OperationRequest = nullptr;
  NimBLERemoteCharacteristic *m_CaptureStatus = nullptr;
  NimBLERemoteCharacteristic *m_SelfTimer = nullptr;
  NimBLERemoteCharacteristic *m_PairedDeviceName = nullptr;
  NimBLERemoteCharacteristic *m_GpsInfo = nullptr;
  NimBLERemoteCharacteristic *m_LocationControl = nullptr;

  // Camera state cache seeded by the _connect() state probe and refreshed by
  // notifications. Diagnostic only: the capture gate never trusts this cache
  // because a held connection can hold stale BLE_STARTUP or stale CAPTURE
  // forever. Capture authorization always uses a fresh OperationMode read.
  std::atomic<uint8_t> m_LastPower {STATE_UNKNOWN};
  std::atomic<uint8_t> m_LastOperationMode {STATE_UNKNOWN};

  uint32_t m_LastGpsWriteMs = 0;
  bool m_HasGpsWrite = false;
  gps_t m_LastGps = {};
  timesync_t m_LastTimesync = {};

  bool _connect(void) override final;
  void _disconnect(void) override final;

  void onPassKeyEntry(NimBLEConnInfo &connInfo) override;
  uint32_t onPassKeyDisplay(NimBLEConnInfo &connInfo) override;
  void onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin) override;
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;

  static bool nameMatches(const std::string &name);
  void clearRemoteState(void);
  void logChr(NimBLERemoteCharacteristic *pChr,
              const char *label,
              const char *(*decode)(uint8_t) = nullptr,
              std::atomic<uint8_t> *lastByte = nullptr);
  bool captureAllowed(void);
  bool writeByte(NimBLERemoteCharacteristic *pChr, uint8_t value, const char *label);
  bool writeOperation(OperationCode code, OperationParameter parameter);
  bool subscribeCharacteristic(NimBLERemoteCharacteristic *pChr, const char *label);
  bool setShootingFlavor(ShootingFlavor flavor);
  bool setLocationControl(bool enabled);
};

}  // namespace Furble
#endif
