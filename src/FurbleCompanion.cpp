#include <TinyGPS++.h>
#include <esp_timer.h>
#include <lvgl.h>

#include <algorithm>
#include <cstring>

#include "Device.h"
#include "Scan.h"

#include "FurbleCompanion.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"

namespace Furble {

Companion &Companion::getInstance(void) {
  static Companion instance;
  return instance;
}

uint64_t Companion::nowMs(void) {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

void Companion::init(void) {
  if (Settings::load<Settings::COMPANION>()) {
    enable(false);
  }
}

void Companion::reloadSetting(bool pairingWindow) {
  if (Settings::load<Settings::COMPANION>()) {
    enable(pairingWindow);
  } else {
    disable();
  }
}

bool Companion::isEnabled(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Enabled;
}

bool Companion::hasPendingPairing(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_PendingPairing;
}

uint32_t Companion::getPendingPairingPin(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_PendingPairingPin;
}

void Companion::confirmPairing(bool accept) {
  uint16_t handle = INVALID_CONN_HANDLE;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_PendingPairing || (m_Server == nullptr)) {
      return;
    }
    handle = m_PendingPairingHandle;
    NimBLEConnInfo peer = m_Server->getPeerInfoByHandle(handle);
    NimBLEDevice::injectConfirmPasskey(peer, accept);
    m_PendingPairing = false;
    m_PendingPairingHandle = INVALID_CONN_HANDLE;
    m_PendingPairingPin = 0;
  }

  if (!accept && (m_Server != nullptr)) {
    m_Server->disconnect(handle);
  }
}

