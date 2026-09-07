#include <esp_timer.h>

#include "Device.h"
#include "Scan.h"

#include "FurbleCompanion.h"
#include "FurbleGPS.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {

CompanionGatt::CompanionGatt() : m_Service {*this} {
  m_Service.setSettingReloadCallback([this](bool pairingWindow) { reloadSetting(pairingWindow); });
}

CompanionGatt &CompanionGatt::getInstance(void) {
  static CompanionGatt instance;
  return instance;
}

uint64_t CompanionGatt::nowMs(void) {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

void CompanionGatt::init(void) {
  if (Settings::load<Settings::COMPANION>()) {
    enable(false);
  }
}

void CompanionGatt::reloadSetting(bool pairingWindow) {
  if (Settings::load<Settings::COMPANION>()) {
    enable(pairingWindow);
  } else {
    scheduleDisable();
  }
}

void CompanionGatt::reloadPassword(void) {
  m_Service.reloadPassword();
}

bool CompanionGatt::isEnabled(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Enabled;
}

bool CompanionGatt::hasPendingPairing(void) const {
  return m_Service.hasPendingPairing();
}

uint32_t CompanionGatt::getPendingPairingPin(void) const {
  return m_Service.getPendingPairingPin();
}

void CompanionGatt::confirmPairing(bool accept) {
  uint16_t handle = INVALID_CONN_HANDLE;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Service.hasPendingPairing() || (m_Server == nullptr)
        || (m_PendingPairingHandle == INVALID_CONN_HANDLE)) {
      return;
    }
    handle = m_PendingPairingHandle;
    NimBLEConnInfo peer = m_Server->getPeerInfoByHandle(handle);
    NimBLEDevice::injectConfirmPasskey(peer, accept);
    m_Service.confirmPairing(accept);
    m_PendingPairingHandle = INVALID_CONN_HANDLE;
  }

  if (!accept && (m_Server != nullptr)) {
    m_Server->disconnect(handle);
  }
}

void CompanionGatt::enable(bool pairingWindow) {
  if ((m_DisableTimer != nullptr) && esp_timer_is_active(m_DisableTimer)) {
    esp_timer_stop(m_DisableTimer);
  }

  bool startPairing = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Enabled) {
      startPairing = pairingWindow;
    } else {
      m_Enabled = true;
      m_PairingWindow = false;

      // Match Camera::connect() and use numeric comparison for the server.
      NimBLEDevice::setSecurityAuth(true, true, true);
      NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);

      // Scan owns the existing server. This call is the only service-side
      // server creation path, and it is reached only with COMPANION enabled.
      Scan::getInstance();
      m_Server = NimBLEDevice::getServer();
      if (m_Server == nullptr) {
        ESP_LOGE(LOG_TAG, "Companion could not obtain the NimBLE server");
        m_Enabled = false;
        return;
      }

      createGatt();
      m_Server->setCallbacks(this, false);
      m_Server->start();
      m_Advertising = NimBLEDevice::getAdvertising();
      loadBond();
      m_Service.init();

      if (m_DisableTimer == nullptr) {
        const esp_timer_create_args_t args = {
            .callback = delayedDisable,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "companion disable",
            .skip_unhandled_events = false,
        };
        if (esp_timer_create(&args, &m_DisableTimer) != ESP_OK) {
          ESP_LOGE(LOG_TAG, "Failed to create companion disable timer");
        }
      }

      if (m_Task == nullptr) {
        const BaseType_t result =
            xTaskCreate(serviceTaskEntry, "companion", 4096, this, 2, &m_Task);
        if (result != pdPASS) {
          ESP_LOGE(LOG_TAG, "Failed to create companion task");
          m_Task = nullptr;
        }
      }

      startPairing = pairingWindow;
      if (!startPairing && m_BondValid) {
        startReconnectAdvertising();
      }
    }
  }

  if (startPairing) {
    startPairingWindow();
  }
}

