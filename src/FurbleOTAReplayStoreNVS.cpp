#include "FurbleOTAReplayStore.h"

#if defined(ESP_PLATFORM)

#include <cstring>

#include "nvs.h"

namespace Furble {
namespace OTA {

namespace {

const char *slotKey(uint8_t slot) {
  return slot == 0 ? "furble_slot0" : "furble_slot1";
}

}  // namespace

NvsReplayJournalBackend::NvsReplayJournalBackend() : m_Handle(0), m_Started(false) {}

NvsReplayJournalBackend::~NvsReplayJournalBackend() {
  end();
}

bool NvsReplayJournalBackend::begin(const char *namespaceName) {
  if (m_Started || (namespaceName == nullptr)) {
    return false;
  }
  nvs_handle_t handle = 0;
  if (nvs_open(namespaceName, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  m_Handle = static_cast<uint32_t>(handle);
  m_Started = true;
  return true;
}

void NvsReplayJournalBackend::end() {
  if (m_Started) {
    nvs_close(static_cast<nvs_handle_t>(m_Handle));
    m_Handle = 0;
    m_Started = false;
  }
}

ReplayJournalBackend::ReadResult NvsReplayJournalBackend::read(uint8_t slot, uint8_t *bytes,
                                                               size_t length) {
  if (!m_Started || (slot >= JournalReplayStore::SLOT_COUNT) || (bytes == nullptr)
      || (length != JournalReplayStore::RECORD_BYTES)) {
    return ReadResult::Failed;
  }
  size_t storedLength = 0;
  const nvs_handle_t handle = static_cast<nvs_handle_t>(m_Handle);
  esp_err_t error = nvs_get_blob(handle, slotKey(slot), nullptr, &storedLength);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    return ReadResult::Missing;
  }
  if (error != ESP_OK) {
    return ReadResult::Failed;
  }
  if (storedLength != length) {
    // A present blob with the wrong size is corruption, not an I/O failure.
    // Return it as an invalid record so the journal can fall back to its other
    // slot. NVS cannot safely decode an oversized blob into this fixed buffer.
    std::memset(bytes, 0, length);
    return ReadResult::Ok;
  }
  return nvs_get_blob(handle, slotKey(slot), bytes, &storedLength) == ESP_OK
             ? ReadResult::Ok
             : ReadResult::Failed;
}

bool NvsReplayJournalBackend::write(uint8_t slot, const uint8_t *bytes, size_t length) {
  if (!m_Started || (slot >= JournalReplayStore::SLOT_COUNT) || (bytes == nullptr)
      || (length != JournalReplayStore::RECORD_BYTES)) {
    return false;
  }
  return nvs_set_blob(static_cast<nvs_handle_t>(m_Handle), slotKey(slot), bytes, length) == ESP_OK;
}

bool NvsReplayJournalBackend::commit() {
  return m_Started && (nvs_commit(static_cast<nvs_handle_t>(m_Handle)) == ESP_OK);
}

}  // namespace OTA
}  // namespace Furble

#endif
