#include <NimBLEAddress.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEDevice.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLERemoteService.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "Device.h"
#include "FujifilmSecure.h"
#include "Scan.h"
#include "protocol/FujifilmProtocol.h"

namespace Furble {

const NimBLEUUID FujifilmSecure::SERVICE_UUID {0xa9d2b304, 0xe8d6, 0x4902, 0x8336352b772d7597};
const NimBLEUUID FujifilmSecure::PRI_SVC_UUID {0x731893f9, 0x744e, 0x4899, 0xb7e3174106ff2b82};

/**
 * Determine if the advertised BLE device is a Fujifilm secure camera.
 */
bool FujifilmSecure::matches(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice == nullptr || !pDevice->haveManufacturerData()) {
    return false;
  }

  const auto manufacturerData = pDevice->getManufacturerData();
  return FujifilmProtocol::matchesSecureAdvertisement(
      reinterpret_cast<const uint8_t *>(manufacturerData.data()), manufacturerData.length(),
      pDevice->isAdvertisingService(SERVICE_UUID));
}

FujifilmSecure::FujifilmSecure(const void *data, size_t len)
    : Fujifilm(Type::FUJIFILM_SECURE, data, len) {
  if (len != sizeof(nvs_t))
    abort();

  const nvs_t *fujifilm = static_cast<const nvs_t *>(data);
  m_Name = std::string(fujifilm->name);
  m_Address = NimBLEAddress(fujifilm->address, fujifilm->type);
  m_Serial = fujifilm->serial;
  m_Queue = xQueueCreate(3, sizeof(bool));
}

FujifilmSecure::FujifilmSecure(const NimBLEAdvertisedDevice *pDevice)
    : Fujifilm(Type::FUJIFILM_SECURE, pDevice) {
  const auto manufacturerData = pDevice->getManufacturerData();
  FujifilmProtocol::SecureAdvertisement advertisement;
  const bool parsed = FujifilmProtocol::parseSecureAdvertisement(
      reinterpret_cast<const uint8_t *>(manufacturerData.data()), manufacturerData.length(),
      advertisement);
  m_Name = pDevice->getName();
  m_Address = pDevice->getAddress();
  if (parsed) {
    std::memcpy(m_Serial.data, advertisement.serial.data(), advertisement.serial.size());
  }
  m_Queue = xQueueCreate(3, sizeof(bool));

  ESP_LOGI(LOG_TAG, "Name = %s", m_Name.c_str());
  ESP_LOGI(LOG_TAG, "Address = %s", m_Address.toString().c_str());
  ESP_LOGI(LOG_TAG, "Serial = %s",
           NimBLEUtils::dataToHexString(m_Serial.data, sizeof(m_Serial)).c_str());
}

FujifilmSecure::~FujifilmSecure(void) {
  vQueueDelete(m_Queue);
}

void FujifilmSecure::onResult(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice->haveManufacturerData()) {
    const auto manufacturerData = pDevice->getManufacturerData();
    FujifilmProtocol::SecureAdvertisement advertisement;
    if (FujifilmProtocol::parseSecureAdvertisement(
            reinterpret_cast<const uint8_t *>(manufacturerData.data()), manufacturerData.length(),
            advertisement)
        && pDevice->isAdvertisingService(PAIR_SVC_UUID)) {
      ESP_LOGD(
          LOG_TAG, "got %s, want %s",
          NimBLEUtils::dataToHexString(advertisement.serial.data(), advertisement.serial.size())
              .c_str(),
          NimBLEUtils::dataToHexString(m_Serial.data, sizeof(m_Serial.data)).c_str());
      if (memcmp(advertisement.serial.data(), m_Serial.data, sizeof(m_Serial.data)) == 0) {
        bool success = true;
        xQueueSend(m_Queue, &success, 0);
      }
    }
  }
}

/**
 * Connect to a Fujifilm secure.
 */