void Companion::enable(bool pairingWindow) {
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

      if (m_TimedShutterTimer == nullptr) {
        const esp_timer_create_args_t args = {
            .callback = timedShutter,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "companion shutter",
            .skip_unhandled_events = false,
        };
        if (esp_timer_create(&args, &m_TimedShutterTimer) != ESP_OK) {
          ESP_LOGE(LOG_TAG, "Failed to create companion shutter timer");
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

void Companion::disable(void) {
  NimBLEServer *server = nullptr;
  TaskHandle_t task = nullptr;
  esp_timer_handle_t shutterTimer = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Enabled) {
      return;
    }
    m_Enabled = false;
    m_PairingWindow = false;
    m_PairingDeadlineMs = 0;
    m_PendingPairing = false;
    m_PendingPairingHandle = INVALID_CONN_HANDLE;
    m_PendingPairingPin = 0;
    server = m_Server;
    task = m_Task;
    shutterTimer = m_TimedShutterTimer;
  }

  stopAdvertising();
  releaseHeldCommands();
  GPS::getInstance().clearExternalFix();
  if ((server != nullptr) && m_CompanionConnected) {
    server->disconnect(m_CompanionConnHandle);
  }
  if ((shutterTimer != nullptr) && esp_timer_is_active(shutterTimer)) {
    esp_timer_stop(shutterTimer);
  }
  if ((shutterTimer != nullptr)) {
    esp_timer_delete(shutterTimer);
    m_TimedShutterTimer = nullptr;
  }

  if (task != nullptr) {
    vTaskDelete(task);
    m_Task = nullptr;
  }

  // Remove the custom attributes when the setting is switched off at runtime.
  // On a normal boot with the default false setting no service is ever created.
  if (server != nullptr) {
    if (m_Service != nullptr) {
      server->removeService(m_Service, true);
      m_Service = nullptr;
    }
    if (m_DeviceInfoService != nullptr) {
      server->removeService(m_DeviceInfoService, true);
      m_DeviceInfoService = nullptr;
    }
    m_Location = nullptr;
    m_Status = nullptr;
    m_Settings = nullptr;
    m_Trigger = nullptr;
    m_Firmware = nullptr;
    m_Manufacturer = nullptr;
    m_Server->setCallbacks(nullptr, false);
  }
  m_CompanionConnected = false;
  m_CompanionConnHandle = INVALID_CONN_HANDLE;
}

void Companion::createGatt(void) {
  if (m_Service != nullptr) {
    return;
  }

  m_Service = m_Server->createService(SERVICE_UUID);
  m_Location = m_Service->createCharacteristic(
      LOCATION_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC,
      sizeof(companion_fix_t));
  m_Location->setCallbacks(this);

  m_Status = m_Service->createCharacteristic(
      STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC,
      sizeof(companion_status_t));
  m_Status->setCallbacks(this);

  m_Settings = m_Service->createCharacteristic(
      SETTINGS_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::WRITE_AUTHEN, 512);
  m_Settings->setCallbacks(this);

  m_Trigger = m_Service->createCharacteristic(
      TRIGGER_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN, 4);
  m_Trigger->setCallbacks(this);

  // OTA is intentionally reserved only. The characteristics are deferred.
  m_DeviceInfoService = m_Server->createService("180A");
  m_Firmware = m_DeviceInfoService->createCharacteristic("2A26", NIMBLE_PROPERTY::READ, 64);
  m_Firmware->setValue(FURBLE_VERSION);
  m_Manufacturer = m_DeviceInfoService->createCharacteristic("2A29", NIMBLE_PROPERTY::READ, 64);
  m_Manufacturer->setValue(FURBLE_STR);

  const companion_status_t status = getStatus();
  m_Status->setValue(reinterpret_cast<const uint8_t *>(&status), sizeof(status));
}

void Companion::clearWhitelist(void) {
  while (NimBLEDevice::getWhiteListCount() > 0) {
    NimBLEDevice::whiteListRemove(NimBLEDevice::getWhiteListAddress(0));
  }
}

void Companion::stopAdvertising(void) {
  if ((m_Advertising != nullptr) && m_Advertising->isAdvertising()) {
    m_Advertising->stop();
  }
}

void Companion::startPairingWindow(void) {
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

void Companion::startReconnectAdvertising(void) {
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

void Companion::loadBond(void) {
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

void Companion::saveBond(const NimBLEAddress &address) {
  m_BondAddress = address;
  m_BondAddressType = address.getType();
  m_BondValid = true;
  m_Prefs.begin(FURBLE_STR, false);
  m_Prefs.put("companion_addr", address.toString());
  m_Prefs.put("companion_addr_type", m_BondAddressType);
  m_Prefs.end();
}

void Companion::forgetBond(void) {
  if (m_BondValid) {
    NimBLEDevice::deleteBond(m_BondAddress);
  }
  m_BondValid = false;
  m_Prefs.begin(FURBLE_STR, false);
  m_Prefs.remove("companion_addr");
  m_Prefs.remove("companion_addr_type");
  m_Prefs.end();
}

bool Companion::isCompanionConnection(NimBLEConnInfo &connInfo) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_CompanionConnected && (m_CompanionConnHandle == connInfo.getConnHandle());
}

void Companion::serviceTaskEntry(void *param) {
  static_cast<Companion *>(param)->serviceTask();
}

void Companion::serviceTask(void) {
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

    notifyStatus();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  m_Task = nullptr;
  vTaskDelete(nullptr);
}

Companion::companion_status_t Companion::getStatus(void) const {
  companion_status_t status = {};
  status.version = WIRE_VERSION;

  const int32_t batteryLevel = UI::getBatteryLevel();
  status.battery_percent =
      (batteryLevel >= 0 && batteryLevel <= 100) ? static_cast<uint8_t>(batteryLevel) : 255;

  const int32_t batteryVoltage = UI::getBatteryVoltage();
  status.battery_mv = (batteryVoltage >= 0) ? static_cast<uint16_t>(batteryVoltage) : 0xffff;

  const int32_t batteryCurrent = UI::getBatteryCurrent();
  status.battery_ma = static_cast<int16_t>(std::clamp<int32_t>(
      batteryCurrent, static_cast<int32_t>(-32768), static_cast<int32_t>(32767)));
  if (UI::isBatteryCharging()) {
    status.power_flags |= 1 << 0;
  }
  if (UI::getBatteryVBUSVoltage() > 0) {
    status.power_flags |= 1 << 1;
  }

  const auto &control = Control::getInstance();
  status.camera_total = static_cast<uint8_t>(std::min<size_t>(control.getTargetCount(), 255));
  status.camera_connected =
      static_cast<uint8_t>(std::min<size_t>(control.getConnectedTargetCount(), 255));
  switch (control.getState()) {
    case Control::STATE_IDLE:
      status.control_state = 0;
      break;
    case Control::STATE_CONNECT:
      status.control_state = 1;
      break;
    case Control::STATE_CONNECTING:
      status.control_state = 2;
      break;
    case Control::STATE_CONNECT_FAILED:
      status.control_state = 3;
      break;
    case Control::STATE_ACTIVE:
      status.control_state = 4;
      break;
    case Control::STATE_DISCONNECTING:
      status.control_state = 5;
      break;
  }

  const auto &gps = GPS::getInstance();
  status.gps_source = static_cast<uint8_t>(gps.getSource());
  status.gps_satellites = gps.getSatellites();
  status.ivl_state = UI::getIntervalometerState();
  status.ivl_remaining = UI::getIntervalometerRemaining();
  status.uptime_s = static_cast<uint32_t>(nowMs() / 1000);
  return status;
}

void Companion::notifyStatus(bool force) {
  if (!m_CompanionConnected || (m_Status == nullptr)) {
    return;
  }

  const companion_status_t status = getStatus();
  const uint64_t now = nowMs();
  const bool changed =
      !m_HaveLastStatus || (std::memcmp(&status, &m_LastStatus, sizeof(status)) != 0);
  const bool keepalive =
      (m_LastStatusNotificationMs == 0) || ((now - m_LastStatusNotificationMs) >= 30 * 1000);
  const bool rateAllowed =
      (m_LastStatusNotificationMs == 0) || ((now - m_LastStatusNotificationMs) >= 1000);

  m_Status->setValue(reinterpret_cast<const uint8_t *>(&status), sizeof(status));
  if ((force || keepalive || (changed && rateAllowed)) && m_CompanionConnected) {
    m_Status->notify(m_CompanionConnHandle);
    m_LastStatusNotificationMs = now;
    m_LastStatus = status;
    m_HaveLastStatus = true;
  }
}

void Companion::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
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
      m_CompanionConnHandle = connInfo.getConnHandle();
      m_HaveLastStatus = false;
      m_LastStatusNotificationMs = 0;
    }
  }

  if (!accept) {
    // The server is shared with the rest of the firmware. Unknown incoming
    // links are left alone so enabling the companion cannot reject another
    // server client. Characteristic callbacks gate access by handle.
    return;
  }

  stopAdvertising();
}

