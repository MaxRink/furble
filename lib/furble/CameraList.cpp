#include <NimBLEAdvertisedDevice.h>
#include <Preferences.h>

#include "CanonEOSRemote.h"
#include "CanonEOSSmart.h"
#include "DJIOsmo.h"
#include "FauxNY.h"
#include "FujifilmBasic.h"
#include "FujifilmSecure.h"
#include "Lumix.h"
#include "Nikon.h"
#include "Ricoh.h"
#include "Sony.h"

#include "CameraList.h"
#include "protocol/CameraListProtocol.h"

#define FURBLE_PREF_INDEX "index"
#define FURBLE_PREF_INDEX_FORMAT "index_format"
#define FURBLE_PREF_INDEX_CHECKSUM "index_crc"
#define FURBLE_INDEX_FORMAT_CURRENT 1
// Monotonic id allocator, persisted alongside the index in the same namespace.
#define FURBLE_PREF_NEXT_ID "cam_next_id"

namespace Furble {

std::vector<std::shared_ptr<Furble::Camera>> CameraList::m_ConnectList;
Preferences CameraList::m_Prefs;

void CameraList::fillSaveEntry(index_entry_t &entry, const Camera *camera) {
  const auto key = CameraListProtocol::addressKey(static_cast<uint64_t>(camera->getAddress()));
  snprintf(entry.name, sizeof(entry.name), "%s", key.c_str());
  entry.type = camera->getType();
  entry.camera_id = 0;
}

bool CameraList::containsCameraId(const std::vector<index_entry_t> &index, uint8_t cameraId) {
  for (const auto &entry : index) {
    if (entry.camera_id == cameraId) {
      return true;
    }
  }
  return false;
}

uint8_t CameraList::allocateCameraId(const std::vector<index_entry_t> &index) {
  // Persisted counter, never handed back below what it has already reached.
  uint8_t persistedNext = 1;
  if (m_Prefs.get(FURBLE_PREF_NEXT_ID, &persistedNext, sizeof(persistedNext))
      != sizeof(persistedNext)) {
    persistedNext = 1;
  }
  uint16_t next = persistedNext;
  while ((next <= 0xff) && ((next == 0) || containsCameraId(index, static_cast<uint8_t>(next)))) {
    next++;
  }
  if ((next == 0) || (next > 0xff)) {
    // Zero is reserved for an unsaved camera. Never wrap and reuse a live id.
    return 0;
  }
  uint16_t following = static_cast<uint16_t>(next + 1);
  if (following > 0xff) {
    following = 0xff;
  }
  const uint8_t persistedFollowing = static_cast<uint8_t>(following);
  m_Prefs.put(FURBLE_PREF_NEXT_ID, &persistedFollowing, sizeof(persistedFollowing));
  return static_cast<uint8_t>(next);
}

void CameraList::syncCameraIdFloor(const std::vector<index_entry_t> &index) {
  uint8_t persistedFloor = 1;
  if (m_Prefs.get(FURBLE_PREF_NEXT_ID, &persistedFloor, sizeof(persistedFloor))
      != sizeof(persistedFloor)) {
    persistedFloor = 1;
  }
  uint16_t floor = persistedFloor;
  for (const auto &entry : index) {
    if (entry.camera_id >= floor) {
      floor = static_cast<uint16_t>(entry.camera_id + 1);
    }
  }
  if (floor == 0) {
    floor = 1;
  }
  if (floor > 0xff) {
    floor = 0xff;
  }
  const uint8_t persistedFloorValue = static_cast<uint8_t>(floor);
  m_Prefs.put(FURBLE_PREF_NEXT_ID, &persistedFloorValue, sizeof(persistedFloorValue));
}

void CameraList::save_index(std::vector<CameraList::index_entry_t> &index) {
  if (index.size() > 0) {
    std::vector<CameraListProtocol::IndexEntry> encoded;
    encoded.reserve(index.size());
    for (const auto &entry : index) {
      CameraListProtocol::IndexEntry item = {};
      memcpy(item.name, entry.name, sizeof(item.name));
      item.type = static_cast<uint32_t>(entry.type);
      item.camera_id = entry.camera_id;
      encoded.push_back(item);
    }

    std::vector<uint8_t> bytes;
    if (CameraListProtocol::encodeIndex(encoded, bytes)) {
      m_Prefs.put(FURBLE_PREF_INDEX, bytes.data(), bytes.size());
      const uint8_t format = FURBLE_INDEX_FORMAT_CURRENT;
      m_Prefs.put(FURBLE_PREF_INDEX_FORMAT, &format, sizeof(format));
      const uint32_t checksum = CameraListProtocol::indexChecksum(bytes.data(), bytes.size());
      m_Prefs.put(FURBLE_PREF_INDEX_CHECKSUM, &checksum, sizeof(checksum));
    }
  } else {
    m_Prefs.remove(FURBLE_PREF_INDEX);
    m_Prefs.remove(FURBLE_PREF_INDEX_FORMAT);
    m_Prefs.remove(FURBLE_PREF_INDEX_CHECKSUM);
  }
}

std::vector<CameraList::index_entry_t> CameraList::load_index(void) {
  std::vector<index_entry_t> index;

  if (m_Prefs.isKey(FURBLE_PREF_INDEX)) {
    size_t bytes = m_Prefs.getBytesLength(FURBLE_PREF_INDEX);
    if (bytes > 0) {
      std::vector<uint8_t> buffer(bytes, 0x00);
      std::vector<CameraListProtocol::IndexEntry> decoded;
      m_Prefs.get(FURBLE_PREF_INDEX, buffer.data(), bytes);
      uint8_t format = 0;
      const bool hasFormat =
          m_Prefs.get(FURBLE_PREF_INDEX_FORMAT, &format, sizeof(format)) == sizeof(format);
      const auto indexFormat = hasFormat ? CameraListProtocol::IndexFormat::CURRENT
                                         : CameraListProtocol::IndexFormat::LEGACY;
      uint32_t storedChecksum = 0;
      const bool checksumValid =
          m_Prefs.get(FURBLE_PREF_INDEX_CHECKSUM, &storedChecksum, sizeof(storedChecksum))
              == sizeof(storedChecksum)
          && storedChecksum == CameraListProtocol::indexChecksum(buffer.data(), bytes);
      const bool integrityValid = !hasFormat || checksumValid;
      if ((!hasFormat || format == FURBLE_INDEX_FORMAT_CURRENT) && integrityValid
          && CameraListProtocol::decodeIndex(buffer.data(), bytes, indexFormat, decoded)) {
        // A blob written before ids existed decodes with camera_id zero. Give
        // those entries stable ids in memory so an upgraded device exposes them
        // immediately, without losing any saved camera.
        CameraListProtocol::assignCameraIds(decoded);
        ESP_LOGI(LOG_TAG, "Index entries: %d", decoded.size());
        for (const auto &item : decoded) {
          index_entry_t entry = {};
          memcpy(entry.name, item.name, sizeof(entry.name));
          entry.type = static_cast<Camera::Type>(item.type);
          entry.camera_id = item.camera_id;
          ESP_LOGI(LOG_TAG, "Loading index entry: %s", entry.name);
          index.push_back(entry);
        }
      }
    }
  }

  return index;
}

void CameraList::add_index(std::vector<CameraList::index_entry_t> &index, index_entry_t &entry) {
  bool exists = false;
  for (auto &i : index) {
    ESP_LOGD(LOG_TAG, "%s : %s", i.name, entry.name);
    if (strcmp(i.name, entry.name) == 0) {
      ESP_LOGI(LOG_TAG, "Overwriting existing entry: %s", entry.name);
      i = entry;
      exists = true;
      break;
    }
  }

  if (!exists) {
    ESP_LOGI(LOG_TAG, "Adding new entry: %s", entry.name);
    index.push_back(entry);
  }
}

void CameraList::save(const Furble::Camera *camera) {
  m_Prefs.begin(FURBLE_STR, false);
  std::vector<index_entry_t> index = load_index();
  // Persist the migrated ids into the counter floor before adding anything.
  syncCameraIdFloor(index);

  index_entry_t entry;
  fillSaveEntry(entry, camera);

  // Re-saving a known camera keeps its id. A new camera gets a fresh one.
  for (const auto &existing : index) {
    if (strcmp(existing.name, entry.name) == 0) {
      entry.camera_id = existing.camera_id;
      break;
    }
  }
  if (entry.camera_id == 0) {
    entry.camera_id = allocateCameraId(index);
  }

  add_index(index, entry);

  size_t dbytes = camera->getSerialisedBytes();
  std::vector<uint8_t> dbuffer(dbytes, 0);
  if (camera->serialise(dbuffer.data(), dbytes)) {
    // Store the entry and the index if serialisation succeeds
    m_Prefs.put(entry.name, dbuffer.data(), dbytes);
    ESP_LOGI(LOG_TAG, "Saved %s", entry.name);
    save_index(index);
    ESP_LOGI(LOG_TAG, "Index entries: %d", index.size());
  }

  m_Prefs.end();
}

void CameraList::remove(Furble::Camera *camera) {
  m_Prefs.begin(FURBLE_STR, false);
  std::vector<index_entry_t> index = load_index();
  // Lock the counter floor before dropping an entry so the deleted id, even if
  // it was the highest, is never handed back to a future camera.
  syncCameraIdFloor(index);

  index_entry_t entry;
  fillSaveEntry(entry, camera);

  size_t i = 0;
  for (i = 0; i < index.size(); i++) {
    if (strcmp(index[i].name, entry.name) == 0) {
      ESP_LOGI(LOG_TAG, "Deleting: %s", entry.name);
      index.erase(index.begin() + i);
      break;
    }
  }

  m_Prefs.remove(entry.name);
  save_index(index);

  m_Prefs.end();

  // delete bond whether needed or not
  NimBLEDevice::deleteBond(camera->getAddress());
}

/**
 * Load the list of saved cameras.
 *
 * The Arduino-ESP32 NVS library does not expose an entry iterator even though
 * the underlying library supports it. We work around this by managing a simple
 * index with a known name and storing target devices in separate entries.
 */
void CameraList::load(void) {
  m_Prefs.begin(FURBLE_STR, true);
  m_ConnectList.clear();
  std::vector<index_entry_t> index = load_index();
  for (const auto &i : index) {
    size_t dbytes = m_Prefs.getBytesLength(i.name);
    if (dbytes == 0) {
      continue;
    }
    std::vector<uint8_t> dbuffer(dbytes, 0);
    m_Prefs.get(i.name, dbuffer.data(), dbytes);

    switch (i.type) {
      case Camera::Type::FUJIFILM_BASIC:
        m_ConnectList.push_back(std::make_shared<Furble::FujifilmBasic>(
            static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::CANON_EOS_SMART:
        m_ConnectList.push_back(std::make_shared<Furble::CanonEOSSmart>(
            static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::CANON_EOS_REMOTE:
        m_ConnectList.push_back(std::make_shared<Furble::CanonEOSRemote>(
            static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::MOBILE_DEVICE:
        ESP_LOGW(FURBLE_STR, "MobileDevice support has been removed.");
        break;
      case Camera::Type::FAUXNY:
        m_ConnectList.push_back(
            std::make_shared<Furble::FauxNY>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::NIKON:
        m_ConnectList.push_back(
            std::make_shared<Furble::Nikon>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::SONY:
        m_ConnectList.push_back(
            std::make_shared<Furble::Sony>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::RICOH:
        m_ConnectList.push_back(
            std::make_shared<Furble::Ricoh>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::FUJIFILM_SECURE:
        m_ConnectList.push_back(std::make_shared<Furble::FujifilmSecure>(
            static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::PANASONIC_LUMIX:
        m_ConnectList.push_back(
            std::make_unique<Furble::Lumix>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::DJI_OSMO:
        m_ConnectList.push_back(
            std::make_unique<Furble::DJIOsmo>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
    }
  }
  m_Prefs.end();
}

size_t CameraList::getSaveCount(void) {
  m_Prefs.begin(FURBLE_STR, false);
  auto index = load_index();
  m_Prefs.end();

  return index.size();
}

uint8_t CameraList::getCameraId(const Furble::Camera *camera) {
  if (camera == nullptr) {
    return 0;
  }

  index_entry_t entry;
  fillSaveEntry(entry, camera);

  m_Prefs.begin(FURBLE_STR, true);
  std::vector<index_entry_t> index = load_index();
  m_Prefs.end();

  for (const auto &existing : index) {
    if (strcmp(existing.name, entry.name) == 0) {
      return existing.camera_id;
    }
  }
  return 0;
}

size_t CameraList::size(void) {
  return m_ConnectList.size();
}

void CameraList::clear(void) {
  m_ConnectList.clear();
}

std::shared_ptr<Furble::Camera> CameraList::last(void) {
  return m_ConnectList.back();
}

std::shared_ptr<Furble::Camera> CameraList::get(size_t n) {
  return m_ConnectList[n];
}

bool CameraList::match(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice == nullptr) {
    return false;
  }

  // Ensure we only match one instance of each camera by address.
  const NimBLEAddress addr = pDevice->getAddress();
  for (auto &c : m_ConnectList) {
    if (c->getAddress() == addr)
      return false;  // already matched
  }

  if (FujifilmBasic::matches(pDevice)) {
    m_ConnectList.push_back(std::make_shared<Furble::FujifilmBasic>(pDevice));
    return true;
  } else if (CanonEOSSmart::matches(pDevice)) {
    m_ConnectList.push_back(std::make_shared<Furble::CanonEOSSmart>(pDevice));
    return true;
  } else if (CanonEOSRemote::matches(pDevice)) {
    m_ConnectList.push_back(std::make_shared<Furble::CanonEOSRemote>(pDevice));
    return true;
  } else if (Nikon::matches(pDevice)) {
    m_ConnectList.push_back(std::make_shared<Furble::Nikon>(pDevice));
    return true;
  } else if (Ricoh::matches(pDevice)) {
    m_ConnectList.push_back(std::make_shared<Furble::Ricoh>(pDevice));
    return true;
  } else if (Sony::matches(pDevice)) {
    m_ConnectList.push_back(std::make_shared<Furble::Sony>(pDevice));
    return true;
  } else if (FujifilmSecure::matches(pDevice)) {
    m_ConnectList.push_back(std::make_shared<Furble::FujifilmSecure>(pDevice));
    return true;
  } else if (Lumix::matches(pDevice)) {
    m_ConnectList.push_back(std::make_unique<Furble::Lumix>(pDevice));
    return true;
  } else if (DJIOsmo::matches(pDevice)) {
    m_ConnectList.push_back(std::make_unique<Furble::DJIOsmo>(pDevice));
    return true;
  }

  return false;
}

void CameraList::addFauxNY(void) {
  m_ConnectList.push_back(std::make_shared<Furble::FauxNY>());
}

}  // namespace Furble