bool FujifilmSecure::_connect(void) {
  bool success = false;
  m_Progress = 0;
  const auto registrationAlive = [this]() {
    if (!isConnected()) {
      ESP_LOGW(LOG_TAG, "Fujifilm Secure registration aborted after link loss");
      return false;
    }
    return true;
  };

  if (m_PairType == PairType::SAVED || m_Paired) {
    ESP_LOGI(LOG_TAG, "Scanning");
    // need to scan for advertising camera
    auto &scan = Scan::getInstance();
    // Results belong to a logical scan. Do not let an earlier advertisement
    // satisfy this reconnect before the new scan has observed the camera.
    xQueueReset(m_Queue);
    scan.clear();
    scan.start(this, SCAN_TIME_MS);
    m_Progress += 5;

    // wait up to 60s for camera to appear
    BaseType_t timeout = pdFALSE;
    do {
      timeout = xQueueReceive(m_Queue, &success, pdMS_TO_TICKS(1000));
    } while (scan.isActive() && !success);
    scan.stop();

    if (timeout == pdFALSE) {
      ESP_LOGI(LOG_TAG, "Timeout waiting for camera");
      return false;
    }

    if (!success) {
      ESP_LOGI(LOG_TAG, "Failed to scan paired camera");
      return false;
    }
  }

  ESP_LOGI(LOG_TAG, "Connecting to %s", m_Address.toString().c_str());
  if (!m_Client->connect(m_Address))
    return false;

  ESP_LOGI(LOG_TAG, "Connected");
  m_Progress += 5;

  ESP_LOGI(LOG_TAG, "Securing");
  if (!m_Client->secureConnection()) {
    return false;
  }
  if (!registrationAlive())
    return false;
  ESP_LOGI(LOG_TAG, "Secured!");
  m_Progress += 5;

  ESP_LOGI(LOG_TAG, "Requesting status");
  NimBLEAttValue status;
  gattRead(PAIR_SVC_UUID, STATUS_CHR_UUID, status);
  if (!registrationAlive())
    return false;
  if (status.size() == 4) {
    ESP_LOGI(LOG_TAG, "Status: %s",
             NimBLEUtils::dataToHexString(status.data(), status.size()).c_str());
    const auto ack = NimBLEAttValue({status[0], status[1], status[2], 0x20});
    ESP_LOGI(LOG_TAG, "Responding status with %s",
             NimBLEUtils::dataToHexString(ack.data(), ack.size()).c_str());
    if (!gattWrite(PAIR_SVC_UUID, STATUS_CHR_UUID, ack, true)) {
      ESP_LOGI(LOG_TAG, "Failed to write status response");
      return false;
    }
    if (!registrationAlive())
      return false;
  } else {
    ESP_LOGI(LOG_TAG, "Failed to request status");
    return false;
  }
  m_Progress += 5;

  auto name = NimBLEAttValue(Device::getStringID());
  ESP_LOGI(LOG_TAG, "Identifying as %s", name.c_str());
  setFujifilmSecureRegistration(true);
  const bool identified = gattWrite(PAIR_SVC_UUID, IDENT_CHR_UUID, name, true);
  setFujifilmSecureRegistration(false);
  if (!registrationAlive())
    return false;
  if (!identified) {
    ESP_LOGI(LOG_TAG, "Failed to send identifier");
    return false;
  }
  ESP_LOGI(LOG_TAG, "Identified!");
  if (!requestFujifilmSecureFastProfile()) {
    ESP_LOGI(LOG_TAG, "Failed to request fast connection profile");
    return false;
  }
  // updateConnParams() reports request acceptance, not controller completion.
  // Wait for the live parameters before proceeding, with a short hard bound so
  // a camera that ignores the update cannot stall registration indefinitely.
  constexpr TickType_t connParamsPoll = pdMS_TO_TICKS(10);
  constexpr TickType_t connParamsTimeout = pdMS_TO_TICKS(1000);
  const TickType_t connParamsStarted = xTaskGetTickCount();
  while (!confirmFujifilmSecureFastProfile()) {
    if (!registrationAlive()
        || static_cast<TickType_t>(xTaskGetTickCount() - connParamsStarted) >= connParamsTimeout) {
      ESP_LOGI(LOG_TAG, "Failed to apply fast connection profile");
      return false;
    }
    vTaskDelay(connParamsPoll);
  }
  m_Progress += 5;

  const std::array<sub_t, 6> subscription0 = {
      {
       {"indication 1", SVC_CONF_UUID, CHR_IND1_UUID, false},
       {"indication 2", SVC_CONF_UUID, CHR_IND2_UUID, false},
       {"notification 1", SVC_CONF_UUID, CHR_NOT1_UUID, true},
       {"notification 2", SVC_CONF_UUID, GEOTAG_UPDATE, true},
       {"notification 4", NOTX_SVC_UUID, NOT4_CHR_UUID, true},
       {"notification 5", NOTX_SVC_UUID, NOT5_CHR_UUID, true},
       }
  };

  // A subscribe failure is never fatal. Promotion to active is link-state only,
  // no notification gates it, and on a stale-session reconnect the camera still
  // holds the prior CCCD subscriptions so a re-subscribe can fail or be a no-op
  // without stopping the handshake. The CCCD writes are unacknowledged (see
  // Fujifilm::subscribe), so they cannot block the connect either. Log and
  // continue so the handshake always reaches the shutter characteristic.
  for (const auto &sub : subscription0) {
    if (!registrationAlive())
      return false;
    ESP_LOGI(LOG_TAG, "Subscribing to %s", sub.name.c_str());
    if (!subscribe(sub.service, sub.uuid, sub.notification)) {
      ESP_LOGI(LOG_TAG, "Failed to subscribe to %s", sub.name.c_str());
    }
    if (!registrationAlive())
      return false;
    m_Progress += 5;
  }

  const std::array<sub_t, 6> subscription1 = {
      {
       {"notification 6", SVC_CONF_UUID, NOT6_CHR_UUID, true},
       {"notification 7", NOTX_SVC_UUID, NOT7_CHR_UUID, true},
       {"notification 8", NOTX_SVC_UUID, NOT8_CHR_UUID, true},
       {"notification 9", NOTX_SVC_UUID, NOT9_CHR_UUID, true},
       {"notification 10", NOTX_SVC_UUID, NOT10_CHR_UUID, true},
       {"notification 11", NOTX_SVC_UUID, GEOTAG_SYNC_INTERVAL_UUID, true},
       }
  };

  for (const auto &sub : subscription1) {
    if (!registrationAlive())
      return false;
    ESP_LOGI(LOG_TAG, "Subscribing to %s", sub.name.c_str());
    if (!subscribe(sub.service, sub.uuid, sub.notification)) {
      // Non-fatal, as above. The geotag subscription used to hard-fail here,
      // which turned a single flaky CCCD write on a stale-session reconnect into
      // a stuck connect. Geotag sync is best-effort and does not gate the
      // shutter, so log and continue.
      ESP_LOGI(LOG_TAG, "Failed to subscribe to %s", sub.name.c_str());
    }
    if (!registrationAlive())
      return false;
    m_Progress += 5;
  }

  auto sync_interval = NimBLEAttValue(reinterpret_cast<const uint8_t *>(&GEOTAG_SYNC_INTERVAL),
                                      sizeof(GEOTAG_SYNC_INTERVAL));
  ESP_LOGI(LOG_TAG, "Configuring %hus geotag sync interval", GEOTAG_SYNC_INTERVAL);
  if (!registrationAlive())
    return false;
  if (!gattWrite(NOTX_SVC_UUID, GEOTAG_SYNC_INTERVAL_UUID, sync_interval, true)) {
    ESP_LOGI(LOG_TAG, "Failed to configure geotag sync interval");
    return false;
  }
  if (!registrationAlive())
    return false;
  m_Progress += 5;

  ESP_LOGI(LOG_TAG, "Getting shutter service");
  if (!registrationAlive())
    return false;
  auto *pSvc = m_Client->getService(SHUTTER_SVC_UUID);
  if (pSvc == nullptr) {
    ESP_LOGI(LOG_TAG, "Failed to get shutter service");
    return false;
  }
  m_Progress += 5;

  ESP_LOGI(LOG_TAG, "Getting shutter characteristic");
  if (!registrationAlive())
    return false;
  m_Shutter = pSvc->getCharacteristic(CHR_SHUTTER_UUID);
  if (m_Shutter == nullptr) {
    ESP_LOGI(LOG_TAG, "Failed to get shutter characteristic");
    return false;
  }

  m_Progress = 100;

  return true;
}

size_t FujifilmSecure::getSerialisedBytes(void) const {
  return sizeof(nvs_t);
}

bool FujifilmSecure::serialise(void *buffer, size_t bytes) const {
  if (bytes != sizeof(nvs_t)) {
    return false;
  }
  nvs_t *x = static_cast<nvs_t *>(buffer);
  strncpy(x->name, m_Name.c_str(), MAX_NAME);
  x->address = (uint64_t)m_Address;
  x->type = m_Address.getType();
  x->serial = m_Serial;

  return true;
}

}  // namespace Furble
