#ifndef FURBLE_COMPANION_SERVICE_H
#define FURBLE_COMPANION_SERVICE_H

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#if defined(FURBLE_HOST_COMPANION_TEST)
// The host transport test replaces the NVS-backed Settings implementation with
// a typed in-memory double. Firmware builds keep the normal Settings header.
#include "CompanionHostSettings.h"
#else
#include "FurbleSettings.h"
#endif

namespace Furble {

class CompanionTransport {
 public:
  virtual ~CompanionTransport() = default;

  virtual bool isConnected(void) const = 0;
  virtual bool isEncrypted(void) const = 0;
  virtual bool isAuthenticated(void) const = 0;
  virtual uint16_t getMaxPayload(void) const = 0;
  virtual void notify(uint8_t charId, const uint8_t *data, size_t len) = 0;
  virtual void indicate(uint8_t charId, const uint8_t *data, size_t len) = 0;
  virtual void error(uint8_t charId, uint8_t attError) = 0;
};

enum companion_char_id_t : uint8_t {
  COMPANION_CHAR_NONE = 0x00,
  COMPANION_CHAR_LOCATION = 0x02,
  COMPANION_CHAR_STATUS = 0x03,
  COMPANION_CHAR_SETTINGS = 0x04,
  COMPANION_CHAR_TRIGGER = 0x05,
  COMPANION_CHAR_CAMERAS = 0x06,
  COMPANION_CHAR_OTA_CONTROL = 0x10,
  COMPANION_CHAR_OTA_DATA = 0x11,
};

/** Transport-independent companion service logic. */
class CompanionService {
 public:
  static constexpr uint8_t WIRE_VERSION = 2;
  static constexpr uint8_t CAPABILITY_VERSION = 1;
  static constexpr uint32_t FEATURE_SETTINGS_V2 = 1U << 0;
  static constexpr uint32_t FEATURE_CAMERAS = 1U << 1;
  static constexpr uint32_t PAIRING_WINDOW_MS = 2 * 60 * 1000;

  /** Operations the companion may write to the cameras characteristic. */
  enum camera_op_t : uint8_t {
    CAMERA_OP_LIST = 0,
    CAMERA_OP_CONNECT = 1,
    CAMERA_OP_DISCONNECT = 2,
    CAMERA_OP_SELECT = 3,
    CAMERA_OP_DESELECT = 4,
  };

  enum camera_status_t : uint8_t {
    CAMERA_OK = 0,
    CAMERA_UNKNOWN_ID = 1,
    CAMERA_REJECTED = 2,
    CAMERA_BUSY = 3,
  };

  /** Wire copy of the plans/25 camera row state. */
  enum camera_state_t : uint8_t {
    CAMERA_IDLE = 0,
    CAMERA_CONNECTING = 1,
    CAMERA_CONNECTED = 2,
    CAMERA_RECONNECTING = 3,
    CAMERA_LOST = 4,
    CAMERA_DISCONNECTING = 5,
  };

  static constexpr uint8_t CAMERA_FLAG_SAVED = 1 << 0;
  static constexpr uint8_t CAMERA_FLAG_SELECTED = 1 << 1;
  static constexpr uint8_t CAMERA_FLAG_TARGET = 1 << 2;
  static constexpr uint8_t CAMERA_FLAG_CONNECTED = 1 << 3;

  /** Unknown connection rssi. Only meaningful while connected. */
  static constexpr int8_t CAMERA_RSSI_UNKNOWN = -128;

  typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t flags;
    uint8_t satellites;
    uint8_t accuracy_m;
    double latitude;
    double longitude;
    double altitude;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t centisecond;
    uint8_t reserved;
    uint32_t age_ms;
    uint8_t reserved_tail;
  } companion_fix_t;

  typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t battery_percent;
    uint16_t battery_mv;
    int16_t battery_ma;
    uint8_t power_flags;
    uint8_t camera_total;
    uint8_t camera_connected;
    uint8_t control_state;
    uint8_t gps_source;
    uint8_t gps_satellites;
    uint8_t ivl_state;
    uint16_t ivl_remaining;
    uint32_t uptime_s;
    uint8_t reserved_tail;
  } companion_status_t;

  typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t wire_version;
    uint32_t features;
  } companion_capability_t;

  /** Fixed head of a camera record. The variable name bytes follow it. */
  typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t camera_id;
    uint8_t cam_type;
    uint8_t flags;
    uint8_t progress;
    int8_t rssi;
    uint8_t state;
    uint8_t name_len;
  } companion_camera_t;

  static_assert(sizeof(companion_fix_t) == 42, "companion fix wire size changed");
  static_assert(sizeof(companion_status_t) == 20, "companion status wire size changed");
  static_assert(sizeof(companion_capability_t) == 6, "companion capability wire size changed");
  static_assert(sizeof(companion_camera_t) == 8, "companion camera wire size changed");

  explicit CompanionService(CompanionTransport &transport);

  CompanionService(CompanionService const &) = delete;
  CompanionService(CompanionService &&) = delete;
  CompanionService &operator=(CompanionService const &) = delete;
  CompanionService &operator=(CompanionService &&) = delete;

  void init(void);
  void deinit(void);

  void onConnected(void);
  void onDisconnected(void);

  void beginPairing(uint32_t pin);
  bool hasPendingPairing(void) const;
  uint32_t getPendingPairingPin(void) const;
  void confirmPairing(bool accept);
  void setSettingReloadCallback(std::function<void(bool)> callback);

  void handleLocation(const uint8_t *data, size_t len);
  void handleSettings(const uint8_t *data, size_t len);
  void handleTrigger(const uint8_t *data, size_t len);
  void handleCameras(const uint8_t *data, size_t len);
  void notifyStatus(bool force = false);
  /** Notify camera records whose state changed since the last batch. */
  void notifyCameras(bool force = false);
  companion_status_t getStatus(void) const;
  /** Capability record served by the read-only capability characteristic. */
  static companion_capability_t getCapability(void);
  void releaseHeldCommands(void);

 private:
  static constexpr uint8_t LOCATION_VALID = 1 << 0;
  static constexpr uint8_t TIME_VALID = 1 << 1;
  static constexpr uint8_t ALTITUDE_VALID = 1 << 2;
  static constexpr uint8_t PACKET_VERSION = 1;
  static constexpr uint8_t SETTING_NEEDS_RESTART = 1 << 0;
  static constexpr uint8_t SETTING_DANGEROUS = 1 << 1;

  /** A cameras request is always {op, camera_id}. */
  static constexpr size_t CAMERA_REQUEST_BYTES = 2;
  /** Longest camera name carried on the wire. Fits one record in one indication. */
  static constexpr size_t CAMERA_NAME_MAX = 64;
  /** Steady-state camera notification rate limit, matching the status packet. */
  static constexpr uint64_t CAMERA_NOTIFY_INTERVAL_MS = 1000;

  enum setting_type_t : uint8_t {
    SETTING_BOOL,
    SETTING_U8,
    SETTING_U32,
    SETTING_STRING,
    SETTING_BLOB,
  };

  enum setting_status_t : uint8_t {
    SETTING_OK,
    SETTING_UNKNOWN_ID,
    SETTING_BAD_LENGTH,
    SETTING_READ_ONLY,
    SETTING_REJECTED,
  };

  /** Camera record plus the name, as sampled from CameraList and Control. */
  struct camera_snapshot_t {
    companion_camera_t record;
    std::string name;
  };

  static uint64_t nowMs(void);
  bool allowTrigger(void);
  void notifySettings(const std::vector<uint8_t> &value);
  static void timedShutter(void *param);

  static std::vector<camera_snapshot_t> getCameraSnapshots(void);
  static std::vector<uint8_t> encodeCameraRecord(const camera_snapshot_t &snapshot);
  void indicateCameraStatus(uint8_t status, uint8_t cameraId);

  static setting_type_t settingType(Settings::type_t type);
  static bool settingValue(Settings::type_t type, std::vector<uint8_t> &value);
  static bool saveSetting(Settings::type_t type, const uint8_t *value, uint8_t length);
  static void appendResponse(std::vector<uint8_t> &response,
                             setting_status_t status,
                             uint8_t id,
                             setting_type_t type,
                             uint8_t flags,
                             const std::vector<uint8_t> &value,
                             bool listRecord);

  CompanionTransport &m_Transport;
  esp_timer_handle_t m_TimedShutterTimer = nullptr;
  std::function<void(bool)> m_SettingReloadCallback;

  mutable std::mutex m_Mutex;
  bool m_PendingPairing = false;
  uint32_t m_PendingPairingPin = 0;

  companion_status_t m_LastStatus = {};
  bool m_HaveLastStatus = false;
  uint64_t m_LastStatusNotificationMs = 0;

  std::vector<camera_snapshot_t> m_LastCameras;
  bool m_HaveLastCameras = false;
  uint64_t m_LastCameraNotificationMs = 0;

  uint64_t m_CommandWindowMs = 0;
  uint8_t m_CommandCount = 0;
  bool m_ShutterHeld = false;
  bool m_FocusHeld = false;
};

}  // namespace Furble

#endif
