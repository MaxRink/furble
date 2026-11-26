#include <NimBLEAddress.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEDevice.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLERemoteService.h>
#include <NimBLEUtils.h>

#include "Sony.h"
#include "protocol/AdvertisementProtocol.h"

namespace Furble {

Sony::Sony(const void *data, size_t len) : Camera(Type::SONY, PairType::SAVED) {
  if (len != sizeof(sony_t))
    abort();

  const sony_t *sony = static_cast<const sony_t *>(data);
  m_Name = std::string(sony->name);
  m_Address = NimBLEAddress(sony->address, sony->type);
  memcpy(&m_Uuid, &sony->uuid, sizeof(Device::uuid128_t));
}

Sony::Sony(const NimBLEAdvertisedDevice *pDevice) : Camera(Type::SONY, PairType::NEW) {
  m_Name = pDevice->getName();
  m_Address = pDevice->getAddress();
  ESP_LOGI(LOG_TAG, "Name = %s", m_Name.c_str());
  ESP_LOGI(LOG_TAG, "Address = %s", m_Address.toString().c_str());
  m_Uuid = Device::getUUID128();

  // Keep the advertised generation: a 2019 body clears tag 0x22 bit 1 and is
  // the one that needs the best-effort location path. A saved record carries no
  // advertisement, and Sony does not scan on reconnect, so it stays Unknown and
  // the discovered GATT profile decides instead.
  if (pDevice->haveManufacturerData()) {
    const auto &manufacturerData = pDevice->getManufacturerData();
    AdvertisementProtocol::SonyGeneration generation =
        AdvertisementProtocol::SonyGeneration::Current;
    if (AdvertisementProtocol::matchesSonyAdvertisement(
            reinterpret_cast<const uint8_t *>(manufacturerData.data()), manufacturerData.size(),
            generation)) {
      m_Generation = (generation == AdvertisementProtocol::SonyGeneration::Legacy)
                         ? Generation::Legacy
                         : Generation::Current;
    }
    ESP_LOGI(LOG_TAG, "Advertisement generation = %s",
             m_Generation == Generation::Legacy ? "legacy (2019)" : "current");
  }
}

bool Sony::matches(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice != nullptr && pDevice->haveManufacturerData()) {
    const auto &manufacturerData = pDevice->getManufacturerData();
    return AdvertisementProtocol::matchesSonyAdvertisement(
        reinterpret_cast<const uint8_t *>(manufacturerData.data()), manufacturerData.size());
  }

  return false;
}

bool Sony::_connect(void) {
  // Drop any stale remote pointers from a previous session.
  m_Control = nullptr;
  m_GeoSvc = nullptr;
  m_GeoGate = GeoGate::Strict;
  m_GeoReported = false;
  m_GeoUnsupported = false;

  // The location packet is 91 or 95 bytes in one DD11 write, so the link has to
  // carry 98 bytes of MTU. The preferred value is proposed by the exchange the
  // BLE library issues when the connection is established, so it has to be
  // raised before connecting, and the result has to be checked afterwards
  // because a body may answer with the ATT minimum.
  requestAttMtu(ATT_MTU_FLOOR);

  ESP_LOGI(LOG_TAG, "Connecting");
  if (!m_Client->connect(m_Address)) {
    ESP_LOGI(LOG_TAG, "Connection failed!!!");
    return false;
  }

  ESP_LOGI(LOG_TAG, "Connected");
  m_Progress = 20;

  (void)verifyAttMtu(ATT_MTU_FLOOR);

  ESP_LOGI(LOG_TAG, "Securing");
  if (!m_Client->secureConnection()) {
    return false;
  }
  ESP_LOGI(LOG_TAG, "Secured!");
  m_Progress = 40;

  ESP_LOGI(LOG_TAG, "Retrieving control service!");
  auto *pSvc = m_Client->getService(CTRL_SVC_UUID);
  if (!pSvc) {
    return false;
  }
  m_Progress = 60;

  m_Control = pSvc->getCharacteristic(CTRL_CHR_UUID);
  if (!m_Control) {
    return false;
  }
  ESP_LOGI(LOG_TAG, "Retrieved control service!");
  m_Progress = 80;

  ESP_LOGI(LOG_TAG, "Retrieving location service");
  m_GeoSvc = m_Client->getService(GEO_SVC_UUID);
  if (m_GeoSvc != nullptr) {
    ESP_LOGI(LOG_TAG, "Retrieved location service!");
    auto *pStatus = m_GeoSvc->getCharacteristic(GEO_CHR_UUID);
    if (pStatus != nullptr) {
      gattSubscribe(
          pStatus, [](NimBLERemoteCharacteristic *, uint8_t *, size_t, bool) {}, false, true);
    }

    // The DD30 allow and DD31 enable pair is the location gate on the current
    // generation. A body that does not expose it has nothing to gate, and only
    // then is the direct DD11 write the honest option. The profile is the
    // authority; the advertised generation breaks the tie for a saved record,
    // which has no advertisement. A body that advertises the current generation
    // keeps today's strict path even if discovery comes back short.
    const bool allow = m_GeoSvc->getCharacteristic(GEO_ALLOW_CHR_UUID) != nullptr;
    const bool enable = m_GeoSvc->getCharacteristic(GEO_ENABLE_CHR_UUID) != nullptr;
    if (!allow || !enable) {
      if (m_Generation == Generation::Current) {
        m_GeoGate = GeoGate::Strict;
        ESP_LOGW(LOG_TAG, "Current generation without DD30/DD31 (allow=%d enable=%d)", allow,
                 enable);
      } else {
        m_GeoGate = GeoGate::BestEffort;
        ESP_LOGI(LOG_TAG, "No DD30/DD31 (allow=%d enable=%d), generation %s: best-effort DD11",
                 allow, enable, m_Generation == Generation::Legacy ? "legacy" : "saved");
      }
    }
  }

  m_Progress = 100;
  ESP_LOGI(LOG_TAG, "Done!");

  return true;
}