void CompanionGatt::disable(void) {
  NimBLEServer *server = nullptr;
  TaskHandle_t task = nullptr;
  esp_timer_handle_t disableTimer = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Enabled) {
      return;
    }
    m_Enabled = false;
    m_PairingWindow = false;
    m_PairingDeadlineMs = 0;
    m_Service.confirmPairing(false);
    m_PendingPairingHandle = INVALID_CONN_HANDLE;
    server = m_Server;
    task = m_Task;
    disableTimer = m_DisableTimer;
  }

  stopAdvertising();
  m_Service.releaseHeldCommands();
  GPS::getInstance().clearExternalFix();
  if ((disableTimer != nullptr) && esp_timer_is_active(disableTimer)) {
    esp_timer_stop(disableTimer);
  }
  if ((server != nullptr) && m_CompanionConnected) {
    server->disconnect(m_CompanionConnHandle);
  }
  m_Service.deinit();

  if (task != nullptr) {
    vTaskDelete(task);
    m_Task = nullptr;
  }

  // Remove the custom attributes when the setting is switched off at runtime.
  // On a normal boot with the default false setting no service is ever created.
  if (server != nullptr) {
    if (m_GattService != nullptr) {
      server->removeService(m_GattService, true);
      m_GattService = nullptr;
    }
    if (m_DeviceInfoService != nullptr) {
      server->removeService(m_DeviceInfoService, true);
      m_DeviceInfoService = nullptr;
    }
    m_Location = nullptr;
    m_Status = nullptr;
    m_Settings = nullptr;
    m_Trigger = nullptr;
    m_Auth = nullptr;
    m_Capability = nullptr;
    m_Firmware = nullptr;
    m_Manufacturer = nullptr;
    m_Server->setCallbacks(nullptr, false);
  }
  m_CompanionConnected = false;
  m_CompanionEncrypted = false;
  m_CompanionAuthenticated = false;
  m_CompanionConnHandle = INVALID_CONN_HANDLE;
}

void CompanionGatt::scheduleDisable(void) {
  if (!m_Enabled) {
    return;
  }
  if (m_DisableTimer == nullptr) {
    ESP_LOGW(LOG_TAG, "Companion disable grace timer is unavailable");
    disable();
    return;
  }
  if (esp_timer_is_active(m_DisableTimer)) {
    esp_timer_stop(m_DisableTimer);
  }
  if (esp_timer_start_once(m_DisableTimer, COMPANION_DISABLE_GRACE_MS * 1000) != ESP_OK) {
    ESP_LOGW(LOG_TAG, "Companion disable grace timer could not start");
    disable();
  }
}

void CompanionGatt::delayedDisable(void *param) {
  auto *companion = static_cast<CompanionGatt *>(param);
  if (!Settings::load<Settings::COMPANION>()) {
    companion->disable();
  }
}

void CompanionGatt::createGatt(void) {
  if (m_GattService != nullptr) {
    return;
  }

  m_GattService = m_Server->createService(SERVICE_UUID);
  m_Location = m_GattService->createCharacteristic(
      LOCATION_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC,
      sizeof(CompanionService::companion_fix_t));
  m_Location->setCallbacks(this);

  m_Status = m_GattService->createCharacteristic(
      STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC,
      sizeof(CompanionService::companion_status_t));
  m_Status->setCallbacks(this);

  m_Settings = m_GattService->createCharacteristic(
      SETTINGS_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::WRITE_AUTHEN, 512);
  m_Settings->setCallbacks(this);

  m_Capability = m_GattService->createCharacteristic(
      CAPABILITY_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC,
      sizeof(companion_capability_t));
  const companion_capability_t capability = {
      CAPABILITY_VERSION,
      WIRE_VERSION,
      FEATURE_SETTINGS_V2,
  };
  m_Capability->setValue(reinterpret_cast<const uint8_t *>(&capability), sizeof(capability));

  m_Trigger = m_GattService->createCharacteristic(
      TRIGGER_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN, 4);
  m_Trigger->setCallbacks(this);

  m_Auth = m_GattService->createCharacteristic(
      AUTH_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::WRITE_AUTHEN,
      CompanionService::AUTH_CHALLENGE_SIZE);
  m_Auth->setCallbacks(this);

  // OTA is intentionally reserved only. The characteristics are deferred.
  m_DeviceInfoService = m_Server->createService("180A");
  m_Firmware = m_DeviceInfoService->createCharacteristic("2A26", NIMBLE_PROPERTY::READ, 64);
  m_Firmware->setValue(FURBLE_VERSION);
  m_Manufacturer = m_DeviceInfoService->createCharacteristic("2A29", NIMBLE_PROPERTY::READ, 64);
  m_Manufacturer->setValue(FURBLE_STR);

  const CompanionService::companion_status_t status = m_Service.getStatus();
  m_Status->setValue(reinterpret_cast<const uint8_t *>(&status), sizeof(status));
}

