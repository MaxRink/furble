#include <esp_timer.h>

#include "CameraList.h"
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

void CompanionGatt::disableNow(void) {
  disable();
}

void CompanionGatt::reloadPassword(void) {
  m_Service.reloadPassword();
}

bool CompanionGatt::isEnabled(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Enabled;
}

bool CompanionGatt::hasBond(void) const {
  const std::lock_guard<std::mutex> lock(m_BondMutex);
  return m_BondValid;
}

bool CompanionGatt::isPairingWindow(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_PairingWindow;
}

bool CompanionGatt::isAdvertising(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return (m_Advertising != nullptr) && m_Advertising->isAdvertising();
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
  bool taskCreationFailed = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Enabled) {
      startPairing = pairingWindow;
    } else {
      m_Enabled = true;
      m_PairingWindow = false;

      // Match Camera::connect() and use numeric comparison for the server.
      // The companion uses an encrypted Just Works bond and authenticates the
      // application with its HMAC password, so it does not request MITM.
      NimBLEDevice::setSecurityAuth(true, false, true);
      NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

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
      m_Server->advertiseOnDisconnect(false);
      if (!m_ServerStarted) {
        m_ServerStarted = m_Server->start();
        if (!m_ServerStarted) {
          ESP_LOGE(LOG_TAG, "Companion GATT server failed to start");
          m_Enabled = false;
          m_ServiceReady = false;
          return;
        }
      }
      ESP_LOGI(LOG_TAG, "Companion handles status=%u settings=%u auth=%u cameras=%u",
               m_Status->getHandle(), m_Settings->getHandle(), m_Auth->getHandle(),
               m_Cameras->getHandle());
      m_Advertising = NimBLEDevice::getAdvertising();
      loadBond();
      m_Service.init();
      m_ServiceReady = true;

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
          taskCreationFailed = true;
        }
      }

      startPairing = pairingWindow;
    }
  }

  // The retained service task is already runnable and owns the reconnect
  // advertising decision. Starting it here as well would race the task on the
  // shared NimBLE advertising object, so the task performs the first reconnect
  // on its next tick.
  if (startPairing) {
    startPairingWindow();
  } else if (taskCreationFailed && hasBond()) {
    // Task creation failed under the lock, so there is no service task to own
    // the first reconnect. Starting it here cannot race a task that does not
    // exist.
    startReconnectAdvertising();
  }
}

void CompanionGatt::disable(void) {
  NimBLEServer *server = nullptr;
  esp_timer_handle_t disableTimer = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Enabled) {
      return;
    }
    m_Enabled = false;
    m_ServiceReady = false;
    ++m_PairingGeneration;
    m_PairingWindow = false;
    m_PairingDeadlineMs = 0;
    m_Service.confirmPairing(false);
    m_PendingPairingHandle = INVALID_CONN_HANDLE;
    server = m_Server;
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

  // The task stays alive and idles while the companion is disabled. Deleting
  // it here freed its 4 KiB internal-heap stack, but the next enable could not
  // find a contiguous block and aborted in xTaskCreate. Keeping the task makes
  // stop/start repeatable on the StickS3, where internal heap is tight.
  //
  // Keep the GATT services and the started server for the next enable. Removing
  // and re-creating them freed a few hundred bytes but the re-registration
  // (ble_gatts_start) could not find a contiguous internal block on the
  // StickS3 and aborted. With NimBLE host allocations in PSRAM the retained
  // services do not compete with the camera tasks for internal heap.
  if (server != nullptr) {
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
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::WRITE_ENC, 512);
  m_Settings->setCallbacks(this);

  m_Capability = m_GattService->createCharacteristic(
      CAPABILITY_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC,
      sizeof(companion_capability_t));
  const companion_capability_t capability = CompanionService::getCapability();
  m_Capability->setValue(reinterpret_cast<const uint8_t *>(&capability), sizeof(capability));

  m_Trigger = m_GattService->createCharacteristic(
      TRIGGER_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC, 4);
  m_Trigger->setCallbacks(this);

  // Command responses and list records are indicated, unsolicited state events
  // are notified. Same split as settings versus status.
  m_Cameras = m_GattService->createCharacteristic(CAMERAS_UUID,
                                                  NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE
                                                      | NIMBLE_PROPERTY::NOTIFY
                                                      | NIMBLE_PROPERTY::WRITE_ENC,
                                                  256);
  m_Cameras->setCallbacks(this);
  m_Auth = m_GattService->createCharacteristic(
      AUTH_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::WRITE_ENC,
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
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisingMutex);
  if ((m_Advertising != nullptr) && m_Advertising->isAdvertising()) {
    m_Advertising->stop();
  }
}

