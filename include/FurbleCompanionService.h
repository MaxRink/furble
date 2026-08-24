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

#include "FurbleCompanionAuth.h"
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
  virtual void disconnect(void) {}
};

enum companion_char_id_t : uint8_t {
  COMPANION_CHAR_NONE = 0x00,
  COMPANION_CHAR_LOCATION = 0x02,
  COMPANION_CHAR_STATUS = 0x03,
  COMPANION_CHAR_SETTINGS = 0x04,
  COMPANION_CHAR_TRIGGER = 0x05,
  COMPANION_CHAR_AUTH = 0x06,
  COMPANION_CHAR_OTA_CONTROL = 0x10,
  COMPANION_CHAR_OTA_DATA = 0x11,
};

/** Transport-independent companion service logic. */
class CompanionService {
 public:
  static constexpr uint8_t WIRE_VERSION = 2;
  static constexpr uint8_t CAPABILITY_VERSION = 1;
  static constexpr uint32_t FEATURE_SETTINGS_V2 = 1U << 0;
  static constexpr uint32_t PAIRING_WINDOW_MS = 2 * 60 * 1000;
  static constexpr uint8_t AUTH_BEGIN = 0x01;
  static constexpr uint8_t AUTH_RESULT_AUTHENTICATED = 0x01;
  static constexpr uint8_t AUTH_RESULT_REJECTED = 0x02;
  static constexpr uint8_t AUTH_RESULT_DROPPED = 0x03;
  static constexpr uint8_t AUTH_RESULT_NOT_REQUIRED = 0x04;
  static constexpr uint8_t AUTH_ATT_ERROR = 0x80;
  static constexpr size_t COMPANION_PASSWORD_MAX = 63;

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

  static_assert(sizeof(companion_fix_t) == 42, "companion fix wire size changed");
  static_assert(sizeof(companion_status_t) == 20, "companion status wire size changed");
  static_assert(sizeof(companion_capability_t) == 6, "companion capability wire size changed");

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
  void handleAuth(const uint8_t *data, size_t len);
  void handleSettings(const uint8_t *data, size_t len);
  void handleTrigger(const uint8_t *data, size_t len);
  void reloadPassword(void);
  bool isPasswordAuthenticated(void) const;
  void notifyStatus(bool force = false);
  companion_status_t getStatus(void) const;
  void releaseHeldCommands(void);

 private:
  static constexpr uint8_t LOCATION_VALID = 1 << 0;
  static constexpr uint8_t TIME_VALID = 1 << 1;
  static constexpr uint8_t ALTITUDE_VALID = 1 << 2;
  static constexpr uint8_t PACKET_VERSION = 1;
  static constexpr uint8_t SETTING_NEEDS_RESTART = 1 << 0;
  static constexpr uint8_t SETTING_DANGEROUS = 1 << 1;

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

  static uint64_t nowMs(void);
  bool allowProtected(uint8_t charId) const;
  bool allowTrigger(void);
  void notifySettings(const std::vector<uint8_t> &value);
  static void timedShutter(void *param);

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
  CompanionAuth m_Auth;
  // NimBLE may invoke a disconnect callback while a characteristic callback
  // is still draining. Keep the connection-local auth state serialized across
  // those callbacks without holding the lock while calling transport code.
  mutable std::mutex m_AuthMutex;
  esp_timer_handle_t m_TimedShutterTimer = nullptr;
  std::function<void(bool)> m_SettingReloadCallback;

  mutable std::mutex m_Mutex;
  bool m_PendingPairing = false;
  uint32_t m_PendingPairingPin = 0;

  companion_status_t m_LastStatus = {};
  bool m_HaveLastStatus = false;
  uint64_t m_LastStatusNotificationMs = 0;

  uint64_t m_CommandWindowMs = 0;
  uint8_t m_CommandCount = 0;
  bool m_ShutterHeld = false;
  bool m_FocusHeld = false;
};

}  // namespace Furble

#endif
