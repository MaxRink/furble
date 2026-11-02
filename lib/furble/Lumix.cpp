#include <cstring>
#include <ctime>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <NimBLEAddress.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEDevice.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLERemoteService.h>

#include "Device.h"
#include "Lumix.h"
#include "protocol/AdvertisementProtocol.h"

namespace Furble {

const NimBLEUUID Lumix::SESSION_SVC_UUID {0x054ac620, 0x3214, 0x11e6, 0xac0d0002a5d5c51b};
const NimBLEUUID Lumix::SESSION_INIT_UUID {0x8d08a420, 0x3213, 0x11e6, 0x8aca0002a5d5c51b};
const NimBLEUUID Lumix::DEVICE_NAME_UUID {0xcd7a71a0, 0x3213, 0x11e6, 0x8f560002a5d5c51b};
const NimBLEUUID Lumix::CONTROL_SVC_UUID {0x7be5faae, 0x475b, 0x11e7, 0xa91992ebcb67fe33};
const NimBLEUUID Lumix::CONTROL_UUID {0x7be5fd56, 0x475b, 0x11e7, 0xa91992ebcb67fe33};
const NimBLEUUID Lumix::CONTROL_READINESS_UUID {0x7be5fe6e, 0x475b, 0x11e7, 0xa91992ebcb67fe33};
const NimBLEUUID Lumix::LOCATION_SVC_UUID {0x1d74afe0, 0x3214, 0x11e6, 0x8ab40002a5d5c51b};
const NimBLEUUID Lumix::LOCATION_CHR_UUID {0xdaff1bc0, 0x3216, 0x11e6, 0x91c80002a5d5c51b};
const NimBLEUUID Lumix::LOCATION_READINESS_UUID {0x37701b80, 0x32a7, 0x11e6, 0xbb830002a5d5c51b};
const NimBLEUUID Lumix::CLOCK_SVC_UUID {0x34738720, 0x3214, 0x11e6, 0xb66b0002a5d5c51b};
const NimBLEUUID Lumix::CLOCK_CHR_UUID {0xead55e60, 0x3216, 0x11e6, 0xa42e0002a5d5c51b};
const NimBLEUUID Lumix::CLOCK_REQUEST_UUID {0x4cf487c0, 0x32a7, 0x11e6, 0xa50d0002a5d5c51b};
constexpr std::array<uint8_t, 16> Lumix::MEI0;

namespace {

// Seconds from 1970-01-01 (Unix epoch) to 1980-01-06 (GPS epoch).
constexpr uint32_t GPS_EPOCH_OFFSET = 315964800;

time_t toUnixTime(const Camera::timesync_t &timesync) {
  struct tm time = {
      .tm_sec = static_cast<int>(timesync.second),
      .tm_min = static_cast<int>(timesync.minute),
      .tm_hour = static_cast<int>(timesync.hour),
      .tm_mday = static_cast<int>(timesync.day),
      .tm_mon = static_cast<int>(timesync.month - 1),
      .tm_year = static_cast<int>(timesync.year - 1900),
      .tm_wday = 0,
      .tm_yday = 0,
      .tm_isdst = -1,
  };
  return mktime(&time);
}

}  // namespace

Lumix::Lumix(const void *data, size_t len) : Camera(Type::PANASONIC_LUMIX, PairType::SAVED) {
  if (len != sizeof(lumix_t))
    abort();

  const lumix_t *lumix = static_cast<const lumix_t *>(data);
  m_Name = std::string(lumix->name);
  m_Address = NimBLEAddress(lumix->address, lumix->type);
}

Lumix::Lumix(const NimBLEAdvertisedDevice *pDevice) : Camera(Type::PANASONIC_LUMIX, PairType::NEW) {
  m_Name = pDevice->getName();
  if (m_Name.empty())
    m_Name = "LUMIX";
  m_Address = pDevice->getAddress();
  ESP_LOGI(LOG_TAG, "Lumix Name = %s", m_Name.c_str());
  ESP_LOGI(LOG_TAG, "Lumix Address = %s", m_Address.toString().c_str());
}

bool Lumix::matches(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice == nullptr || !pDevice->haveManufacturerData()) {
    return false;
  }

  const auto manufacturerData = pDevice->getManufacturerData();
  return AdvertisementProtocol::matchesLumixAdvertisement(
      reinterpret_cast<const uint8_t *>(manufacturerData.data()), manufacturerData.size(),
      pDevice->isAdvertisingService(SESSION_SVC_UUID));
}