void CompanionGatt::clearWhitelist(void) {
  while (NimBLEDevice::getWhiteListCount() > 0) {
    NimBLEDevice::whiteListRemove(NimBLEDevice::getWhiteListAddress(0));
  }
}

void CompanionGatt::stopAdvertising(void) {
  if ((m_Advertising != nullptr) && m_Advertising->isAdvertising()) {
    m_Advertising->stop();
  }
}

void CompanionGatt::startPairingWindow(void) {
  if (!m_Enabled || (m_Advertising == nullptr) || (m_Server == nullptr)) {
    return;
  }

  const uint64_t now = nowMs();
  if (!m_PairingWindow || (m_PairingDeadlineMs <= now)) {
    if (!m_BondValid && (NimBLEDevice::getNumBonds() >= static_cast<int>(MAX_BONDS))) {
      ESP_LOGW(LOG_TAG, "Companion pairing refused because all bond slots are in use");
      return;
    }
    if (m_BondValid && (NimBLEDevice::getNumBonds() >= static_cast<int>(MAX_BONDS))) {
      // Free the companion slot before replacing it. Camera bonds are never evicted.
      NimBLEDevice::deleteBond(m_BondAddress);
      m_BondValid = false;
    }
    m_PairingWindow = true;
    m_PairingDeadlineMs = now + PAIRING_WINDOW_MS;
  }

  if (Scan::getInstance().isActive()) {
    ESP_LOGI(LOG_TAG, "Companion pairing deferred while camera scan is active");
    return;
  }

  const uint64_t remaining = m_PairingDeadlineMs - now;
  stopAdvertising();
  clearWhitelist();
  m_Advertising->reset();
  m_Advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);
  m_Advertising->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  m_Advertising->setAdvertisingInterval(160);  // 100 ms in 0.625 ms units
  m_Advertising->setScanFilter(false, false);
  m_Advertising->enableScanResponse(true);
  m_Advertising->setName(Device::getStringID());
  m_Advertising->addServiceUUID(SERVICE_UUID);
  m_Advertising->setAdvertisingCompleteCallback([this](NimBLEAdvertising *) {
    m_PairingWindow = false;
    if (m_Enabled && !m_CompanionConnected && m_BondValid) {
      startReconnectAdvertising();
    }
  });
  m_Advertising->start(static_cast<uint32_t>(remaining));
}

void CompanionGatt::startReconnectAdvertising(void) {
  if (!m_Enabled || m_PairingWindow || m_CompanionConnected || !m_BondValid
      || (m_Advertising == nullptr) || Scan::getInstance().isActive()) {
    return;
  }

  if (!NimBLEDevice::isBonded(m_BondAddress)) {
    m_BondValid = false;
    return;
  }

  stopAdvertising();
  clearWhitelist();
  NimBLEDevice::whiteListAdd(m_BondAddress);
  m_Advertising->reset();
  m_Advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);
  m_Advertising->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  m_Advertising->setAdvertisingInterval(1600);  // 1000 ms in 0.625 ms units
  m_Advertising->setScanFilter(false, true);
  m_Advertising->enableScanResponse(true);
  m_Advertising->setName(Device::getStringID());
  m_Advertising->addServiceUUID(SERVICE_UUID);
  m_Advertising->setAdvertisingCompleteCallback(nullptr);
  m_Advertising->start(0);
}