void Companion::onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) {
  (void)server;
  (void)reason;
  bool wasCompanion = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_CompanionConnected && (m_CompanionConnHandle == connInfo.getConnHandle())) {
      m_CompanionConnected = false;
      m_CompanionConnHandle = INVALID_CONN_HANDLE;
      wasCompanion = true;
    }
  }

  if (!wasCompanion) {
    return;
  }

  releaseHeldCommands();
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

void Companion::onConfirmPassKey(NimBLEConnInfo &connInfo, uint32_t pin) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  if (m_CompanionConnected && (m_CompanionConnHandle == connInfo.getConnHandle())) {
    m_PendingPairing = true;
    m_PendingPairingHandle = connInfo.getConnHandle();
    m_PendingPairingPin = pin;
  }
}

void Companion::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
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
  m_PairingWindow = false;
  stopAdvertising();
  notifyStatus(true);
}

void Companion::onRead(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) {
  if ((characteristic == m_Status) && isCompanionConnection(connInfo)) {
    notifyStatus(true);
  }
}

void Companion::onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) {
  if (!isCompanionConnection(connInfo)) {
    return;
  }
  if (characteristic == m_Location) {
    handleLocation(characteristic->getValue());
  } else if (characteristic == m_Settings) {
    handleSettings(characteristic->getValue(), connInfo);
  } else if (characteristic == m_Trigger) {
    handleTrigger(characteristic->getValue(), connInfo);
  }
}