bool Lumix::_connect(void) {
  // Drop any stale characteristic pointers from a previous session.
  m_Control = nullptr;
  m_ControlReadiness = nullptr;
  m_Location = nullptr;
  m_LocationReadiness = nullptr;
  m_Clock = nullptr;
  m_ClockRequest = nullptr;
  m_ControlReady = false;
  m_LocationReady = false;
  m_LocationResend = false;
  m_ClockReady = false;
  m_SessionStarted = false;
  m_Progress = 10;
  ESP_LOGI(LOG_TAG, "Lumix connecting");
  if (!m_Client->connect(m_Address)) {
    ESP_LOGI(LOG_TAG, "Lumix connection failed");
    return false;
  }
  m_Progress = 30;

  // This is the S5II and BGH1 MEI0 path. S5II first pairing may still require
  // the camera-side WiFi-assisted Lumix Sync handshake before this succeeds.
  NimBLERemoteService *pSvc = m_Client->getService(SESSION_SVC_UUID);
  if (pSvc == nullptr) {
    ESP_LOGI(LOG_TAG, "Lumix session service unavailable");
    return false;
  }

  NimBLERemoteCharacteristic *pChr = pSvc->getCharacteristic(SESSION_INIT_UUID);
  if (pChr == nullptr || !pChr->canWrite()) {
    ESP_LOGI(LOG_TAG, "Lumix session-init characteristic unavailable");
    return false;
  }
  if (!pChr->writeValue(MEI0.data(), MEI0.size(), true)) {
    ESP_LOGI(LOG_TAG, "Lumix MEI0 write failed");
    return false;
  }
  m_Progress = 50;

  pChr = pSvc->getCharacteristic(DEVICE_NAME_UUID);
  if (pChr == nullptr || !pChr->canWrite()) {
    ESP_LOGI(LOG_TAG, "Lumix device-name characteristic unavailable");
    return false;
  }
  const std::string name = Device::getStringID();
  if (!pChr->writeValue(name.c_str(), name.length(), true)) {
    ESP_LOGI(LOG_TAG, "Lumix device-name write failed");
    return false;
  }
  m_Progress = 70;

  pSvc = m_Client->getService(CONTROL_SVC_UUID);
  if (pSvc == nullptr) {
    ESP_LOGI(LOG_TAG, "Lumix control service unavailable");
    return false;
  }
  m_Control = pSvc->getCharacteristic(CONTROL_UUID);
  if (m_Control == nullptr || !m_Control->canWrite()) {
    ESP_LOGI(LOG_TAG, "Lumix control characteristic unavailable");
    return false;
  }

  // The official app sends Confirm 01, waits for a 00 readiness notification
  // on 7be5fe6e, then sends Start 02. A bare ShutterOn 04 is refused by a body
  // that requires the session gate.
  m_ControlReadiness = pSvc->getCharacteristic(CONTROL_READINESS_UUID);
  if (m_ControlReadiness != nullptr) {
    if (!gattSubscribe(
            m_ControlReadiness,
            [this](NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
              if (length > 0 && data[0] == 0x00) {
                m_ControlReady = true;
              }
            },
            false, true)) {
      ESP_LOGW(LOG_TAG, "Lumix control readiness subscribe failed");
    }
  } else {
    ESP_LOGW(LOG_TAG, "Lumix control readiness characteristic unavailable");
  }

  if (!startSession()) {
    ESP_LOGI(LOG_TAG, "Lumix session start failed");
    return false;
  }
  m_Progress = 80;

  pSvc = m_Client->getService(LOCATION_SVC_UUID);
  if (pSvc != nullptr) {
    m_Location = pSvc->getCharacteristic(LOCATION_CHR_UUID);
    // 37701b80: 01 starts location updates, 02 stops them, 03 resends the
    // stored packet. The official app only writes GPS after 01.
    m_LocationReadiness = pSvc->getCharacteristic(LOCATION_READINESS_UUID);
    if (m_LocationReadiness != nullptr) {
      if (!gattSubscribe(
              m_LocationReadiness,
              [this](NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
                if (length == 0) {
                  return;
                }
                switch (data[0]) {
                  case 0x01:
                    m_LocationReady = true;
                    break;
                  case 0x02:
                    // Location updates stopped.
                    m_LocationReady = false;
                    break;
                  case 0x03:
                    // The camera asks for the stored packet again. Keep the
                    // gate open and write the current fix on the next tick.
                    m_LocationReady = true;
                    m_LocationResend = true;
                    break;
                  default:
                    break;
                }
              },
              false, true)) {
        ESP_LOGW(LOG_TAG, "Lumix location readiness subscribe failed");
      }
    }
  } else {
    ESP_LOGI(LOG_TAG, "Lumix location service unavailable");
  }

  pSvc = m_Client->getService(CLOCK_SVC_UUID);
  if (pSvc != nullptr) {
    m_Clock = pSvc->getCharacteristic(CLOCK_CHR_UUID);
    // 4cf487c0 is the clock request. The official app writes the clock packet
    // on this notification.
    m_ClockRequest = pSvc->getCharacteristic(CLOCK_REQUEST_UUID);
    if (m_ClockRequest != nullptr) {
      if (!gattSubscribe(
              m_ClockRequest,
              [this](NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
                if (length > 0) {
                  m_ClockReady = (data[0] == 0x01);
                }
              },
              false, true)) {
        ESP_LOGW(LOG_TAG, "Lumix clock request subscribe failed");
      }
    }
  } else {
    ESP_LOGI(LOG_TAG, "Lumix clock service unavailable");
  }

  m_Progress = 100;
  ESP_LOGI(LOG_TAG, "Lumix connected");
  return true;
}

