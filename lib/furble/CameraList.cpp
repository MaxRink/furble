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
#define FURBLE_PREF_SLOT_A_BLOB "slot_a_blob"
#define FURBLE_PREF_SLOT_A_HEADER "slot_a_hdr"
#define FURBLE_PREF_SLOT_A_CRC "slot_a_crc"
#define FURBLE_PREF_SLOT_A_COMMIT "slot_a_cmt"
#define FURBLE_PREF_SLOT_B_BLOB "slot_b_blob"
#define FURBLE_PREF_SLOT_B_HEADER "slot_b_hdr"
#define FURBLE_PREF_SLOT_B_CRC "slot_b_crc"
#define FURBLE_PREF_SLOT_B_COMMIT "slot_b_cmt"
#define FURBLE_INDEX_SLOT_MAGIC 0x46524C49U
// Monotonic id allocator, persisted alongside the index in the same namespace.
#define FURBLE_PREF_NEXT_ID "cam_next_id"
#define FURBLE_PREF_PENDING_BLOB "pending_blob"
#define FURBLE_PREF_RECLAIM "reclaim"
#define FURBLE_RECLAIM_SLOTS 2
#define FURBLE_RECLAIM_NAME_BYTES 16

namespace Furble {

static_assert(Preferences::validNvsKey(FURBLE_PREF_INDEX));
static_assert(Preferences::validNvsKey(FURBLE_PREF_INDEX_FORMAT));
static_assert(Preferences::validNvsKey(FURBLE_PREF_INDEX_CHECKSUM));
static_assert(Preferences::validNvsKey(FURBLE_PREF_SLOT_A_BLOB));
static_assert(Preferences::validNvsKey(FURBLE_PREF_SLOT_A_HEADER));
static_assert(Preferences::validNvsKey(FURBLE_PREF_SLOT_A_CRC));
static_assert(Preferences::validNvsKey(FURBLE_PREF_SLOT_A_COMMIT));
static_assert(Preferences::validNvsKey(FURBLE_PREF_SLOT_B_BLOB));
static_assert(Preferences::validNvsKey(FURBLE_PREF_SLOT_B_HEADER));
static_assert(Preferences::validNvsKey(FURBLE_PREF_SLOT_B_CRC));
static_assert(Preferences::validNvsKey(FURBLE_PREF_SLOT_B_COMMIT));
static_assert(Preferences::validNvsKey(FURBLE_PREF_NEXT_ID));
static_assert(Preferences::validNvsKey(FURBLE_PREF_PENDING_BLOB));
static_assert(Preferences::validNvsKey(FURBLE_PREF_RECLAIM));

std::mutex CameraList::m_Mutex;

namespace {

struct __attribute__((packed)) index_slot_header_t {
  uint32_t magic;
  uint32_t generation;
  uint32_t bytes;
  uint8_t format;
  uint8_t reserved[3];
};

static_assert(sizeof(index_slot_header_t) == 16, "index slot header layout changed");

struct index_slot_keys_t {
  const char *blob;
  const char *header;
  const char *crc;
  const char *commit;
};

struct reclaim_queue_t {
  char names[FURBLE_RECLAIM_SLOTS][FURBLE_RECLAIM_NAME_BYTES];
};

struct pending_blob_t {
  char name[FURBLE_RECLAIM_NAME_BYTES];
};

static_assert(sizeof(pending_blob_t) == FURBLE_RECLAIM_NAME_BYTES, "pending blob layout changed");

static_assert(sizeof(reclaim_queue_t) == 32, "reclaim queue layout changed");

const index_slot_keys_t &slotKeys(size_t slot) {
  static const index_slot_keys_t keys[] = {
      {FURBLE_PREF_SLOT_A_BLOB, FURBLE_PREF_SLOT_A_HEADER, FURBLE_PREF_SLOT_A_CRC,
       FURBLE_PREF_SLOT_A_COMMIT},
      {FURBLE_PREF_SLOT_B_BLOB, FURBLE_PREF_SLOT_B_HEADER, FURBLE_PREF_SLOT_B_CRC,
       FURBLE_PREF_SLOT_B_COMMIT},
  };
  return keys[slot & 1U];
}

enum class slot_state_t : uint8_t { ABSENT, INVALID, VALID };

// RFC1982 serial-number arithmetic: equal and half-range values are not
// ordered and must be treated as ambiguous rather than guessed.
bool generationNewer(uint32_t lhs, uint32_t rhs) {
  const uint32_t delta = lhs - rhs;
  return delta != 0 && delta < 0x80000000U;
}

slot_state_t readSlot(Preferences &prefs,
                      size_t slot,
                      std::vector<uint8_t> &bytes,
                      uint32_t &generation) {
  const auto &keys = slotKeys(slot);
  const bool present = prefs.isKey(keys.blob) || prefs.isKey(keys.header) || prefs.isKey(keys.crc)
                       || prefs.isKey(keys.commit);
  if (!present) {
    return slot_state_t::ABSENT;
  }

  index_slot_header_t header = {};
  uint32_t crc = 0;
  uint32_t commit = 0;
  const size_t blobBytes = prefs.getBytesLength(keys.blob);
  const size_t headerRead = prefs.get(keys.header, &header, sizeof(header));
  const size_t crcRead = prefs.get(keys.crc, &crc, sizeof(crc));
  const size_t commitRead = prefs.get(keys.commit, &commit, sizeof(commit));
  if ((headerRead != sizeof(header)) || (crcRead != sizeof(crc)) || (commitRead != sizeof(commit))
      || (header.magic != FURBLE_INDEX_SLOT_MAGIC) || (header.format != FURBLE_INDEX_FORMAT_CURRENT)
      || (commit != header.generation) || (header.bytes > blobBytes)
      || ((header.bytes == 0) ? !(blobBytes == 0 || blobBytes == 1)
                              : (header.bytes != blobBytes))) {
    return slot_state_t::INVALID;
  }

  bytes.assign(header.bytes, 0);
  if ((header.bytes > 0) && (prefs.get(keys.blob, bytes.data(), header.bytes) != header.bytes)) {
    return slot_state_t::INVALID;
  }
  if (crc != CameraListProtocol::indexChecksum(bytes.data(), bytes.size())) {
    return slot_state_t::INVALID;
  }
  generation = header.generation;
  return slot_state_t::VALID;
}

bool anySlotKey(Preferences &prefs) {
  for (size_t slot = 0; slot < 2; slot++) {
    const auto &keys = slotKeys(slot);
    if (prefs.isKey(keys.blob) || prefs.isKey(keys.header) || prefs.isKey(keys.crc)
        || prefs.isKey(keys.commit)) {
      return true;
    }
  }
  return false;
}

bool validCameraType(uint32_t type) {
  return (type >= static_cast<uint32_t>(Camera::Type::FUJIFILM_BASIC))
         && (type <= static_cast<uint32_t>(Camera::Type::DJI_OSMO));
}

bool readReclaimQueue(Preferences &prefs, std::vector<std::string> &names) {
  names.clear();
  const size_t bytes = prefs.getBytesLength(FURBLE_PREF_RECLAIM);
  if (bytes == 0) {
    return true;
  }
  if (bytes != sizeof(reclaim_queue_t)) {
    return false;
  }
  reclaim_queue_t queue = {};
  if (prefs.get(FURBLE_PREF_RECLAIM, &queue, sizeof(queue)) != sizeof(queue)) {
    return false;
  }
  for (size_t i = 0; i < FURBLE_RECLAIM_SLOTS; i++) {
    if (queue.names[i][0] == '\0') {
      continue;
    }
    if (::strnlen(queue.names[i], FURBLE_RECLAIM_NAME_BYTES) >= FURBLE_RECLAIM_NAME_BYTES
        || !Preferences::validNvsKey(queue.names[i])) {
      return false;
    }
    names.emplace_back(queue.names[i]);
  }
  return true;
}

bool writeReclaimQueue(Preferences &prefs, const std::vector<std::string> &names) {
  if (names.empty()) {
    if (!prefs.isKey(FURBLE_PREF_RECLAIM)) {
      return true;
    }
    return prefs.remove(FURBLE_PREF_RECLAIM);
  }
  if (names.size() > FURBLE_RECLAIM_SLOTS) {
    return false;
  }
  reclaim_queue_t queue = {};
  for (size_t i = 0; i < names.size(); i++) {
    if (names[i].size() >= FURBLE_RECLAIM_NAME_BYTES
        || !Preferences::validNvsKey(names[i].c_str())) {
      return false;
    }
    std::strncpy(queue.names[i], names[i].c_str(), FURBLE_RECLAIM_NAME_BYTES - 1);
  }
  return prefs.put(FURBLE_PREF_RECLAIM, &queue, sizeof(queue)) == sizeof(queue);
}

bool slotReferences(Preferences &prefs, size_t slot, const char *name) {
  std::vector<uint8_t> bytes;
  uint32_t generation = 0;
  if (readSlot(prefs, slot, bytes, generation) != slot_state_t::VALID) {
    // An invalid slot is conservatively treated as a reference. This keeps a
    // blob available for recovery if the other slot is damaged later.
    return true;
  }
  std::vector<CameraListProtocol::IndexEntry> entries;
  if (!CameraListProtocol::decodeIndex(bytes.data(), bytes.size(),
                                       CameraListProtocol::IndexFormat::CURRENT, entries)) {
    return true;
  }
  for (const auto &entry : entries) {
    if (std::strncmp(entry.name, name, FURBLE_RECLAIM_NAME_BYTES) == 0) {
      return true;
    }
  }
  return false;
}

bool durableSlotReferences(Preferences &prefs, size_t slot, const char *name) {
  std::vector<uint8_t> bytes;
  uint32_t generation = 0;
  if (readSlot(prefs, slot, bytes, generation) != slot_state_t::VALID) {
    return false;
  }
  std::vector<CameraListProtocol::IndexEntry> entries;
  if (!CameraListProtocol::decodeIndex(bytes.data(), bytes.size(),
                                       CameraListProtocol::IndexFormat::CURRENT, entries)) {
    return false;
  }
  return std::any_of(entries.begin(), entries.end(), [name](const auto &entry) {
    return std::strncmp(entry.name, name, FURBLE_RECLAIM_NAME_BYTES) == 0;
  });
}

}  // namespace

std::vector<std::shared_ptr<Furble::Camera>> CameraList::m_ConnectList;
Preferences CameraList::m_Prefs;
std::mutex CameraList::m_PrefsMutex;

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
  while ((next <= 0xfe) && ((next == 0) || containsCameraId(index, static_cast<uint8_t>(next)))) {
    next++;
  }
  if ((next == 0) || (next > 0xfe)) {
    // Zero is reserved for an unsaved camera and 0xff is the all-cameras
    // protocol marker. Never wrap or reuse a live id.
    return 0;
  }
  uint16_t following = static_cast<uint16_t>(next + 1);
  if (following > 0xfe) {
    following = 0xfe;
  }
  const uint8_t persistedFollowing = static_cast<uint8_t>(following);
  if (m_Prefs.put(FURBLE_PREF_NEXT_ID, &persistedFollowing, sizeof(persistedFollowing))
      != sizeof(persistedFollowing)) {
    return 0;
  }
  return static_cast<uint8_t>(next);
}