void Companion::onSubscribe(NimBLECharacteristic *characteristic,
                            NimBLEConnInfo &connInfo,
                            uint16_t subValue) {
  (void)characteristic;
  (void)subValue;
  if (isCompanionConnection(connInfo)) {
    notifyStatus(true);
  }
}

void Companion::handleLocation(const NimBLEAttValue &value) {
  if (value.size() < (offsetof(companion_fix_t, age_ms) + sizeof(uint32_t))) {
    ESP_LOGW(LOG_TAG, "Short companion location write");
    return;
  }

  companion_fix_t packet = {};
  std::memcpy(&packet, value.data(), std::min<size_t>(value.size(), sizeof(packet)));
  if ((packet.version == 0) || (packet.flags & ~(LOCATION_VALID | TIME_VALID | ALTITUDE_VALID))) {
    return;
  }

  GPS::external_fix_t fix = {};
  fix.gps = {
      packet.latitude,
      packet.longitude,
      packet.altitude,
      packet.satellites,
  };
  fix.timesync = {
      packet.year,   packet.month,  packet.day,         packet.hour,
      packet.minute, packet.second, packet.centisecond,
  };
  fix.age_ms = packet.age_ms;
  fix.position_valid = (packet.flags & LOCATION_VALID) != 0;
  fix.time_valid = (packet.flags & TIME_VALID) != 0;
  fix.altitude_valid = (packet.flags & ALTITUDE_VALID) != 0;
  GPS::getInstance().setExternalFix(fix);
}

Companion::setting_type_t Companion::settingType(Settings::type_t type) {
  switch (type) {
    case Settings::GPS:
    case Settings::GPS_NMEA:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::COMPANION:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
      return SETTING_BOOL;
    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TX_POWER:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::SCAN_MODE:
      return SETTING_U8;
    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
      return SETTING_U32;
    case Settings::THEME:
      return SETTING_STRING;
    case Settings::INTERVAL:
      return SETTING_BLOB;
    case Settings::BULB:
    case Settings::TOUCH_CALIBRATION:
      return SETTING_BLOB;
  }
  return SETTING_BLOB;
}

bool Companion::settingValue(Settings::type_t type, std::vector<uint8_t> &value) {
  switch (type) {
    case Settings::GPS:
    case Settings::GPS_NMEA:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::COMPANION:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
    {
      const bool v = Settings::load<bool>(type);
      value.assign(reinterpret_cast<const uint8_t *>(&v),
                   reinterpret_cast<const uint8_t *>(&v) + 1);
      return true;
    }
    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TX_POWER:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::SCAN_MODE:
    {
      const uint8_t v = Settings::load<uint8_t>(type);
      value.assign(1, v);
      return true;
    }
    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
    {
      const uint32_t v = Settings::load<uint32_t>(type);
      value.resize(sizeof(v));
      std::memcpy(value.data(), &v, sizeof(v));
      return true;
    }
    case Settings::THEME:
    {
      const std::string v = Settings::load<std::string>(type);
      value.assign(v.begin(), v.end());
      return value.size() <= 255;
    }
    case Settings::INTERVAL:
    {
      const interval_t v = Settings::load<interval_t>(type);
      value.resize(sizeof(v));
      std::memcpy(value.data(), &v, sizeof(v));
      return true;
    }
    case Settings::BULB:
    case Settings::TOUCH_CALIBRATION:
      return false;
  }
  return false;
}

