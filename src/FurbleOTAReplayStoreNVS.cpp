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

NvsReplayJournalBackend::NvsReplayJournalBackend()
    : m_Handle(0), m_Started(false), m_Namespace {} {}

NvsReplayJournalBackend::~NvsReplayJournalBackend() {
  end();
}

bool NvsReplayJournalBackend::begin(const char *namespaceName) {
  if (m_Started || (namespaceName == nullptr) || (std::strlen(namespaceName) == 0)
      || (std::strlen(namespaceName) >= sizeof(m_Namespace))) {
    return false;
  }
  nvs_handle_t handle = 0;
  if (nvs_open(namespaceName, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  m_Handle = static_cast<uint32_t>(handle);
  std::strncpy(m_Namespace, namespaceName, sizeof(m_Namespace));
  m_Namespace[sizeof(m_Namespace) - 1] = '\0';
  m_Started = true;
  return true;
}

void NvsReplayJournalBackend::end() {
  if (m_Started) {
    nvs_close(static_cast<nvs_handle_t>(m_Handle));
    m_Handle = 0;
    m_Namespace[0] = '\0';
    m_Started = false;
  }
}

ReplayJournalBackend::ReadResult NvsReplayJournalBackend::read(uint8_t slot,
                                                               uint8_t *bytes,
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
  return nvs_get_blob(handle, slotKey(slot), bytes, &storedLength) == ESP_OK ? ReadResult::Ok
                                                                             : ReadResult::Failed;
}

bool NvsReplayJournalBackend::write(uint8_t slot, const uint8_t *bytes, size_t length) {
  if (!m_Started || (slot >= JournalReplayStore::SLOT_COUNT) || (bytes == nullptr)
      || (length != JournalReplayStore::RECORD_BYTES)) {
    return false;
  }
  const nvs_handle_t handle = static_cast<nvs_handle_t>(m_Handle);
  if (nvs_set_blob(handle, slotKey(slot), bytes, length) == ESP_OK
      && nvs_commit(handle) == ESP_OK) {
    return true;
  }

  // NVS has no two-key transaction. Reopen after any failed write so a later
  // operation cannot mistake an uncommitted value on this handle for
  // persisted journal state. A commit may have persisted a complete or torn
  // blob before reporting failure. The CRC and other slot decide recovery.
  nvs_close(handle);
  m_Handle = 0;
  nvs_handle_t reopened = 0;
  if (nvs_open(m_Namespace, NVS_READWRITE, &reopened) != ESP_OK) {
    m_Namespace[0] = '\0';
    m_Started = false;
    return false;
  }
  m_Handle = static_cast<uint32_t>(reopened);
  return false;
}

}  // namespace OTA
}  // namespace Furble

#endif