void Sony::_disconnect(void) {
  // The release writes must not race a self-deleting client: hold the teardown
  // lock so a link drop waits for this sequence, and re-check the link before
  // each write. gattWrite also checks isConnected().
  auto lifetime = lockClientLifetime();
  if (!m_Connected) {
    return;
  }
  // A best-effort body exposes neither DD31 nor DD30, so it has no state to
  // release and the two writes would only fail.
  if ((m_GeoSvc != nullptr) && (m_GeoGate == GeoGate::Strict) && isConnected()) {
    const uint8_t release = 0x00;
    gattWrite(GEO_SVC_UUID, GEO_ENABLE_CHR_UUID, &release, sizeof(release), false);
    gattWrite(GEO_SVC_UUID, GEO_ALLOW_CHR_UUID, &release, sizeof(release), false);
  }
  if (isConnected()) {
    m_Client->disconnect();
  }
}

void Sony::shutterPress(void) {
  const uint16_t command = SHUTTER_DOWN;
  gattWrite(m_Control, reinterpret_cast<const uint8_t *>(&command), sizeof(command), true);
  return;
}

void Sony::shutterRelease(void) {
  const uint16_t command = SHUTTER_UP;
  gattWrite(m_Control, reinterpret_cast<const uint8_t *>(&command), sizeof(command), true);
  return;
}

void Sony::focusPress(void) {
  const uint16_t command = FOCUS_DOWN;
  gattWrite(m_Control, reinterpret_cast<const uint8_t *>(&command), sizeof(command), true);
  return;
}

void Sony::focusRelease(void) {
  const uint16_t command = FOCUS_UP;
  gattWrite(m_Control, reinterpret_cast<const uint8_t *>(&command), sizeof(command), true);
}

bool Sony::locationEnabled(void) {
  if (m_GeoGate == GeoGate::BestEffort) {
    // This body exposes no DD30 allow or DD31 enable, so there is no state to
    // read or set. The location service being present is the only capability
    // signal available, and updateGeoData decides the rest.
    return m_GeoSvc != nullptr;
  }

  if (m_GeoSvc) {
    NimBLEAttValue allowValue;
    NimBLEAttValue enabledValue;
    gattRead(GEO_SVC_UUID, GEO_ALLOW_CHR_UUID, allowValue);
    gattRead(GEO_SVC_UUID, GEO_ENABLE_CHR_UUID, enabledValue);

    if (allowValue.size() > 0 && enabledValue.size() > 0) {
      uint8_t allow = allowValue.data()[0];
      uint8_t enabled = enabledValue.data()[0];
      // ESP_LOGI(LOG_TAG, "1: allow = %x, enabled = %x", allow, enabled);
      if (allow != LOCATION_ALLOW
          && gattWrite(GEO_SVC_UUID, GEO_ALLOW_CHR_UUID, &LOCATION_ALLOW, sizeof(LOCATION_ALLOW),
                       false)) {
        gattRead(GEO_SVC_UUID, GEO_ALLOW_CHR_UUID, allowValue);
        if (allowValue.size() > 0) {
          allow = allowValue.data()[0];
        }
        ESP_LOGI(LOG_TAG, "Location service allowed!");
      }
      if (enabled != LOCATION_ENABLE
          && gattWrite(GEO_SVC_UUID, GEO_ENABLE_CHR_UUID, &LOCATION_ENABLE, sizeof(LOCATION_ENABLE),
                       false)) {
        gattRead(GEO_SVC_UUID, GEO_ENABLE_CHR_UUID, enabledValue);
        if (enabledValue.size() > 0) {
          enabled = enabledValue.data()[0];
        }
        ESP_LOGI(LOG_TAG, "Location service enabled!");
      }

      // auto info = m_GeoSvc->getValue(GEO_INFO_CHR_UUID);
      // ESP_LOGI(LOG_TAG, "info = %s",
      // NimBLEUtils::dataToHexString(info.data(), info.size()).c_str());

      // ESP_LOGI(LOG_TAG, "2: allow = %x, enabled = %x", allow, enabled);
      if (allow == LOCATION_ALLOW && enabled == LOCATION_ENABLE) {
        return true;
      }
    }
  }

  return false;
}