void CompanionGatt::startPairingWindow(bool allowNewWindow) {
  const std::lock_guard<std::recursive_mutex> advertisingLock(m_AdvertisingMutex);
  if (!m_Enabled || (m_Advertising == nullptr) || (m_Server == nullptr)) {
    return;
  }

  const uint64_t now = nowMs();
  bool bondValid = false;
  NimBLEAddress bondAddress;
  {
    const std::lock_guard<std::mutex> bondLock(m_BondMutex);
    bondValid = m_BondValid;
    bondAddress = m_BondAddress;
  }
  bool needWindow = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    needWindow = !m_PairingWindow || (m_PairingDeadlineMs <= now);
  }
  if (needWindow) {
    if (!allowNewWindow) {
      return;
    }
    if (!bondValid && (NimBLEDevice::getNumBonds() >= static_cast<int>(MAX_BONDS))) {
      ESP_LOGW(LOG_TAG, "Companion pairing refused because all bond slots are in use");
      return;
    }
    if (bondValid && (NimBLEDevice::getNumBonds() >= static_cast<int>(MAX_BONDS))) {
      // Free the companion slot before replacing it. Camera bonds are never evicted.
      NimBLEDevice::deleteBond(bondAddress);
      const std::lock_guard<std::mutex> bondLock(m_BondMutex);
      m_BondValid = false;
    }
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_PairingWindow = true;
    m_PairingDeadlineMs = now + PAIRING_WINDOW_MS;
  }

  if (Scan::getInstance().isActive()) {
    ESP_LOGI(LOG_TAG, "Companion pairing deferred while camera scan is active");
    return;
  }

  const uint64_t remaining = m_PairingDeadlineMs - now;
  uint32_t generation = 0;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    generation = ++m_PairingGeneration;
  }
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
  m_Advertising->setAdvertisingCompleteCallback([this, generation](NimBLEAdvertising *) {
    bool current = false;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      current = (generation == m_PairingGeneration);
      if (current) {
        m_PairingWindow = false;
      }
    }
    if (current && m_Enabled && !m_CompanionConnected) {
      startReconnectAdvertising();
    }
  });
  m_Advertising->start(static_cast<uint32_t>(remaining));
}

