#ifndef FURBLE_COMPANION_H
#define FURBLE_COMPANION_H

#include <NimBLEAdvertising.h>
#include <NimBLECharacteristic.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEService.h>

#include <freertos/FreeRTOS.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "FurbleCompanionService.h"

namespace Furble {

/** NimBLE transport for the companion service. */
class CompanionGatt: public NimBLEServerCallbacks,
                     public NimBLECharacteristicCallbacks,
                     public CompanionTransport {
 public:
  static constexpr const char *SERVICE_UUID = "b57f4f5e-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *LOCATION_UUID = "b57f4f5f-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *STATUS_UUID = "b57f4f60-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *SETTINGS_UUID = "b57f4f61-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *TRIGGER_UUID = "b57f4f62-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *CAMERAS_UUID = "b57f4f63-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *CAPABILITY_UUID = "b57f4f64-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *OTA_CONTROL_UUID = "b57f4f6d-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *OTA_DATA_UUID = "b57f4f6e-087b-4740-b71d-8262cf26ebbc";

  static constexpr uint8_t WIRE_VERSION = CompanionService::WIRE_VERSION;
  static constexpr uint8_t CAPABILITY_VERSION = CompanionService::CAPABILITY_VERSION;
  static constexpr uint32_t FEATURE_SETTINGS_V2 = CompanionService::FEATURE_SETTINGS_V2;
  static constexpr uint32_t FEATURE_CAMERAS = CompanionService::FEATURE_CAMERAS;
  static constexpr uint32_t PAIRING_WINDOW_MS = CompanionService::PAIRING_WINDOW_MS;
  static constexpr uint32_t MAX_BONDS = 15;

  using companion_fix_t = CompanionService::companion_fix_t;
  using companion_status_t = CompanionService::companion_status_t;
  using companion_capability_t = CompanionService::companion_capability_t;

  static CompanionGatt &getInstance(void);

  CompanionGatt(CompanionGatt const &) = delete;
  CompanionGatt(CompanionGatt &&) = delete;
  CompanionGatt &operator=(CompanionGatt const &) = delete;
  CompanionGatt &operator=(CompanionGatt &&) = delete;

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

  bool isConnected(void) const override;
  bool isEncrypted(void) const override;
  bool isAuthenticated(void) const override;
  uint16_t getMaxPayload(void) const override;
  void notify(uint8_t charId, const uint8_t *data, size_t len) override;
  void indicate(uint8_t charId, const uint8_t *data, size_t len) override;
  void error(uint8_t charId, uint8_t attError) override;

 private:
  CompanionGatt();

  static constexpr uint16_t INVALID_CONN_HANDLE = BLE_HS_CONN_HANDLE_NONE;
  static constexpr uint32_t COMPANION_DISABLE_GRACE_MS = 1000;

  void enable(bool pairingWindow);
  void disable(void);
  void scheduleDisable(void);
  static void delayedDisable(void *param);
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

  CompanionService m_Service;

  NimBLEServer *m_Server = nullptr;
  NimBLEService *m_GattService = nullptr;
  NimBLEService *m_DeviceInfoService = nullptr;
  NimBLECharacteristic *m_Location = nullptr;
  NimBLECharacteristic *m_Status = nullptr;
  NimBLECharacteristic *m_Settings = nullptr;
  NimBLECharacteristic *m_Cameras = nullptr;
  NimBLECharacteristic *m_Capability = nullptr;
  NimBLECharacteristic *m_Trigger = nullptr;
  NimBLECharacteristic *m_Firmware = nullptr;
  NimBLECharacteristic *m_Manufacturer = nullptr;
  NimBLEAdvertising *m_Advertising = nullptr;

  TaskHandle_t m_Task = nullptr;
  esp_timer_handle_t m_DisableTimer = nullptr;

  mutable std::mutex m_Mutex;
  bool m_Enabled = false;
  bool m_PairingWindow = false;
  bool m_CompanionConnected = false;
  bool m_CompanionEncrypted = false;
  bool m_CompanionAuthenticated = false;
  uint64_t m_PairingDeadlineMs = 0;
  uint16_t m_CompanionConnHandle = INVALID_CONN_HANDLE;

  bool m_BondValid = false;
  NimBLEAddress m_BondAddress;
  uint8_t m_BondAddressType = 0;

  uint16_t m_PendingPairingHandle = INVALID_CONN_HANDLE;

  Preferences m_Prefs;
};

// Keep the public name used by the existing UI and console source stable.
using Companion = CompanionGatt;

}  // namespace Furble

#endif