void CompanionGatt::loadBond(void) {
  m_Prefs.begin(FURBLE_STR, true);
  const std::string address = m_Prefs.get<std::string>("companion_addr", "");
  const uint8_t type = m_Prefs.get<uint8_t>("companion_addr_type", 0);
  m_Prefs.end();

  if (address.empty()) {
    m_BondValid = false;
    return;
  }

  m_BondAddress = NimBLEAddress(address, type);
  m_BondAddressType = type;
  m_BondValid = NimBLEDevice::isBonded(m_BondAddress);
  if (!m_BondValid) {
    m_Prefs.begin(FURBLE_STR, false);
    m_Prefs.remove("companion_addr");
    m_Prefs.remove("companion_addr_type");
    m_Prefs.end();
  }
}

void CompanionGatt::saveBond(const NimBLEAddress &address) {
  m_BondAddress = address;
  m_BondAddressType = address.getType();
  m_BondValid = true;
  m_Prefs.begin(FURBLE_STR, false);
  m_Prefs.put("companion_addr", address.toString());
  m_Prefs.put("companion_addr_type", m_BondAddressType);
  m_Prefs.end();
}

void CompanionGatt::forgetBond(void) {
  if (m_BondValid) {
    NimBLEDevice::deleteBond(m_BondAddress);
  }
  m_BondValid = false;
  m_Prefs.begin(FURBLE_STR, false);
  m_Prefs.remove("companion_addr");
  m_Prefs.remove("companion_addr_type");
  m_Prefs.end();
}

bool CompanionGatt::isCompanionConnection(NimBLEConnInfo &connInfo) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_CompanionConnected && (m_CompanionConnHandle == connInfo.getConnHandle());
}

void CompanionGatt::serviceTaskEntry(void *param) {
  static_cast<CompanionGatt *>(param)->serviceTask();
}

void CompanionGatt::serviceTask(void) {
  while (true) {
    bool enabled = false;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      enabled = m_Enabled;
    }
    if (!enabled) {
      break;
    }

    const uint64_t now = nowMs();
    if (m_PairingWindow && (now >= m_PairingDeadlineMs)) {
      m_PairingWindow = false;
      stopAdvertising();
      if (m_BondValid) {
        startReconnectAdvertising();
      }
    } else if (Scan::getInstance().isActive()) {
      stopAdvertising();
    } else if (m_PairingWindow && !m_CompanionConnected
               && ((m_Advertising == nullptr) || !m_Advertising->isAdvertising())) {
      startPairingWindow();
    } else if (!m_CompanionConnected && !m_PairingWindow && m_BondValid
               && ((m_Advertising == nullptr) || !m_Advertising->isAdvertising())) {
      startReconnectAdvertising();
    }

    m_Service.notifyStatus();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  m_Task = nullptr;
  vTaskDelete(nullptr);
}

void CompanionGatt::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
  (void)server;
  bool accept = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Enabled) {
      return;
    }
    accept = !m_CompanionConnected
             && (m_PairingWindow || (m_BondValid && connInfo.getIdAddress().equals(m_BondAddress)));
    if (accept) {
      m_CompanionConnected = true;
      m_CompanionEncrypted = false;
      m_CompanionAuthenticated = false;
      m_CompanionConnHandle = connInfo.getConnHandle();
    }
  }

  if (!accept) {
    // The server is shared with the rest of the firmware. Unknown incoming
    // links are left alone so enabling the companion cannot reject another
    // server client. Characteristic callbacks gate access by handle.
    return;
  }

  m_Service.onConnected();
  stopAdvertising();
}

void CompanionGatt::onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) {
  (void)server;
  (void)reason;
  bool wasCompanion = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_CompanionConnected && (m_CompanionConnHandle == connInfo.getConnHandle())) {
      m_CompanionConnected = false;
      m_CompanionEncrypted = false;
      m_CompanionAuthenticated = false;
      m_CompanionConnHandle = INVALID_CONN_HANDLE;
      wasCompanion = true;
    }
  }

  if (!wasCompanion) {
    return;
  }

  m_Service.onDisconnected();
  if (!m_Enabled) {
    return;
  }
  if (m_PairingWindow && (nowMs() < m_PairingDeadlineMs)) {
    startPairingWindow();
  } else {
    m_PairingWindow = false;
    startReconnectAdvertising();
  }
}