void CompanionGatt::startReconnectAdvertising(void) {
  const std::lock_guard<std::recursive_mutex> advertisingLock(m_AdvertisingMutex);
  NimBLEAddress bondAddress;
  {
    const std::lock_guard<std::mutex> lock(m_BondMutex);
    if (!m_BondValid) {
      ESP_LOGD(LOG_TAG, "Companion reconnect skipped: no bond");
      return;
    }
    bondAddress = m_BondAddress;
  }
  const bool enabled = isEnabled();
  const bool pairingWindow = isPairingWindow();
  const bool connected = isConnected();
  if (!enabled || pairingWindow || connected || (m_Advertising == nullptr)
      || Scan::getInstance().isActive()) {
    ESP_LOGD(LOG_TAG,
             "Companion reconnect skipped enabled=%d window=%d connected=%d adv=%d scan=%d",
             enabled ? 1 : 0, pairingWindow ? 1 : 0, connected ? 1 : 0,
             m_Advertising != nullptr ? 1 : 0, Scan::getInstance().isActive() ? 1 : 0);
    return;
  }

  if (!NimBLEDevice::isBonded(bondAddress)) {
    ESP_LOGW(LOG_TAG, "Companion reconnect bond %s is no longer in the bond store",
             bondAddress.toString().c_str());
    {
      const std::lock_guard<std::mutex> lock(m_BondMutex);
      m_BondValid = false;
    }
    return;
  }

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    ++m_PairingGeneration;
  }
  stopAdvertising();
  clearWhitelist();
  // Do not filter reconnect advertising by the stored address. macOS presents a
  // resolvable private address and the filter entry can be saved with the wrong
  // address type, which makes the device advertise invisibly; the controller
  // also rejects whitelist updates while the previous link tears down.
  // onConnect() still accepts only the stored identity (or any peer while the
  // pairing window is open), so an unknown central cannot read the protected
  // characteristics.
  m_Advertising->reset();
  m_Advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);
  m_Advertising->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  // Keep reconnect advertising at the same 100 ms cadence as the pairing
  // window. The old 1 s interval was invisible to some macOS scan windows even
  // though the controller reported the advertisement active.
  m_Advertising->setAdvertisingInterval(160);  // 100 ms in 0.625 ms units
  m_Advertising->setScanFilter(false, false);
  m_Advertising->enableScanResponse(true);
  m_Advertising->setName(Device::getStringID());
  m_Advertising->addServiceUUID(SERVICE_UUID);
  m_Advertising->setAdvertisingCompleteCallback(nullptr);
  const bool started = m_Advertising->start(0);
  ESP_LOGI(LOG_TAG, "Companion reconnect advertising bond=%s started=%d",
           bondAddress.toString().c_str(), started ? 1 : 0);
}

void CompanionGatt::loadBond(void) {
  const std::lock_guard<std::mutex> lock(m_BondMutex);
  m_Prefs.begin(FURBLE_STR, true);
  const std::string address = m_Prefs.get<std::string>("companion_addr", "");
  const uint8_t type = m_Prefs.get<uint8_t>("companion_ty", 0);
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
    m_Prefs.remove("companion_ty");
    m_Prefs.end();
  }
}

void CompanionGatt::saveBond(const NimBLEAddress &address) {
  const std::lock_guard<std::mutex> lock(m_BondMutex);
  m_BondAddress = address;
  m_BondAddressType = address.getType();
  m_BondValid = true;
  m_Prefs.begin(FURBLE_STR, false);
  m_Prefs.put("companion_addr", address.toString());
  m_Prefs.put("companion_ty", m_BondAddressType);
  m_Prefs.end();
}