void Lumix::_disconnect(void) {
  if (m_SessionStarted && m_Location != nullptr && m_Location->canWrite()) {
    // Clear the last position so the camera does not retain a stale fix.
    // LUMIX Sync sends fix 0x56 with sentinel coordinates on GPS stop.
    const geo_t stop = {
        .gps_time = 0,
        .latitude = 0x7fffffff,
        .longitude = 0x7fffffff,
        .altitude = 0x7fff,
        .fix = 0x56,
        .reserved = 0x00,
    };
    m_Location->writeValue(reinterpret_cast<const uint8_t *>(&stop), sizeof(stop), true);
  }
  if (m_SessionStarted) {
    writeCommand(CMD_SESSION_FINISH);
  }
  m_SessionStarted = false;
  m_ControlReady = false;
  m_Control = nullptr;
  m_ControlReadiness = nullptr;
  m_Location = nullptr;
  m_LocationReadiness = nullptr;
  m_Clock = nullptr;
  m_ClockRequest = nullptr;
  m_ControlReady = false;
  m_LocationReady = false;
  m_LocationResend = false;
  m_ClockReady = false;
  if (m_Client != nullptr && m_Client->isConnected())
    m_Client->disconnect();
}

bool Lumix::writeCommand(uint8_t command) {
  if (!isConnected() || m_Control == nullptr || !m_Control->canWrite()) {
    ESP_LOGW(LOG_TAG, "Lumix control write skipped: command=0x%02x connected=%d control=%p",
             command, isConnected(), m_Control);
    return false;
  }
  if (!m_Control->writeValue(&command, sizeof(command), true)) {
    ESP_LOGW(LOG_TAG, "Lumix control write failed: command=0x%02x", command);
    return false;
  }
  return true;
}

bool Lumix::startSession(void) {
  m_ControlReady = false;
  if (!writeCommand(CMD_SESSION_CONFIRM)) {
    return false;
  }

  // The readiness notification is the documented Start gate. Give it two
  // seconds, but do not hang a connect if a body does not send it.
  constexpr uint32_t READINESS_TIMEOUT_MS = 2000;
  constexpr uint32_t READINESS_POLL_MS = 20;
  uint32_t waited = 0;
  while (!m_ControlReady.load() && (waited < READINESS_TIMEOUT_MS)) {
    if (connectCancelled()) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(READINESS_POLL_MS));
    waited += READINESS_POLL_MS;
  }
  if (!m_ControlReady.load()) {
    if (m_ControlReadiness != nullptr) {
      // The body exposes the gate but did not open it. Starting anyway would
      // bypass the Confirm/readiness/Start prerequisite the sequence exists
      // for, and the session would answer every command with a refusal.
      ESP_LOGW(LOG_TAG, "Lumix control readiness timeout; session refused");
      return false;
    }
    ESP_LOGW(LOG_TAG, "Lumix control readiness unavailable; starting without the gate");
  }

  if (!writeCommand(CMD_SESSION_START)) {
    return false;
  }
  m_SessionStarted = true;
  return true;
}