bool CameraList::syncCameraIdFloor(const std::vector<index_entry_t> &index) {
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
  if (persistedFloorValue == persistedFloor) {
    return true;
  }
  return m_Prefs.put(FURBLE_PREF_NEXT_ID, &persistedFloorValue, sizeof(persistedFloorValue))
         == sizeof(persistedFloorValue);
}

bool CameraList::writePendingBlob(const char *name) {
  if (name == nullptr || !Preferences::validNvsKey(name)) {
    return false;
  }
  pending_blob_t pending = {};
  std::strncpy(pending.name, name, sizeof(pending.name) - 1);
  return m_Prefs.put(FURBLE_PREF_PENDING_BLOB, &pending, sizeof(pending)) == sizeof(pending);
}

bool CameraList::pendingBlobName(std::string &name) {
  name.clear();
  const size_t bytes = m_Prefs.getBytesLength(FURBLE_PREF_PENDING_BLOB);
  if (bytes == 0) {
    return true;
  }
  if (bytes != sizeof(pending_blob_t)) {
    return false;
  }
  pending_blob_t pending = {};
  if (m_Prefs.get(FURBLE_PREF_PENDING_BLOB, &pending, sizeof(pending)) != sizeof(pending)
      || pending.name[0] == '\0'
      || ::strnlen(pending.name, sizeof(pending.name)) >= sizeof(pending.name)
      || !Preferences::validNvsKey(pending.name)) {
    return false;
  }
  name.assign(pending.name);
  return true;
}

