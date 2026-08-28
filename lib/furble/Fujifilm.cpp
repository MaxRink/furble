#include <NimBLEAddress.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEDevice.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLERemoteService.h>
#if defined(FURBLE_HOST_REGISTRATION_TIMEOUT_MS)
#include <chrono>
#include <thread>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "Device.h"
#include "Fujifilm.h"
#include "protocol/FujifilmProtocol.h"

namespace Furble {

void Fujifilm::notify(BLERemoteCharacteristic *pChr,
                      uint8_t *pData,
                      size_t length,
                      bool isNotify,
                      uint32_t generation) {
  if (generation != m_RegistrationGeneration.load()) {
    ESP_LOGW(LOG_TAG, "Ignoring stale Fujifilm registration callback");
    return;
  }
  if (pChr == nullptr) {
    ESP_LOGW(LOG_TAG, "Ignoring Fujifilm callback without a characteristic");
    return;
  }

  ESP_LOGI(LOG_TAG, "Got %s (%u bytes) from %s", isNotify ? "notification" : "indication", length,
           pChr->getUUID().toString().c_str());
  if (length > 0) {
    ESP_LOGI(LOG_TAG, " %s", NimBLEUtils::dataToHexString(pData, length).c_str());
  }

  if (pChr->getUUID() == CHR_NOT1_UUID) {
    // The X100VI capture records 01 00. Keep accepting the legacy 02 00 form
    // used by older Fujifilm bodies, but only on this dedicated characteristic.
    if (FujifilmProtocol::isRegistrationNotification(pData, length)) {
      m_Configured = true;
    }
  } else if (pChr->getUUID() == GEOTAG_UPDATE) {
    if (FujifilmProtocol::isGeotagRequest(pData, length)) {
      m_GeoRequested = true;
    }
  } else {
    ESP_LOGW(LOG_TAG, "Unhandled subscription.");
  }
}

bool Fujifilm::subscribe(const NimBLEUUID &svc,
                         const NimBLEUUID &chr,
                         bool notification,
                         bool response) {
  auto pSvc = m_Client->getService(svc);
  if (pSvc == nullptr) {
    return false;
  }

  auto pChr = pSvc->getCharacteristic(chr);
  if (pChr == nullptr) {
    return false;
  }

  // response defaults to false: the CCCD descriptor write is unacknowledged so a
  // stale-session reconnect, where the camera still holds the prior CCCD
  // subscriptions, cannot block the connect waiting for a write response that
  // never comes.
  const uint32_t generation = m_RegistrationGeneration.load();
  return gattSubscribe(
      pChr,
      [this, generation](BLERemoteCharacteristic *pChr, uint8_t *pData, size_t length,
                         bool isNotify) {
        this->notify(pChr, pData, length, isNotify, generation);
      },
      !notification, response);
}

bool Fujifilm::waitForRegistration(uint8_t progress, bool cancelOnInactive) {
  m_Progress = progress;
#if defined(FURBLE_HOST_REGISTRATION_TIMEOUT_MS)
  const auto started = std::chrono::steady_clock::now();
#else
  const TickType_t started = xTaskGetTickCount();
  const TickType_t timeout = pdMS_TO_TICKS(REGISTRATION_TIMEOUT_MS);
  const TickType_t poll = pdMS_TO_TICKS(REGISTRATION_POLL_MS);
#endif

  while (!m_Configured.load()) {
    if (!m_Connected || (cancelOnInactive && !isActive())) {
      ESP_LOGW(LOG_TAG, "Fujifilm registration aborted before confirmation");
      return false;
    }
#if defined(FURBLE_HOST_REGISTRATION_TIMEOUT_MS)
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (elapsed.count() >= REGISTRATION_TIMEOUT_MS) {
#else
    if (static_cast<TickType_t>(xTaskGetTickCount() - started) >= timeout) {
#endif
      ESP_LOGW(LOG_TAG, "Registration not confirmed after %lu ms; put the camera in pairing mode",
               static_cast<unsigned long>(REGISTRATION_TIMEOUT_MS));
      return false;
    }
#if defined(FURBLE_HOST_REGISTRATION_TIMEOUT_MS)
    std::this_thread::sleep_for(std::chrono::milliseconds(REGISTRATION_POLL_MS));
#else
    vTaskDelay(poll);
#endif
  }

  ESP_LOGI(LOG_TAG, "Fujifilm registration confirmed");
  return true;
}

/**
 * Determine if the advertised BLE device is a Fujifilm.
 */
bool Fujifilm::matches(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice != nullptr && pDevice->haveManufacturerData()) {
    const auto manufacturerData = pDevice->getManufacturerData();
    return FujifilmProtocol::isFujifilmAdvertisement(
        reinterpret_cast<const uint8_t *>(manufacturerData.data()), manufacturerData.length());
  }
  return false;
}

template <std::size_t N>
void Fujifilm::sendShutterCommand(const std::array<uint8_t, N> &cmd,
                                  const std::array<uint8_t, N> &param) {
  if (m_Shutter != nullptr && m_Shutter->canWrite()) {
    gattWrite(m_Shutter, cmd.data(), sizeof(cmd), true);
    gattWrite(m_Shutter, param.data(), sizeof(cmd), true);
  }
}

void Fujifilm::shutterPress(void) {
  const auto frame = FujifilmProtocol::makeShutterFrame(FujifilmProtocol::ShutterAction::PRESS);
  sendShutterCommand(frame.command, frame.parameter);
}

void Fujifilm::shutterRelease(void) {
  const auto frame = FujifilmProtocol::makeShutterFrame(FujifilmProtocol::ShutterAction::RELEASE);
  sendShutterCommand(frame.command, frame.parameter);
}

void Fujifilm::focusPress(void) {
  const auto frame = FujifilmProtocol::makeShutterFrame(FujifilmProtocol::ShutterAction::FOCUS);
  sendShutterCommand(frame.command, frame.parameter);
}

void Fujifilm::focusRelease(void) {
  shutterRelease();
}

void Fujifilm::sendGeoData(const gps_t &gps, const timesync_t &timesync) {
  NimBLERemoteService *pSvc = m_Client->getService(SVC_GEOTAG_UUID);
  if (pSvc == nullptr) {
    return;
  }

  NimBLERemoteCharacteristic *pChr = pSvc->getCharacteristic(CHR_GEOTAG_UUID);
  if (pChr == nullptr) {
    return;
  }

  if (pChr->canWrite()) {
    const FujifilmProtocol::GeotagInput input = {gps.latitude,  gps.longitude,   gps.altitude,
                                                 timesync.year, timesync.month,  timesync.day,
                                                 timesync.hour, timesync.minute, timesync.second};
    const auto geotag = FujifilmProtocol::encodeGeotag(input);
    const int32_t latitude = static_cast<int32_t>(gps.latitude * 10000000);
    const int32_t longitude = static_cast<int32_t>(gps.longitude * 10000000);
    const int32_t altitude = static_cast<int32_t>(gps.altitude);

    ESP_LOGI(LOG_TAG,
             "Sending geotag data (%u bytes) to 0x%04x\r\n"
             "  lat: %f, %ld\r\n"
             "  lon: %f, %ld\r\n"
             "  alt: %f, %ld\r\n",
             geotag.size(), pChr->getHandle(), gps.latitude, latitude, gps.longitude, longitude,
             gps.altitude, altitude);

    gattWrite(pChr, geotag.data(), geotag.size(), true);
  }
}

void Fujifilm::updateGeoData(const gps_t &gps, const timesync_t &timesync) {
  if (m_GeoRequested) {
    sendGeoData(gps, timesync);
    m_GeoRequested = false;
  }
}

void Fujifilm::_disconnect(void) {
  m_Client->disconnect();
}

}  // namespace Furble
