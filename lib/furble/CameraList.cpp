#include <NimBLEAdvertisedDevice.h>
#include <Preferences.h>

#include <algorithm>
#include <cstring>

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
// Monotonic camera id allocator. Persisted so deleting the highest id does not
// hand that id straight back to the next saved camera.
#define FURBLE_PREF_NEXT_ID "index_next"

namespace Furble {

std::vector<std::shared_ptr<Furble::Camera>> CameraList::m_ConnectList;
std::map<std::string, uint8_t> CameraList::m_CameraIds;
std::mutex CameraList::m_Mutex;
Preferences CameraList::m_Prefs;

void CameraList::fillSaveEntry(index_entry_t &entry, const Camera *camera) {
  const auto key = CameraListProtocol::addressKey(static_cast<uint64_t>(camera->getAddress()));
  snprintf(entry.name, sizeof(entry.name), "%s", key.c_str());
  entry.type = camera->getType();
  entry.camera_id = CameraListProtocol::INDEX_ID_INVALID;
}

bool CameraList::assignCameraIds(std::vector<CameraList::index_entry_t> &index) {
  bool assigned = false;

  uint8_t next = 1;
  if (m_Prefs.getBytesLength(FURBLE_PREF_NEXT_ID) == sizeof(next)) {
    m_Prefs.get(FURBLE_PREF_NEXT_ID, &next, sizeof(next));
  }
  if ((next == CameraListProtocol::INDEX_ID_INVALID)
      || (next == CameraListProtocol::INDEX_ID_ALL)) {
    next = 1;
  }

  for (auto &entry : index) {
    if (entry.camera_id != CameraListProtocol::INDEX_ID_INVALID) {
      continue;
    }

    // Ids run 1 to 254: zero means unassigned and 0xff means all cameras on the
    // companion wire. Walk forward from the counter so an id is only reused
    // once the whole range has been handed out.
    for (unsigned int step = 0; step < 254; step++) {
      const uint8_t candidate = static_cast<uint8_t>((next - 1 + step) % 254) + 1;
      const bool taken = std::any_of(index.begin(), index.end(), [candidate](const auto &other) {
        return other.camera_id == candidate;
      });
      if (taken) {
        continue;
      }
      entry.camera_id = candidate;
      next = static_cast<uint8_t>(candidate % 254) + 1;
      assigned = true;
      break;
    }
  }

  if (assigned) {
    m_Prefs.put(FURBLE_PREF_NEXT_ID, &next, sizeof(next));
  }

  return assigned;
}

void CameraList::publishCameraIds(const std::vector<CameraList::index_entry_t> &index) {
  std::map<std::string, uint8_t> ids;
  for (const auto &entry : index) {
    ids[std::string(entry.name, strnlen(entry.name, sizeof(entry.name)))] = entry.camera_id;
  }

  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_CameraIds = std::move(ids);
}

uint8_t CameraList::getCameraId(const Furble::Camera *camera) {
  if (camera == nullptr) {
    return CameraListProtocol::INDEX_ID_INVALID;
  }

  const auto key = CameraListProtocol::addressKey(static_cast<uint64_t>(camera->getAddress()));
  const std::lock_guard<std::mutex> lock(m_Mutex);
  const auto found = m_CameraIds.find(key);
  return (found == m_CameraIds.end()) ? CameraListProtocol::INDEX_ID_INVALID : found->second;
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
    }
  } else {
    m_Prefs.remove(FURBLE_PREF_INDEX);
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
      if (CameraListProtocol::decodeIndex(buffer.data(), bytes, decoded)) {
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
      // Re-saving a camera keeps the id the companion already knows it by.
      const uint8_t camera_id = i.camera_id;
      i = entry;
      i.camera_id = camera_id;
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

  index_entry_t entry;
  fillSaveEntry(entry, camera);

  add_index(index, entry);
  assignCameraIds(index);

  size_t dbytes = camera->getSerialisedBytes();
  std::vector<uint8_t> dbuffer(dbytes, 0);
  if (camera->serialise(dbuffer.data(), dbytes)) {
    // Store the entry and the index if serialisation succeeds
    m_Prefs.put(entry.name, dbuffer.data(), dbytes);
    ESP_LOGI(LOG_TAG, "Saved %s", entry.name);
    save_index(index);
    ESP_LOGI(LOG_TAG, "Index entries: %d", index.size());
    publishCameraIds(index);
  }

  m_Prefs.end();
}

void CameraList::remove(Furble::Camera *camera) {
  m_Prefs.begin(FURBLE_STR, false);
  std::vector<index_entry_t> index = load_index();

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
  publishCameraIds(index);

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
  // Opened for writing: a v1 index is migrated to v2 here, which assigns and
  // persists the stable camera ids exactly once.
  m_Prefs.begin(FURBLE_STR, false);
  std::vector<index_entry_t> index = load_index();
  if (assignCameraIds(index)) {
    ESP_LOGI(LOG_TAG, "Migrated camera index to stable ids");
    save_index(index);
  }
  publishCameraIds(index);

  const std::lock_guard<std::mutex> lock(m_Mutex);

  // Carry the multi-connect selection across the rebuild. Every Camera object
  // is replaced below, so without this any reload between selecting cameras and
  // connecting silently drops the selection.
  std::map<std::string, bool> selection;
  for (const auto &camera : m_ConnectList) {
    selection[CameraListProtocol::addressKey(static_cast<uint64_t>(camera->getAddress()))] =
        camera->isActive();
  }

  m_ConnectList.clear();
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

  for (const auto &camera : m_ConnectList) {
    const auto found =
        selection.find(CameraListProtocol::addressKey(static_cast<uint64_t>(camera->getAddress())));
    if ((found != selection.end()) && found->second) {
      camera->setActive(true);
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

size_t CameraList::size(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_ConnectList.size();
}

void CameraList::clear(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_ConnectList.clear();
}

std::shared_ptr<Furble::Camera> CameraList::last(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_ConnectList.back();
}

std::shared_ptr<Furble::Camera> CameraList::get(size_t n) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_ConnectList[n];
}

std::vector<std::shared_ptr<Furble::Camera>> CameraList::snapshot(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_ConnectList;
}

bool CameraList::match(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice == nullptr) {
    return false;
  }

  const std::lock_guard<std::mutex> lock(m_Mutex);

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
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_ConnectList.push_back(std::make_shared<Furble::FauxNY>());
}

}  // namespace Furble