void CompanionGatt::forgetBond(void) {
  const std::lock_guard<std::mutex> lock(m_BondMutex);
  if (m_BondValid) {
    NimBLEDevice::deleteBond(m_BondAddress);
  }
  m_BondValid = false;
  m_Prefs.begin(FURBLE_STR, false);
  m_Prefs.remove("companion_addr");
  m_Prefs.remove("companion_ty");
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
      enabled = m_Enabled && m_ServiceReady;
    }
    if (!enabled) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    const uint64_t now = nowMs();
    enum class AdvertisingAction { NONE, STOP, PAIR, RECONNECT, EXPIRE };
    AdvertisingAction action = AdvertisingAction::NONE;
    {
      const std::lock_guard<std::recursive_mutex> advertisingLock(m_AdvertisingMutex);
      {
        const std::lock_guard<std::mutex> lock(m_Mutex);
        const bool advertising = (m_Advertising != nullptr) && m_Advertising->isAdvertising();
        if (m_PairingWindow && (now >= m_PairingDeadlineMs)) {
          m_PairingWindow = false;
          action = AdvertisingAction::EXPIRE;
        } else if (Scan::getInstance().isActive()) {
          action = AdvertisingAction::STOP;
        } else if (m_PairingWindow && !m_CompanionConnected && !advertising) {
          action = AdvertisingAction::PAIR;
        } else if (!m_CompanionConnected && !m_PairingWindow && hasBond() && !advertising) {
          action = AdvertisingAction::RECONNECT;
        }
      }
      switch (action) {
        case AdvertisingAction::EXPIRE:
          stopAdvertising();
          startReconnectAdvertising();
          break;
        case AdvertisingAction::STOP:
          stopAdvertising();
          break;
        case AdvertisingAction::PAIR:
          startPairingWindow(false);
          break;
        case AdvertisingAction::RECONNECT:
          startReconnectAdvertising();
          break;
        case AdvertisingAction::NONE:
          break;
      }
    }
    bool sendServiceChanged = false;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      if (m_ServiceChangedPending && (now >= m_ServiceChangedAt) && m_CompanionConnected) {
        m_ServiceChangedPending = false;
        sendServiceChanged = true;
      }
    }
    if (sendServiceChanged && (m_Server != nullptr)) {
      // The central has now enabled its Service Changed subscription. Tell it
      // the handle map may have moved since its cached copy, otherwise macOS
      // keeps writing the old CCCD handles after a firmware update.
      m_Server->sendServiceChangedIndication();
      ESP_LOGI(LOG_TAG, "Companion sent Service Changed indication");
    }

    m_Service.notifyStatus();
    m_Service.notifyCameras();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void CompanionGatt::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
  (void)server;
  bool accept = false;
  bool staleBond = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Enabled) {
      return;
    }
    NimBLEAddress bondAddress;
    bool bondValid = false;
    {
      const std::lock_guard<std::mutex> bondLock(m_BondMutex);
      bondValid = m_BondValid;
      bondAddress = m_BondAddress;
    }
    accept = !m_CompanionConnected
             && (m_PairingWindow || (bondValid && connInfo.getIdAddress().equals(bondAddress)));
    ESP_LOGI(LOG_TAG, "Companion connect peer=%s id=%s bond=%s window=%d accept=%d bonds=%d",
             connInfo.getAddress().toString().c_str(), connInfo.getIdAddress().toString().c_str(),
             bondValid ? bondAddress.toString().c_str() : "none", m_PairingWindow ? 1 : 0,
             accept ? 1 : 0, NimBLEDevice::getNumBonds());
    if (accept) {
      // macOS rotates its LE address between sessions on a legacy bond. If the
      // pairing window is open and the stored bond is for another address,
      // drop it so the new link can pair instead of silently failing to
      // encrypt with keys the peer no longer has.
      staleBond = m_PairingWindow && bondValid && !connInfo.getIdAddress().equals(bondAddress);
      m_CompanionConnected = true;
      m_CompanionEncrypted = false;
      m_CompanionAuthenticated = false;
      m_CompanionConnHandle = connInfo.getConnHandle();
    }
  }

  if (!accept) {
    // The NimBLE server is shared with Sony camera name discovery. Do not tear
    // down an unknown inbound link here; onDisconnect restores advertising when
    // it leaves. A hostile central can hold the link, which is the cost of not
    // breaking camera clients.
    return;
  }

  if (staleBond) {
    forgetBond();
  }
  m_Service.onConnected();
  stopAdvertising();

  ESP_LOGI(LOG_TAG, "Companion link accepted handle=%u encrypted=%d bonded=%d",
           static_cast<unsigned>(connInfo.getConnHandle()), connInfo.isEncrypted() ? 1 : 0,
           connInfo.isBonded() ? 1 : 0);

  // A protected read returns ATT "insufficient encryption" but does not itself
  // start pairing. Phones often initiate security on their own; a macOS central
  // waits for the peripheral to ask, so the first capability read would fail
  // forever. Start the security procedure as soon as an accepted companion
  // link is up.
  if (!connInfo.isEncrypted()) {
    // Camera::connect() also writes these globals for its own pairing mode.
    // Re-assert the companion policy immediately before starting security so a
    // camera connect cannot leave the companion on numeric comparison.
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    const bool started = NimBLEDevice::startSecurity(connInfo.getConnHandle());
    ESP_LOGI(LOG_TAG, "Companion security initiate: %s", started ? "started" : "failed");
  }
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
    // A rejected link still stopped the controller's advertising. Without this
    // restart the next connect attempt finds nothing and the companion only
    // comes back after a stop/start or reboot.
    resumeAdvertisingAfterDisconnect();
    return;
  }

  m_Service.onDisconnected();
  resumeAdvertisingAfterDisconnect();
}