bool Companion::saveSetting(Settings::type_t type, const uint8_t *value, uint8_t length) {
  const setting_type_t wireType = settingType(type);
  switch (wireType) {
    case SETTING_BOOL:
      if (length != 1 || (value[0] > 1)) {
        return false;
      }
      Settings::save<bool>(type, value[0] != 0);
      return true;
    case SETTING_U8:
      if (length != 1) {
        return false;
      }
      Settings::save<uint8_t>(type, value[0]);
      return true;
    case SETTING_U32:
    {
      if (length != sizeof(uint32_t)) {
        return false;
      }
      uint32_t v;
      std::memcpy(&v, value, sizeof(v));
      Settings::save<uint32_t>(type, v);
      return true;
    }
    case SETTING_STRING:
      Settings::save<std::string>(type, std::string(reinterpret_cast<const char *>(value), length));
      return true;
    case SETTING_BLOB:
      if (type != Settings::INTERVAL || length != sizeof(interval_t)) {
        return false;
      }
      interval_t interval;
      std::memcpy(&interval, value, sizeof(interval));
      Settings::save<interval_t>(type, interval);
      return true;
  }
  return false;
}

bool Companion::settingNeedsRestart(Settings::type_t type) {
  return type == Settings::THEME;
}

void Companion::appendResponse(std::vector<uint8_t> &response,
                               setting_status_t status,
                               uint8_t id,
                               setting_type_t type,
                               uint8_t flags,
                               const std::vector<uint8_t> &value,
                               bool listRecord) {
  if (value.size() > 255) {
    return;
  }
  response.push_back(static_cast<uint8_t>(status));
  response.push_back(id);
  response.push_back(static_cast<uint8_t>(type));
  if (listRecord) {
    response.push_back(flags);
  }
  response.push_back(static_cast<uint8_t>(value.size()));
  response.insert(response.end(), value.begin(), value.end());
}

void Companion::notifySettings(const std::vector<uint8_t> &value, NimBLEConnInfo &connInfo) {
  if ((m_Settings != nullptr) && !value.empty()) {
    m_Settings->indicate(value.data(), value.size(), connInfo.getConnHandle());
  }
}

void Companion::handleSettings(const NimBLEAttValue &value, NimBLEConnInfo &connInfo) {
  if (!connInfo.isEncrypted() || !connInfo.isAuthenticated() || (value.size() < 3)) {
    return;
  }

  const uint8_t *data = value.data();
  const uint8_t op = data[0];
  const uint8_t id = data[1];
  const uint8_t length = data[2];
  const bool lengthMatches = value.size() == (static_cast<size_t>(3) + length);
  const Settings::setting_t *setting = Settings::getByWireId(id);

  if (!lengthMatches || ((op <= 1) && (length != 0)) || (op > 2)) {
    std::vector<uint8_t> response;
    appendResponse(response, SETTING_BAD_LENGTH, id, SETTING_BLOB, 0, {}, false);
    notifySettings(response, connInfo);
    return;
  }

  if (op == 0) {
    std::vector<const Settings::setting_t *> settings;
    for (const auto &it : Settings::all()) {
      if (it.second.wire_id != 0) {
        settings.push_back(&it.second);
      }
    }
    std::sort(settings.begin(), settings.end(),
              [](const auto *left, const auto *right) { return left->wire_id < right->wire_id; });
    for (const auto *entry : settings) {
      std::vector<uint8_t> current;
      if (settingValue(entry->type, current)) {
        std::vector<uint8_t> response;
        appendResponse(response, SETTING_OK, entry->wire_id, settingType(entry->type),
                       settingNeedsRestart(entry->type) ? 1 : 0, current, true);
        notifySettings(response, connInfo);
      }
    }
    std::vector<uint8_t> terminator;
    appendResponse(terminator, SETTING_OK, 0xff, SETTING_BLOB, 0, {}, true);
    notifySettings(terminator, connInfo);
    return;
  }

  if (setting == nullptr) {
    std::vector<uint8_t> response;
    appendResponse(response, SETTING_UNKNOWN_ID, id, SETTING_BLOB, 0, {}, false);
    notifySettings(response, connInfo);
    return;
  }

  const setting_type_t type = settingType(setting->type);
  if (op == 1) {
    std::vector<uint8_t> current;
    const bool valueValid = settingValue(setting->type, current);
    std::vector<uint8_t> response;
    appendResponse(response, valueValid ? SETTING_OK : SETTING_REJECTED, id, type, 0, current,
                   false);
    notifySettings(response, connInfo);
    return;
  }

  const uint8_t expected =
      (type == SETTING_BOOL || type == SETTING_U8)
          ? 1
          : (type == SETTING_U32 ? sizeof(uint32_t)
                                 : (type == SETTING_STRING ? length : sizeof(interval_t)));
  const bool saved = (type == SETTING_STRING || length == expected)
                     && saveSetting(setting->type, data + 3, length);
  std::vector<uint8_t> response;
  appendResponse(response, saved ? SETTING_OK : SETTING_BAD_LENGTH, id, type, 0, {}, false);
  notifySettings(response, connInfo);

  if (!saved) {
    return;
  }

  switch (setting->type) {
    case Settings::GPS:
      GPS::getInstance().reloadSetting();
      break;
    case Settings::TX_POWER:
      Control::getInstance().setPower(Settings::load<esp_power_level_t>(Settings::TX_POWER));
      break;
    case Settings::COMPANION:
      reloadSetting(false);
      break;
    default:
      break;
  }
}