bool CameraList::clearPendingBlob(void) {
  if (!m_Prefs.isKey(FURBLE_PREF_PENDING_BLOB)) {
    return true;
  }
  return m_Prefs.remove(FURBLE_PREF_PENDING_BLOB);
}

void CameraList::recoverPendingBlob(void) {
  std::string name;
  if (!pendingBlobName(name)) {
    return;
  }
  if (name.empty()) {
    return;
  }
  if (durableSlotReferences(m_Prefs, 0, name.c_str())
      || durableSlotReferences(m_Prefs, 1, name.c_str())) {
    (void)clearPendingBlob();
    return;
  }
  // Deleting first and clearing the intent second is idempotent across a cut
  // at either boundary. A retained intent is harmless if the delete happened.
  if (m_Prefs.isKey(name.c_str()) && !m_Prefs.remove(name.c_str())) {
    return;
  }
  (void)clearPendingBlob();
}

bool CameraList::enqueueReclaim(const char *name) {
  std::vector<std::string> names;
  if (!readReclaimQueue(m_Prefs, names)) {
    return false;
  }
  if (std::find(names.begin(), names.end(), name) != names.end()) {
    return true;
  }
  if (names.size() >= FURBLE_RECLAIM_SLOTS) {
    return false;
  }
  names.emplace_back(name);
  return writeReclaimQueue(m_Prefs, names);
}