void Sony::updateGeoData(const gps_t &gps, const timesync_t &timesync) {
  // The DD21 read can be the drop point and frees the service cache with the
  // self-deleting client. Hold the teardown lock and re-check the link before
  // the service pointer is used again.
  auto lifetime = lockClientLifetime();
  if (!isConnected()) {
    return;
  }
  if (m_GeoUnsupported) {
    return;
  }
  if (locationEnabled() && isConnected()) {
    // DD21 byte 4 bit 1 selects the 91-byte form. The ZV-1F 2.01 firmware
    // exposes the 91-byte 0x59 entry, not the 95-byte 0x5d entry.
    bool use95 = true;
    if (m_GeoSvc != nullptr) {
      NimBLEAttValue info;
      if (gattRead(GEO_SVC_UUID, GEO_INFO_CHR_UUID, info) && (info.size() >= 5)) {
        use95 = (info.data()[4] & 0x02) != 0;
      }
    }
    if (!isConnected()) {
      return;
    }

    sony_geo_t geo = {
        .prefix = {0x00, 0x5d, 0x08, 0x02, 0xfc, 0x03, 0x00, 0x00, 0x10, 0x10, 0x10},
        .latitude = (int32_t)__builtin_bswap32((uint32_t)(gps.latitude * 10000000)),
        .longitude = (int32_t)__builtin_bswap32((uint32_t)(gps.longitude * 10000000)),
        .year = __builtin_bswap16((uint16_t)timesync.year),
        .month = (uint8_t)timesync.month,
        .day = (uint8_t)timesync.day,
        .hour = (uint8_t)timesync.hour,
        .minute = (uint8_t)timesync.minute,
        .second = (uint8_t)timesync.second,
        .zero0 = {0x00},
        // The 95-byte form carries the current timezone offset and the DST
        // difference in minutes, both big-endian.
        .offset = __builtin_bswap16(static_cast<uint16_t>(timesync.tz_offset_minutes)),
        .dst_offset = __builtin_bswap16(
            static_cast<uint16_t>(timesync.dst ? timesync.tz_dst_offset_minutes : 0)),
    };
    // ESP_LOGI(LOG_TAG, "geo: %s", NimBLEUtils::dataToHexString((const uint8_t *)&geo,
    // sizeof(geo)).c_str());
    if (!use95) {
      geo.prefix[1] = 0x59;
      geo.prefix[5] = 0x00;
    }
    auto *pChr = m_GeoSvc->getCharacteristic(GEO_UPDATE_UUID);
    const size_t length = use95 ? sizeof(geo) : sizeof(geo) - 4;

    // DD11 takes the whole packet in one write. The ATT layer cannot split it
    // into two writes, and a long or prepared write would have to be reassembled
    // by the camera, which no captured exchange shows it doing. So a link that
    // cannot carry length + 3 bytes in one write gets no location at all, which
    // is reported once per session rather than failing every GPS tick. Read the
    // MTU again here: a late exchange completion raises it mid-session.
    const size_t required = length + ATT_WRITE_HEADER_BYTES;
    const uint16_t mtu = attMtu();
    if (mtu < required) {
      if (!m_GeoReported) {
        m_GeoReported = true;
        ESP_LOGW(LOG_TAG, "Location update needs a %u byte ATT MTU on a %u byte link, skipped",
                 static_cast<unsigned>(required), mtu);
      }
      return;
    }

    if (!gattWrite(pChr, reinterpret_cast<const uint8_t *>(&geo), length, true)) {
      // A body without the DD30/DD31 handshake may still refuse DD11. That is
      // not an error the user should see, and retrying a refused write on every
      // GPS tick would only spend radio time, so this session stops trying.
      if (m_GeoGate == GeoGate::BestEffort) {
        m_GeoUnsupported = true;
      }
      if (!m_GeoReported) {
        m_GeoReported = true;
        ESP_LOGI(LOG_TAG, "Location update rejected by the camera, no location for this session");
      }
    }
  }
}

size_t Sony::getSerialisedBytes(void) const {
  return sizeof(sony_t);
}

bool Sony::serialise(void *buffer, size_t bytes) const {
  if (bytes != sizeof(sony_t)) {
    return false;
  }
  sony_t *x = static_cast<sony_t *>(buffer);
  strncpy(x->name, m_Name.c_str(), MAX_NAME);
  x->address = (uint64_t)m_Address;
  x->type = m_Address.getType();
  memcpy(&x->uuid, &m_Uuid, sizeof(Device::uuid128_t));

  return true;
}

}  // namespace Furble