void CompanionGatt::onConfirmPassKey(NimBLEConnInfo &connInfo, uint32_t pin) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  if (m_CompanionConnected && (m_CompanionConnHandle == connInfo.getConnHandle())) {
    m_PendingPairingHandle = connInfo.getConnHandle();
    m_Service.beginPairing(pin);
  }
}

void CompanionGatt::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
  bool companion = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    companion = m_CompanionConnected && (m_CompanionConnHandle == connInfo.getConnHandle());
  }
  if (!companion) {
    return;
  }

  if (!connInfo.isEncrypted() || !connInfo.isAuthenticated() || !connInfo.isBonded()) {
    ESP_LOGW(LOG_TAG, "Companion authentication failed");
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_CompanionEncrypted = false;
      m_CompanionAuthenticated = false;
    }
    if (m_Server != nullptr) {
      m_Server->disconnect(connInfo.getConnHandle());
    }
    return;
  }

  const NimBLEAddress newAddress = connInfo.getIdAddress();
  if (m_BondValid && !m_BondAddress.equals(newAddress)) {
    NimBLEDevice::deleteBond(m_BondAddress);
  }
  saveBond(newAddress);
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_CompanionEncrypted = true;
    m_CompanionAuthenticated = true;
    m_PairingWindow = false;
  }
  stopAdvertising();
  m_Service.notifyStatus(true);
}

void CompanionGatt::onRead(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) {
  if ((characteristic == m_Status) && isCompanionConnection(connInfo)) {
    m_Service.notifyStatus(true);
  }
}

void CompanionGatt::onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) {
  if (!isCompanionConnection(connInfo)) {
    return;
  }
  const NimBLEAttValue value = characteristic->getValue();
  if (characteristic == m_Location) {
    m_Service.handleLocation(value.data(), value.size());
  } else if (characteristic == m_Auth) {
    m_Service.handleAuth(value.data(), value.size());
  } else if (characteristic == m_Settings) {
    m_Service.handleSettings(value.data(), value.size());
  } else if (characteristic == m_Trigger) {
    m_Service.handleTrigger(value.data(), value.size());
  }
}

void CompanionGatt::onSubscribe(NimBLECharacteristic *characteristic,
                                NimBLEConnInfo &connInfo,
                                uint16_t subValue) {
  (void)characteristic;
  (void)subValue;
  if (isCompanionConnection(connInfo)) {
    m_Service.notifyStatus(true);
  }
}

bool CompanionGatt::isConnected(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_CompanionConnected;
}

bool CompanionGatt::isEncrypted(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_CompanionEncrypted;
}

bool CompanionGatt::isAuthenticated(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_CompanionAuthenticated;
}

uint16_t CompanionGatt::getMaxPayload(void) const {
  return 244;
}

void CompanionGatt::notify(uint8_t charId, const uint8_t *data, size_t len) {
  if (charId != COMPANION_CHAR_STATUS || data == nullptr || m_Status == nullptr || !isConnected()) {
    return;
  }
  m_Status->setValue(data, len);
  m_Status->notify(m_CompanionConnHandle);
}

void CompanionGatt::indicate(uint8_t charId, const uint8_t *data, size_t len) {
  NimBLECharacteristic *characteristic = nullptr;
  if (charId == COMPANION_CHAR_SETTINGS) {
    characteristic = m_Settings;
  } else if (charId == COMPANION_CHAR_AUTH) {
    characteristic = m_Auth;
  }
  if (characteristic == nullptr || data == nullptr || !isConnected()) {
    return;
  }
  characteristic->indicate(data, len, m_CompanionConnHandle);
}

void CompanionGatt::error(uint8_t charId, uint8_t attError) {
  ESP_LOGW(LOG_TAG, "Companion transport error char 0x%02x ATT 0x%02x", charId, attError);
}

void CompanionGatt::disconnect(void) {
  NimBLEServer *server = nullptr;
  uint16_t handle = INVALID_CONN_HANDLE;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    server = m_Server;
    handle = m_CompanionConnHandle;
  }
  if ((server != nullptr) && (handle != INVALID_CONN_HANDLE)) {
    server->disconnect(handle);
  }
}

}  // namespace Furble