void CameraList::reclaimSafeBlobs(void) {
  std::vector<std::string> names;
  if (!readReclaimQueue(m_Prefs, names)) {
    return;
  }
  std::vector<std::string> remaining;
  bool changed = false;
  for (const auto &name : names) {
    if (slotReferences(m_Prefs, 0, name.c_str()) || slotReferences(m_Prefs, 1, name.c_str())) {
      remaining.push_back(name);
      continue;
    }
    // The journal has two durable generations excluding this key. Erasing
    // the blob is now safe; if the erase or queue update is interrupted, the
    // queue remains and the next boot retries the idempotent erase.
    if (m_Prefs.isKey(name.c_str()) && !m_Prefs.remove(name.c_str())) {
      remaining.push_back(name);
      continue;
    }
    changed = true;
  }
  if (changed) {
    (void)writeReclaimQueue(m_Prefs, remaining);
  }
}

bool CameraList::save_index(std::vector<CameraList::index_entry_t> &index) {
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
  if (!CameraListProtocol::encodeIndex(encoded, bytes)) {
    return false;
  }

  uint32_t generation[2] = {};
  bool valid[2] = {};
  for (size_t slot = 0; slot < 2; slot++) {
    std::vector<uint8_t> oldBytes;
    valid[slot] = readSlot(m_Prefs, slot, oldBytes, generation[slot]) == slot_state_t::VALID;
  }
  if (valid[0] && valid[1]
      && (generation[0] == generation[1]
          || (!generationNewer(generation[0], generation[1])
              && !generationNewer(generation[1], generation[0])))) {
    return true;
  }
  size_t active = valid[1] && (!valid[0] || generationNewer(generation[1], generation[0])) ? 1 : 0;
  const size_t target = valid[0] || valid[1] ? (active ^ 1U) : 0;
  uint32_t nextGeneration = (valid[0] || valid[1]) ? (generation[active] + 1U) : 1U;
  if (nextGeneration == 0) {
    nextGeneration = 1;
  }
  const auto &keys = slotKeys(target);

  // A one-byte marker gives empty indexes a real NVS blob operation while the
  // header still records zero payload bytes. The commit marker is always the
  // final write, so an interrupted target slot can never displace the prior
  // valid generation.
  const uint8_t emptyMarker = 0;
  if (bytes.empty()) {
    if (m_Prefs.put(keys.blob, &emptyMarker, sizeof(emptyMarker)) != sizeof(emptyMarker)) {
      return false;
    }
  } else if (m_Prefs.put(keys.blob, bytes.data(), bytes.size()) != bytes.size()) {
    return false;
  }

  index_slot_header_t header = {
      FURBLE_INDEX_SLOT_MAGIC,
      nextGeneration,
      static_cast<uint32_t>(bytes.size()),
      FURBLE_INDEX_FORMAT_CURRENT,
      {0, 0, 0}
  };
  if (m_Prefs.put(keys.header, &header, sizeof(header)) != sizeof(header)) {
    return false;
  }
  const uint32_t checksum = CameraListProtocol::indexChecksum(bytes.data(), bytes.size());
  if (m_Prefs.put(keys.crc, &checksum, sizeof(checksum)) != sizeof(checksum)) {
    return false;
  }
  if (m_Prefs.put(keys.commit, &nextGeneration, sizeof(nextGeneration)) != sizeof(nextGeneration)) {
    return false;
  }
  reclaimSafeBlobs();
  return true;
}

