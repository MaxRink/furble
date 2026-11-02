#include <NimBLEAddress.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEDevice.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLERemoteService.h>

#include <array>
// The registration wait below needs a clock, and which clock is correct depends
// on whether a FreeRTOS tick exists, not on which platform this is. So ask for
// exactly that.
//
// Keying it on a platform macro is what broke it: the simulator defines neither
// ESP_PLATFORM nor the host timeout override, so a `defined(ESP_PLATFORM)` test
// dropped it onto the wall clock while the simulator runs on a virtual clock
// its scenarios advance, and a scenario could pass a hundred virtual seconds in
// no real time with the wait neither seeing the confirmation nor timing out.
// __has_include cannot be wrong that way: firmware and the simulator both have
// the header, and the simulator's tick is the virtual clock, which is the one
// its scenarios move.
#if __has_include(<freertos/FreeRTOS.h>)
#define FURBLE_REGISTRATION_WAIT_USES_TICKS 1
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
// Host unit targets that link no FreeRTOS shim. Wall clock is the right answer
// there: nothing advances a virtual clock for them.
#include <chrono>
#include <thread>
#endif

#include "Device.h"
#include "Fujifilm.h"
#include "FurbleTestSync.h"
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
    // The X100VI capture records 01 00. The official apps also treat 01 00 on
    // this characteristic as a date or time request, so this is not a proven
    // registration flag. Keep the operational promotion because the camera
    // only sends it to an accepted client, and keep accepting the legacy
    // 02 00 form used by older Fujifilm bodies, but only on this dedicated
    // characteristic.
    if (FujifilmProtocol::isRegistrationNotification(pData, length)) {
      m_Configured = true;
      // 01 00 is also the camera's date and time request. A request can arrive
      // during registration before any fix cached a timestamp, and a GATT write
      // must not run inside the NimBLE callback. Remember it and let the control
      // task answer on the next GPS update; the flag survives until served, so a
      // request that arrives before the first fix is not lost.
      if (length >= 2 && pData[0] == 0x01 && pData[1] == 0x00) {
        m_TimeRequested = true;
      }
    }
  } else if (pChr->getUUID() == GEOTAG_UPDATE) {
    if (FujifilmProtocol::isGeotagRequest(pData, length)) {
      m_GeoRequested = true;
      // A saved-camera reconnect on the X100VI does not resend the dedicated
      // registration confirmation. The camera goes straight to periodic geotag
      // requests, which it only sends to a client it has accepted, so treat a
      // valid request as the registration confirmation. Hardware trace
      // 2026-08-28: reconnect stalls at 25 s without this while 01 00 arrives
      // here every 10 s.
      m_Configured = true;
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
  // The gate is open here: the subscription is live and nothing is confirmed
  // yet. Fires before the deadline below is taken, so a parked test thread does
  // not burn the registration timeout it is about to observe.
  FURBLE_TEST_SYNC_POINT("fujifilm_registration_wait");
#if defined(FURBLE_REGISTRATION_WAIT_USES_TICKS)
  const TickType_t started = xTaskGetTickCount();
  const TickType_t timeout = pdMS_TO_TICKS(REGISTRATION_TIMEOUT_MS);
  const TickType_t poll = pdMS_TO_TICKS(REGISTRATION_POLL_MS);
#else
  const auto started = std::chrono::steady_clock::now();
#endif

  while (!m_Configured.load()) {
    // connectCancelled() is the direct user-cancel path. The isActive() check
    // only works when the attempt began active, and a stale-session camera
    // keeps the link up so m_Connected never clears: without the cancel token a
    // disconnect during this wait blocked the whole teardown for the full
    // registration timeout (plan 148).
    if (!m_Connected || connectCancelled() || (cancelOnInactive && !isActive())) {
      ESP_LOGW(LOG_TAG, "Fujifilm registration aborted before confirmation");
      return false;
    }
#if defined(FURBLE_REGISTRATION_WAIT_USES_TICKS)
    if (static_cast<TickType_t>(xTaskGetTickCount() - started) >= timeout) {
#else
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (elapsed.count() >= REGISTRATION_TIMEOUT_MS) {
#endif
      ESP_LOGW(LOG_TAG, "Registration not confirmed after %lu ms; put the camera in pairing mode",
               static_cast<unsigned long>(REGISTRATION_TIMEOUT_MS));
      return false;
    }
#if defined(FURBLE_REGISTRATION_WAIT_USES_TICKS)
    vTaskDelay(poll);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(REGISTRATION_POLL_MS));
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
  if (m_Shutter != nullptr && m_Shutter->canWrite()) {
    gattWrite(m_Shutter, FujifilmProtocol::SHUTTER_HALF_PRESS.data(),
              FujifilmProtocol::SHUTTER_HALF_PRESS.size(), true);
  }
}

void Fujifilm::focusRelease(void) {
  if (m_Shutter != nullptr && m_Shutter->canWrite()) {
    gattWrite(m_Shutter, FujifilmProtocol::SHUTTER_RELEASE.data(),
              FujifilmProtocol::SHUTTER_RELEASE.size(), true);
  }
}

void Fujifilm::sendTimeData(void) {
  if (!m_HasTimesync || m_Client == nullptr) {
    return;
  }
  NimBLERemoteService *pSvc = m_Client->getService(SVC_TIME_UUID);
  if (pSvc == nullptr) {
    return;
  }

  NimBLERemoteCharacteristic *pChr = pSvc->getCharacteristic(CHR_UTC_TIME_UUID);
  if (pChr != nullptr && pChr->canWrite()) {
    const FujifilmProtocol::TimeInput input = {
        m_LastTimesync.year,
        m_LastTimesync.month,
        m_LastTimesync.day,
        m_LastTimesync.hour,
        m_LastTimesync.minute,
        m_LastTimesync.second,
        0,
        0,
    };
    const auto packet = FujifilmProtocol::encodeTime(input);
    if (gattWrite(pChr, packet.data(), packet.size(), true)) {
      ESP_LOGI(LOG_TAG, "Sent UTC time packet");
    }
    return;
  }

  // The local-time characteristic (B9BFD37F) has no offset field and expects
  // wall-clock time. Furble only has UTC, so writing the cached fields there
  // would set a camera in any non-UTC timezone wrong. If the body exposes no
  // writable UTC characteristic, leave its clock alone until a local-time
  // source exists.
}

void Fujifilm::sendGeoData(const gps_t &gps, const timesync_t &timesync) {
  NimBLERemoteService *pSvc = m_Client->getService(SVC_GEOTAG_UUID);
  if (pSvc == nullptr) {
    return;
  }

  if (!m_HasLocationSync) {
    // Enable the location sync setting before the first packet. The interval
    // alone does not turn the feature on for a body whose menu default is off.
    // Firmware accepts exactly 0 or 1 on AAB609C4.
    NimBLERemoteService *syncSvc = m_Client->getService(SVC_READ_UUID);
    if (syncSvc != nullptr) {
      NimBLERemoteCharacteristic *syncChr = syncSvc->getCharacteristic(CHR_LOCATION_SYNC_UUID);
      if (syncChr != nullptr && syncChr->canWrite()) {
        const std::array<uint8_t, 2> enable = {0x01, 0x00};
        if (gattWrite(syncChr, enable.data(), enable.size(), true)) {
          m_HasLocationSync = true;
          // Read back through the UUID seam, not the cached characteristic: a
          // drop right after the ATT response can free the self-deleting
          // client's attributes before this line.
          NimBLEAttValue value;
          if (gattRead(SVC_READ_UUID, CHR_LOCATION_SYNC_UUID, value) && (value.size() >= 2)) {
            ESP_LOGI(LOG_TAG, "Location sync setting readback 0x%02x%02x", value.data()[1],
                     value.data()[0]);
          }
        } else {
          ESP_LOGW(LOG_TAG, "Location sync enable failed");
        }
      }
    }
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
  m_LastTimesync = timesync;
  m_HasTimesync = true;
  // Both writes can be reached from one GPS tick. A drop during the first one
  // self-deletes the client, so hold the lifetime lock and re-check the link
  // before entering the geotag write with a stale characteristic cache.
  auto lifetime = lockClientLifetime();
  if (!isConnected()) {
    return;
  }
  if (m_TimeRequested.exchange(false)) {
    sendTimeData();
  }
  if (m_GeoRequested && isConnected()) {
    sendGeoData(gps, timesync);
    m_GeoRequested = false;
  }
}

void Fujifilm::_disconnect(void) {
  m_HasLocationSync = false;
  m_HasTimesync = false;
  m_GeoRequested = false;
  m_TimeRequested = false;
  m_Client->disconnect();
}

}  // namespace Furble