bool Companion::allowTrigger(void) {
  const uint64_t now = nowMs();
  if ((now - m_CommandWindowMs) >= 1000) {
    m_CommandWindowMs = now;
    m_CommandCount = 0;
  }
  if (m_CommandCount >= 10) {
    return false;
  }
  m_CommandCount++;
  return true;
}

void Companion::handleTrigger(const NimBLEAttValue &value, NimBLEConnInfo &connInfo) {
  if (!connInfo.isEncrypted() || !connInfo.isAuthenticated() || (value.size() < 2)) {
    return;
  }
  const uint8_t *data = value.data();
  const uint8_t op = data[1];
  if ((data[0] == 0) || (op > 4) || ((op == 4) && (value.size() != 4))
      || ((op != 4) && (value.size() != 2))) {
    return;
  }
  if (Control::getInstance().getState() != Control::STATE_ACTIVE || !allowTrigger()) {
    return;
  }

  switch (op) {
    case 0:
      if (Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE) == pdTRUE) {
        m_ShutterHeld = false;
      }
      break;
    case 1:
      if (!m_ShutterHeld
          && (Control::getInstance().sendCommand(Control::CMD_SHUTTER_PRESS) == pdTRUE)) {
        m_ShutterHeld = true;
      }
      break;
    case 2:
      if (!m_FocusHeld
          && (Control::getInstance().sendCommand(Control::CMD_FOCUS_PRESS) == pdTRUE)) {
        m_FocusHeld = true;
      }
      break;
    case 3:
      if (Control::getInstance().sendCommand(Control::CMD_FOCUS_RELEASE) == pdTRUE) {
        m_FocusHeld = false;
      }
      break;
    case 4:
    {
      uint16_t holdMs;
      std::memcpy(&holdMs, data + 2, sizeof(holdMs));
      if (m_ShutterHeld
          || (Control::getInstance().sendCommand(Control::CMD_SHUTTER_PRESS) != pdTRUE)) {
        return;
      }
      m_ShutterHeld = true;
      if ((m_TimedShutterTimer == nullptr) || (holdMs == 0)) {
        timedShutter(this);
      } else {
        if (esp_timer_is_active(m_TimedShutterTimer)) {
          esp_timer_stop(m_TimedShutterTimer);
        }
        esp_timer_start_once(m_TimedShutterTimer, static_cast<uint64_t>(holdMs) * 1000);
      }
      break;
    }
    default:
      break;
  }
}

void Companion::releaseHeldCommands(void) {
  if (m_ShutterHeld) {
    Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE);
    m_ShutterHeld = false;
  }
  if (m_FocusHeld) {
    Control::getInstance().sendCommand(Control::CMD_FOCUS_RELEASE);
    m_FocusHeld = false;
  }
}

void Companion::timedShutter(void *param) {
  auto *companion = static_cast<Companion *>(param);
  if (companion->m_ShutterHeld) {
    Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE);
    companion->m_ShutterHeld = false;
  }
}

}  // namespace Furble
