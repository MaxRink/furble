#ifndef FURBLE_COMPANION_H
#define FURBLE_COMPANION_H

#include <NimBLEAdvertising.h>
#include <NimBLECharacteristic.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEService.h>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "FurbleSettings.h"

namespace Furble {

/**
 * Firmware side of the opt-in companion GATT service.
 *
 * The UUID base was generated with uuidgen and is frozen here. Characteristic
 * UUIDs derive from it by changing the first 32-bit field only.
 */
class Companion: public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
 public:
  static constexpr const char *SERVICE_UUID = "b57f4f5e-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *LOCATION_UUID = "b57f4f5f-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *STATUS_UUID = "b57f4f60-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *SETTINGS_UUID = "b57f4f61-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *TRIGGER_UUID = "b57f4f62-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *OTA_CONTROL_UUID = "b57f4f6d-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *OTA_DATA_UUID = "b57f4f6e-087b-4740-b71d-8262cf26ebbc";

  static constexpr uint8_t WIRE_VERSION = 1;
  static constexpr uint32_t PAIRING_WINDOW_MS = 2 * 60 * 1000;
  static constexpr uint32_t MAX_BONDS = 15;

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

  static_assert(sizeof(companion_fix_t) == 42, "companion fix wire size changed");
  static_assert(sizeof(companion_status_t) == 20, "companion status wire size changed");

  static Companion &getInstance(void);

  Companion(Companion const &) = delete;
  Companion(Companion &&) = delete;
  Companion &operator=(Companion const &) = delete;
  Companion &operator=(Companion &&) = delete;

  /** Initialize the service only when the COMPANION setting is enabled. */
  void init(void);

  /** Refresh the setting. A true value explicitly opens the pairing window. */
  void reloadSetting(bool pairingWindow = false);

  bool isEnabled(void) const;
  bool hasPendingPairing(void) const;
  uint32_t getPendingPairingPin(void) const;
  void confirmPairing(bool accept);

  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;
  void onConfirmPassKey(NimBLEConnInfo &connInfo, uint32_t pin) override;
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;

  void onRead(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override;
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override;
  void onSubscribe(NimBLECharacteristic *characteristic,
                   NimBLEConnInfo &connInfo,
                   uint16_t subValue) override;

 private:
  Companion() = default;

  static constexpr uint16_t INVALID_CONN_HANDLE = BLE_HS_CONN_HANDLE_NONE;
  static constexpr uint8_t LOCATION_VALID = 1 << 0;
  static constexpr uint8_t TIME_VALID = 1 << 1;
  static constexpr uint8_t ALTITUDE_VALID = 1 << 2;

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

  void enable(bool pairingWindow);
  void disable(void);
  void createGatt(void);
  void startPairingWindow(void);
  void startReconnectAdvertising(void);
  void stopAdvertising(void);
  void clearWhitelist(void);
  void serviceTask(void);
  static void serviceTaskEntry(void *param);
  static uint64_t nowMs(void);

  void loadBond(void);
  void saveBond(const NimBLEAddress &address);
  void forgetBond(void);
  bool isCompanionConnection(NimBLEConnInfo &connInfo) const;

  void handleLocation(const NimBLEAttValue &value);
  void handleSettings(const NimBLEAttValue &value, NimBLEConnInfo &connInfo);
  void handleTrigger(const NimBLEAttValue &value, NimBLEConnInfo &connInfo);
  void notifySettings(const std::vector<uint8_t> &value, NimBLEConnInfo &connInfo);
  void notifyStatus(bool force = false);
  companion_status_t getStatus(void) const;
  void releaseHeldCommands(void);
  bool allowTrigger(void);
  static void timedShutter(void *param);

  static setting_type_t settingType(Settings::type_t type);
  static bool settingValue(Settings::type_t type, std::vector<uint8_t> &value);
  static bool saveSetting(Settings::type_t type, const uint8_t *value, uint8_t length);
  static bool settingNeedsRestart(Settings::type_t type);
  static void appendResponse(std::vector<uint8_t> &response,
                             setting_status_t status,
                             uint8_t id,
                             setting_type_t type,
                             uint8_t flags,
                             const std::vector<uint8_t> &value,
                             bool listRecord);

  NimBLEServer *m_Server = nullptr;
  NimBLEService *m_Service = nullptr;
  NimBLEService *m_DeviceInfoService = nullptr;
  NimBLECharacteristic *m_Location = nullptr;
  NimBLECharacteristic *m_Status = nullptr;
  NimBLECharacteristic *m_Settings = nullptr;
  NimBLECharacteristic *m_Trigger = nullptr;
  NimBLECharacteristic *m_Firmware = nullptr;
  NimBLECharacteristic *m_Manufacturer = nullptr;
  NimBLEAdvertising *m_Advertising = nullptr;

  TaskHandle_t m_Task = nullptr;
  esp_timer_handle_t m_TimedShutterTimer = nullptr;

  mutable std::mutex m_Mutex;
  bool m_Enabled = false;
  bool m_PairingWindow = false;
  bool m_CompanionConnected = false;
  uint64_t m_PairingDeadlineMs = 0;
  uint16_t m_CompanionConnHandle = INVALID_CONN_HANDLE;

  bool m_BondValid = false;
  NimBLEAddress m_BondAddress;
  uint8_t m_BondAddressType = 0;

  bool m_PendingPairing = false;
  uint16_t m_PendingPairingHandle = INVALID_CONN_HANDLE;
  uint32_t m_PendingPairingPin = 0;

  companion_status_t m_LastStatus = {};
  bool m_HaveLastStatus = false;
  uint64_t m_LastStatusNotificationMs = 0;

  uint64_t m_CommandWindowMs = 0;
  uint8_t m_CommandCount = 0;
  bool m_ShutterHeld = false;
  bool m_FocusHeld = false;

  Preferences m_Prefs;
};

}  // namespace Furble

#endif