void Lumix::shutterPress(void) {
  writeCommand(CMD_SHUTTER_PRESS);
}

void Lumix::shutterRelease(void) {
  writeCommand(CMD_SHUTTER_RELEASE);
}

void Lumix::focusPress(void) {
  // LUMIX has no focus opcode. 01 and 02 are Confirm and Start session
  // commands, never focus. See spec/panasonic.md C06 and C07.
  ESP_LOGI(LOG_TAG, "Lumix focusPress ignored (no focus protocol)");
}

void Lumix::focusRelease(void) {
  ESP_LOGI(LOG_TAG, "Lumix focusRelease ignored (no focus protocol)");
}

void Lumix::updateGeoData(const gps_t &gps, const timesync_t &timesync) {
  if (!isConnected()) {
    ESP_LOGW(LOG_TAG, "Lumix GPS skipped: not connected");
    return;
  }

  const time_t unixTime = toUnixTime(timesync);
  const bool timeValid = unixTime >= GPS_EPOCH_OFFSET;

  // The clock request is independent of the location gate and of the location
  // write. LUMIX Sync answers 4cf487c0 even while location updates are off, so
  // a pending clock request must not wait for 37701b80 or for a GPS write.
  const bool clockReady = (m_ClockRequest == nullptr) || m_ClockReady.load();
  if (timeValid && clockReady && m_Clock != nullptr && m_Clock->canWrite()) {
    clock_t clock = {
        .year = static_cast<uint16_t>(timesync.year),
        .month = static_cast<uint8_t>(timesync.month),
        .day = static_cast<uint8_t>(timesync.day),
        .hour = static_cast<uint8_t>(timesync.hour),
        .minute = static_cast<uint8_t>(timesync.minute),
        .second = static_cast<uint8_t>(timesync.second),
        // The official app writes the standard offset at bytes 7 to 8 and a
        // DST flag at byte 9. Furble has no timezone source, so both are zero.
        .tz_offset = 0,
        .dst = 0,
    };
    m_Clock->writeValue(reinterpret_cast<const uint8_t *>(&clock), sizeof(clock), true);
  }

  if (m_Location == nullptr || !m_Location->canWrite()) {
    ESP_LOGW(LOG_TAG, "Lumix GPS characteristic unavailable");
    return;
  }

  // A body that exposes 37701b80 must report ready before the first write. The
  // notification can arrive after connect, so a skipped update is not an error:
  // the next GPS tick writes once the gate opens. A 03 value asks for the
  // stored packet and is served on the next tick without a new fix.
  const bool resend = m_LocationResend.exchange(false);
  if ((m_LocationReadiness != nullptr) && !m_LocationReady.load() && !resend) {
    ESP_LOGD(LOG_TAG, "Lumix GPS waiting for location readiness");
    return;
  }
  if (!timeValid) {
    ESP_LOGW(LOG_TAG, "Lumix GPS skipped: invalid time");
    return;
  }

  geo_t geo = {
      .gps_time = static_cast<uint32_t>(unixTime - GPS_EPOCH_OFFSET),
      .latitude = static_cast<int32_t>(gps.latitude * 10000000.0),
      .longitude = static_cast<int32_t>(gps.longitude * 10000000.0),
      .altitude = static_cast<int16_t>(gps.altitude),
      .fix = 0x41,
      .reserved = 0x00,
  };
  bool rc = m_Location->writeValue(reinterpret_cast<const uint8_t *>(&geo), sizeof(geo), true);

  ESP_LOGI(LOG_TAG, "Lumix GPS lat=%.7f lon=%.7f alt=%.1f utc=%04u-%02u-%02u %02u:%02u:%02u => %s",
           gps.latitude, gps.longitude, gps.altitude, timesync.year, timesync.month, timesync.day,
           timesync.hour, timesync.minute, timesync.second, rc ? "ok" : "failed");
}

size_t Lumix::getSerialisedBytes(void) const {
  return sizeof(lumix_t);
}

bool Lumix::serialise(void *buffer, size_t bytes) const {
  if (bytes != sizeof(lumix_t))
    return false;

  lumix_t *x = static_cast<lumix_t *>(buffer);
  strncpy(x->name, m_Name.c_str(), MAX_NAME);
  x->name[MAX_NAME - 1] = '\0';
  x->address = static_cast<uint64_t>(m_Address);
  x->type = m_Address.getType();
  return true;
}

}  // namespace Furble
