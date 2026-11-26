#ifndef RICOH_H
#define RICOH_H

#include <atomic>

#include <NimBLERemoteCharacteristic.h>

#include "Camera.h"

// Wake sequence budget for a GR IV in BLE standby, in milliseconds.
//
// No source documents how long a GR IV needs after the link is re-established
// before OperationMode reports CAPTURE, and the 2026-08-28 bench session did not
// measure it. These are design bounds, kept as build flags so host tests use
// short deterministic waits instead of seconds, and so the numbers can be
// retuned from bench data without touching the class:
//
// - FURBLE_RICOH_WAKE_CONNECT_TIMEOUT_MS: the reconnect's own connect timeout.
//   The peer is already bonded and was connected moments ago, so this is far
//   below the 30 s Control uses for a cold connect.
// - FURBLE_RICOH_WAKE_SETTLE_MS: how long the link stays down, so the camera
//   sees a real disconnect before the new connection.
// - FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS: the whole bounded wait for a fresh
//   OperationMode read of CAPTURE after the link is back.
#ifndef FURBLE_RICOH_WAKE_CONNECT_TIMEOUT_MS
#define FURBLE_RICOH_WAKE_CONNECT_TIMEOUT_MS 5000
#endif
#ifndef FURBLE_RICOH_WAKE_SETTLE_MS
#define FURBLE_RICOH_WAKE_SETTLE_MS 200
#endif
#ifndef FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS
#define FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS 2000
#endif

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
 * that state cold boots the camera and can wedge its firmware. A capture
 * request in BLE_STARTUP runs a bounded wake state machine instead of a bare
 * refusal: drop the link, re-establish it, then poll a fresh OperationMode read
 * for CAPTURE. Only that confirming read authorizes the capture writes; a wake
 * that does not confirm inside the budget writes nothing. Focus is
 * unsupported: the documented Focus Mode characteristic configures a mode but
 * does not trigger autofocus, and no separate half-press command has been
 * verified. GPS geotagging enables the Camera Service GEO Tag characteristic
 * and writes the GPS Information packet. Pairing uses MITM LE Secure
 * Connections.
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

  /**
   * No focus primitive: Focus Mode configures a mode but does not trigger a
   * half press. No bulb: OperationRequest has no stop primitive, so a release
   * cannot end a held exposure. No movie: the Capture Mode flow is not
   * implemented and no payload is proven.
   */
  Capability capabilities(void) const override final {
    return Camera::Capability {false, false, false};
  }

  /** Raw byte 0 of the last Capture Status notification, 0xFF before any. */
  uint8_t captureStatus(void) const;

  /** True while the camera reports a capture in progress. */
  bool captureInProgress(void) const;

  /** Completed 1 to 0 capture transitions observed on Capture Status. */
  uint32_t capturesCompleted(void) const;

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

  // Official name: Drive Mode. Values 0, 1, and 2 are single frame, single
  // frame with a 10 second self-timer, and single frame with a 2 second
  // self-timer. Furble only uses the immediate and 2 second values.
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
    // WGS84 datum selector. The official apps write 0. Firmware does not read
    // this byte. This is not a centisecond field; sub-second time is not
    // carried in this packet.
    uint8_t datum;
  } ricoh_geo_t;

  // Camera Information Service
  static const NimBLEUUID INFO_SVC_UUID;
  static const NimBLEUUID MODEL_CHR_UUID;

  // Camera Service
  static const NimBLEUUID CAMERA_SVC_UUID;
  static const NimBLEUUID POWER_CHR_UUID;
  static const NimBLEUUID OPERATION_MODE_CHR_UUID;
  static const NimBLEUUID DATE_TIME_CHR_UUID;
  // Camera Service GEO Tag enable. This is the location enable write.
  static const NimBLEUUID GEOTAG_ENABLE_CHR_UUID;

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

  NimBLERemoteCharacteristic *m_Power = nullptr;
  NimBLERemoteCharacteristic *m_OperationMode = nullptr;
  NimBLERemoteCharacteristic *m_DateTime = nullptr;
  NimBLERemoteCharacteristic *m_ShootingFlavor = nullptr;
  NimBLERemoteCharacteristic *m_OperationRequest = nullptr;
  NimBLERemoteCharacteristic *m_CaptureStatus = nullptr;
  NimBLERemoteCharacteristic *m_SelfTimer = nullptr;
  NimBLERemoteCharacteristic *m_PairedDeviceName = nullptr;
  NimBLERemoteCharacteristic *m_GpsInfo = nullptr;
  NimBLERemoteCharacteristic *m_GeotagEnable = nullptr;

  // Camera state cache seeded by the _connect() state probe and refreshed by
  // notifications. Diagnostic only: the capture gate never trusts this cache
  // because a held connection can hold stale BLE_STARTUP or stale CAPTURE
  // forever. Capture authorization always uses a fresh OperationMode read.
  std::atomic<uint8_t> m_LastPower {STATE_UNKNOWN};
  std::atomic<uint8_t> m_LastOperationMode {STATE_UNKNOWN};
  // Capture Status byte 0. 1 means a capture is in progress; the 1 to 0
  // transition is the shot-confirmation signal. Each failed property read
  // substitutes 0xFF, which is never treated as a capture or a completion.
  std::atomic<uint8_t> m_CaptureStatusValue {STATE_UNKNOWN};
  std::atomic<uint32_t> m_CapturesCompleted {0};

  uint32_t m_LastGpsWriteMs = 0;
  bool m_HasGpsWrite = false;
  gps_t m_LastGps = {};
  timesync_t m_LastTimesync = {};
  timesync_t m_LastClock = {};
  bool m_HasClockWrite = false;

  bool _connect(void) override final;
  void _disconnect(void) override final;

  static bool nameMatches(const std::string &name);
  void clearRemoteState(void);
  void logChr(NimBLERemoteCharacteristic *pChr,
              const char *label,
              const char *(*decode)(uint8_t) = nullptr,
              std::atomic<uint8_t> *lastByte = nullptr);
  /**
   * Outcome of the pre-capture OperationMode gate.
   *
   * AUTHORIZED means a fresh read just reported CAPTURE, or the body has no
   * OperationMode characteristic at all. ASLEEP means BLE_STARTUP, which is the
   * only state the wake path may act on. REFUSED covers every other reading, a
   * failed read, and a dead link.
   */
  enum class GateResult : uint8_t {
    AUTHORIZED = 0,
    ASLEEP = 1,
    REFUSED = 2,
  };

  /** Outcome of reading OperationMode. */
  enum class ModeRead : uint8_t {
    VALUE = 0,
    UNAVAILABLE = 1,
    FAILED = 2,
  };

  // Wake state machine. IDLE -> RELINK -> PROBE, and PROBE ends in either AWAKE
  // or FAILED. Only AWAKE authorizes a capture, and FAILED writes nothing.
  enum class WakeState : uint8_t {
    IDLE = 0,
    RELINK = 1,
    PROBE = 2,
    AWAKE = 3,
    FAILED = 4,
  };

  static const char *wakeStateName(WakeState state);
  ModeRead readOperationMode(uint8_t &mode);
  GateResult captureGate(uint8_t &mode);
  bool wakeFromStandby(uint8_t asleepMode);
  bool wakePause(uint32_t milliseconds);
  bool writeByte(NimBLERemoteCharacteristic *pChr, uint8_t value, const char *label);
  bool writeOperation(OperationCode code, OperationParameter parameter);
  bool subscribeCharacteristic(NimBLERemoteCharacteristic *pChr, const char *label);
  bool setShootingFlavor(ShootingFlavor flavor);
  bool setGeotagEnable(bool enabled);
};

}  // namespace Furble
#endif