void CompanionGatt::resumeAdvertisingAfterDisconnect(void) {
  if (!isEnabled()) {
    return;
  }
  bool pairing = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    pairing = m_PairingWindow && (nowMs() < m_PairingDeadlineMs);
    if (!pairing) {
      m_PairingWindow = false;
    }
  }
  if (pairing) {
    startPairingWindow(false);
  } else {
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
  ESP_LOGI(LOG_TAG, "Companion authentication complete encrypted=%d authenticated=%d bonded=%d",
           connInfo.isEncrypted() ? 1 : 0, connInfo.isAuthenticated() ? 1 : 0,
           connInfo.isBonded() ? 1 : 0);
  bool companion = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    companion = m_CompanionConnected && (m_CompanionConnHandle == connInfo.getConnHandle());
  }
  if (!companion) {
    return;
  }

  // Link encryption is the transport requirement. The companion service has
  // its own HMAC challenge/response for application authentication, so a
  // Just Works bond is accepted when the platform cannot present a numeric
  // comparison prompt (macOS from a terminal or background session).
  if (!connInfo.isEncrypted() || !connInfo.isBonded()) {
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
  NimBLEAddress oldBond;
  bool hadBond = false;
  {
    const std::lock_guard<std::mutex> bondLock(m_BondMutex);
    hadBond = m_BondValid;
    oldBond = m_BondAddress;
  }
  if (hadBond && !oldBond.equals(newAddress)) {
    NimBLEDevice::deleteBond(oldBond);
  }
  saveBond(newAddress);
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_CompanionEncrypted = true;
    m_CompanionAuthenticated = true;
    m_PairingWindow = false;
    // The central subscribes to the standard Service Changed characteristic a
    // few milliseconds after this callback. Sending now would race that
    // subscription and be dropped; the service task sends it one second later.
    m_ServiceChangedPending = true;
    m_ServiceChangedAt = nowMs() + 1000;
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
    ESP_LOGI(LOG_TAG, "Companion auth write len=%u", static_cast<unsigned>(value.size()));
    m_Service.handleAuth(value.data(), value.size());
  } else if (characteristic == m_Settings) {
    m_Service.handleSettings(value.data(), value.size());
  } else if (characteristic == m_Trigger) {
    m_Service.handleTrigger(value.data(), value.size());
  } else if (characteristic == m_Cameras) {
    m_Service.handleCameras(value.data(), value.size());
  }
}

void CompanionGatt::onSubscribe(NimBLECharacteristic *characteristic,
                                NimBLEConnInfo &connInfo,
                                uint16_t subValue) {
  (void)subValue;
  if (!isCompanionConnection(connInfo)) {
    return;
  }
  if (characteristic == m_Cameras) {
    m_Service.notifyCameras(true);
  } else {
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
  if (charId == COMPANION_CHAR_CAMERAS && data != nullptr && m_Cameras != nullptr
      && isConnected()) {
    m_Cameras->setValue(data, len);
    m_Cameras->notify(m_CompanionConnHandle);
    return;
  }
  if (charId != COMPANION_CHAR_STATUS || data == nullptr || m_Status == nullptr || !isConnected()) {
    return;
  }
  m_Status->setValue(data, len);
  m_Status->notify(m_CompanionConnHandle);
}

void CompanionGatt::indicate(uint8_t charId, const uint8_t *data, size_t len) {
  if (charId == COMPANION_CHAR_CAMERAS && data != nullptr && m_Cameras != nullptr
      && isConnected()) {
    m_Cameras->indicate(data, len, m_CompanionConnHandle);
    return;
  }
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