std::vector<CameraList::index_entry_t> CameraList::load_index(bool *migrated) {
  std::vector<index_entry_t> index;
  if (migrated != nullptr) {
    *migrated = false;
  }

  if (anySlotKey(m_Prefs)) {
    std::vector<uint8_t> selectedBytes;
    uint32_t selectedGeneration = 0;
    bool selected = false;
    for (size_t slot = 0; slot < 2; slot++) {
      std::vector<uint8_t> bytes;
      uint32_t generation = 0;
      if (readSlot(m_Prefs, slot, bytes, generation) == slot_state_t::VALID) {
        if (!selected || generationNewer(generation, selectedGeneration)) {
          selectedBytes = std::move(bytes);
          selectedGeneration = generation;
          selected = true;
        } else if (!generationNewer(selectedGeneration, generation)) {
          return index;
        }
      }
    }
    if (!selected) {
      // An interrupted first journal write may coexist with an intact legacy
      // index. Let the explicit legacy migration below recover it; without a
      // legacy index, fail closed on the corrupt/current journal state.
      if (!m_Prefs.isKey(FURBLE_PREF_INDEX)) {
        return index;
      }
    } else {
      std::vector<CameraListProtocol::IndexEntry> decoded;
      if (!CameraListProtocol::decodeIndex(selectedBytes.data(), selectedBytes.size(),
                                           CameraListProtocol::IndexFormat::CURRENT, decoded)) {
        return index;
      }
      for (size_t i = 0; i < decoded.size(); i++) {
        if (decoded[i].camera_id == 0xff) {
          return index;
        }
        for (size_t j = i + 1; j < decoded.size(); j++) {
          if ((decoded[i].camera_id != 0) && (decoded[i].camera_id == decoded[j].camera_id)) {
            return index;
          }
        }
      }
      const bool needsMigration = std::any_of(decoded.begin(), decoded.end(),
                                              [](const auto &item) { return item.camera_id == 0; });
      CameraListProtocol::assignCameraIds(decoded);
      if (std::any_of(decoded.begin(), decoded.end(), [](const auto &item) {
            return item.camera_id == 0 || item.camera_id == 0xff;
          })) {
        return index;
      }
      for (const auto &item : decoded) {
        index_entry_t entry = {};
        memcpy(entry.name, item.name, sizeof(entry.name));
        entry.type = static_cast<Camera::Type>(item.type);
        entry.camera_id = item.camera_id;
        index.push_back(entry);
      }
      if (migrated != nullptr) {
        *migrated = needsMigration;
      }
      return index;
    }
  }

  if (m_Prefs.isKey(FURBLE_PREF_INDEX)) {
    size_t bytes = m_Prefs.getBytesLength(FURBLE_PREF_INDEX);
    if (bytes > 0) {
      std::vector<uint8_t> buffer(bytes, 0x00);
      std::vector<CameraListProtocol::IndexEntry> decoded;
      if (m_Prefs.get(FURBLE_PREF_INDEX, buffer.data(), bytes) != bytes) {
        return index;
      }
      const bool formatKey = m_Prefs.isKey(FURBLE_PREF_INDEX_FORMAT);
      const bool checksumKey = m_Prefs.isKey(FURBLE_PREF_INDEX_CHECKSUM);
      // A genuinely legacy installation has neither metadata key. Any
      // partial metadata set is an interrupted current-format write and must
      // fail closed rather than falling through to legacy decoding.
      if (formatKey != checksumKey) {
        return index;
      }

      uint8_t format = 0;
      const bool formatValid =
          !formatKey
          || (m_Prefs.get(FURBLE_PREF_INDEX_FORMAT, &format, sizeof(format)) == sizeof(format));
      uint32_t storedChecksum = 0;
      const bool checksumValid =
          !checksumKey
          || ((m_Prefs.get(FURBLE_PREF_INDEX_CHECKSUM, &storedChecksum, sizeof(storedChecksum))
               == sizeof(storedChecksum))
              && storedChecksum == CameraListProtocol::indexChecksum(buffer.data(), bytes));
      const auto indexFormat = formatKey ? CameraListProtocol::IndexFormat::CURRENT
                                         : CameraListProtocol::IndexFormat::LEGACY;
      if ((!formatKey || (format == FURBLE_INDEX_FORMAT_CURRENT)) && formatValid && checksumValid
          && CameraListProtocol::decodeIndex(buffer.data(), bytes, indexFormat, decoded)) {
        if (!formatKey) {
          // The only safe metadata-free migration candidate is a real legacy
          // index whose names and types still point at stored camera records.
          // A current-format blob interrupted before metadata publication is
          // otherwise indistinguishable at the byte level and must fail closed.
          for (const auto &item : decoded) {
            if (!validCameraType(item.type) || !m_Prefs.isKey(item.name)) {
              return index;
            }
          }
        }
        // A blob written before ids existed decodes with camera_id zero. Give
        // those entries stable ids in memory so an upgraded device exposes them
        // immediately, without losing any saved camera.
        const bool needsMigration = std::any_of(
            decoded.begin(), decoded.end(), [](const auto &item) { return item.camera_id == 0; });
        CameraListProtocol::assignCameraIds(decoded);
        if (std::any_of(decoded.begin(), decoded.end(), [](const auto &item) {
              return item.camera_id == 0 || item.camera_id == 0xff;
            })) {
          return index;
        }
        ESP_LOGI(LOG_TAG, "Index entries: %d", decoded.size());
        for (const auto &item : decoded) {
          index_entry_t entry = {};
          memcpy(entry.name, item.name, sizeof(entry.name));
          entry.type = static_cast<Camera::Type>(item.type);
          entry.camera_id = item.camera_id;
          ESP_LOGI(LOG_TAG, "Loading index entry: %s", entry.name);
          index.push_back(entry);
        }
        if (migrated != nullptr) {
          *migrated = needsMigration;
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
  const std::lock_guard<std::mutex> prefsLock(m_PrefsMutex);
  if (!m_Prefs.begin(FURBLE_STR, false)) {
    return;
  }
  recoverPendingBlob();
  std::vector<index_entry_t> index = load_index();
  // Persist the migrated ids into the counter floor before adding anything.
  if (!syncCameraIdFloor(index)) {
    m_Prefs.end();
    return;
  }

  index_entry_t entry;
  fillSaveEntry(entry, camera);

  // Re-saving a known camera keeps its id. A new camera gets a fresh one.
  for (const auto &existing : index) {
    if (strcmp(existing.name, entry.name) == 0) {
      entry.camera_id = existing.camera_id;
      break;
    }
  }
  const bool newCamera = entry.camera_id == 0;
  if (newCamera) {
    entry.camera_id = allocateCameraId(index);
    if (entry.camera_id == 0) {
      m_Prefs.end();
      return;
    }
  }

  add_index(index, entry);

  size_t dbytes = camera->getSerialisedBytes();
  std::vector<uint8_t> dbuffer(dbytes, 0);
  if (camera->serialise(dbuffer.data(), dbytes)) {
    if (newCamera && !writePendingBlob(entry.name)) {
      m_Prefs.end();
      return;
    }
    // Store the entry and the index if serialisation succeeds
    if (m_Prefs.put(entry.name, dbuffer.data(), dbytes) != dbytes) {
      m_Prefs.end();
      return;
    }
    ESP_LOGI(LOG_TAG, "Saved %s", entry.name);
    if (save_index(index) && newCamera) {
      (void)clearPendingBlob();
    }
    ESP_LOGI(LOG_TAG, "Index entries: %d", index.size());
  }

  m_Prefs.end();
}

void CameraList::remove(Furble::Camera *camera) {
  const std::lock_guard<std::mutex> prefsLock(m_PrefsMutex);
  if (!m_Prefs.begin(FURBLE_STR, false)) {
    return;
  }
  std::vector<index_entry_t> index = load_index();
  // Lock the counter floor before dropping an entry so the deleted id, even if
  // it was the highest, is never handed back to a future camera.
  if (!syncCameraIdFloor(index)) {
    m_Prefs.end();
    return;
  }

  index_entry_t entry;
  fillSaveEntry(entry, camera);

  bool removed = false;
  size_t i = 0;
  for (i = 0; i < index.size(); i++) {
    if (strcmp(index[i].name, entry.name) == 0) {
      ESP_LOGI(LOG_TAG, "Deleting: %s", entry.name);
      index.erase(index.begin() + i);
      removed = true;
      break;
    }
  }

  if (!removed) {
    m_Prefs.end();
    return;
  }

  // Queue the blob before publishing the new generation. If power fails
  // before the commit, the old generation still references it and the queue
  // is harmless. A later successful publication reclaims only after both
  // durable generations exclude the queued key.
  if (!enqueueReclaim(entry.name)) {
    m_Prefs.end();
    return;
  }
  (void)save_index(index);

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
  const std::lock_guard<std::mutex> prefsLock(m_PrefsMutex);
  if (!m_Prefs.begin(FURBLE_STR, false)) {
    return;
  }
  recoverPendingBlob();
  reclaimSafeBlobs();
  bool migrated = false;
  std::vector<index_entry_t> index = load_index(&migrated);
  if (migrated) {
    // Advance the floor before publishing migrated IDs. A cut between these
    // operations can only leak unused IDs; it cannot permit a collision.
    if (!syncCameraIdFloor(index) || !save_index(index)) {
      m_Prefs.end();
      return;
    }
  }
  std::vector<std::shared_ptr<Furble::Camera>> loaded;
  for (const auto &i : index) {
    size_t dbytes = m_Prefs.getBytesLength(i.name);
    if (dbytes == 0) {
      continue;
    }
    std::vector<uint8_t> dbuffer(dbytes, 0);
    if (m_Prefs.get(i.name, dbuffer.data(), dbytes) != dbytes) {
      continue;
    }

    switch (i.type) {
      case Camera::Type::FUJIFILM_BASIC:
        loaded.push_back(std::make_shared<Furble::FujifilmBasic>(
            static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::CANON_EOS_SMART:
        loaded.push_back(std::make_shared<Furble::CanonEOSSmart>(
            static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::CANON_EOS_REMOTE:
        loaded.push_back(std::make_shared<Furble::CanonEOSRemote>(
            static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::MOBILE_DEVICE:
        ESP_LOGW(FURBLE_STR, "MobileDevice support has been removed.");
        break;
      case Camera::Type::FAUXNY:
        loaded.push_back(
            std::make_shared<Furble::FauxNY>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::NIKON:
        loaded.push_back(
            std::make_shared<Furble::Nikon>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::SONY:
        loaded.push_back(
            std::make_shared<Furble::Sony>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::RICOH:
        loaded.push_back(
            std::make_shared<Furble::Ricoh>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::FUJIFILM_SECURE:
        loaded.push_back(std::make_shared<Furble::FujifilmSecure>(
            static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::PANASONIC_LUMIX:
        loaded.push_back(
            std::make_unique<Furble::Lumix>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
      case Camera::Type::DJI_OSMO:
        loaded.push_back(
            std::make_unique<Furble::DJIOsmo>(static_cast<const void *>(dbuffer.data()), dbytes));
        break;
    }
  }
  m_Prefs.end();
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_ConnectList.swap(loaded);
  }
}

size_t CameraList::getSaveCount(void) {
  const std::lock_guard<std::mutex> prefsLock(m_PrefsMutex);
  if (!m_Prefs.begin(FURBLE_STR, false)) {
    return 0;
  }
  auto index = load_index();
  m_Prefs.end();

  return index.size();
}

uint8_t CameraList::getCameraId(const Furble::Camera *camera) {
  if (camera == nullptr) {
    return 0;
  }

  const std::lock_guard<std::mutex> prefsLock(m_PrefsMutex);
  index_entry_t entry;
  fillSaveEntry(entry, camera);

  if (!m_Prefs.begin(FURBLE_STR, true)) {
    return 0;
  }
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
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_ConnectList.size();
}

std::vector<std::shared_ptr<Furble::Camera>> CameraList::snapshot(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_ConnectList;
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

bool CameraList::match(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice == nullptr) {
    return false;
  }

  // Ensure we only match one instance of each camera by address.
  const NimBLEAddress addr = pDevice->getAddress();
  const std::lock_guard<std::mutex> lock(m_Mutex);
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
