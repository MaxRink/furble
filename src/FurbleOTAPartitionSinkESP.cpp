#include "FurbleOTAPartitionAdapters.h"

#if defined(ESP_PLATFORM)

#include <array>

#include "esp_ota_ops.h"
#include "esp_partition.h"

namespace Furble {
namespace OTA {

namespace {

const esp_partition_t *partition(const void *value) {
  return static_cast<const esp_partition_t *>(value);
}

}  // namespace

bool EspIdfPartitionTarget::begin(uint32_t imageSize, uint32_t partitionSize) {
  abort();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  if ((next == nullptr) || (next->type != ESP_PARTITION_TYPE_APP)
      || (next->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0)
      || (next->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_MAX) || (next->size != partitionSize)
      || (imageSize == 0) || (imageSize > next->size)) {
    return false;
  }
  esp_ota_handle_t handle = 0;
  if (esp_ota_begin(next, imageSize, &handle) != ESP_OK) {
    return false;
  }
  m_Partition = next;
  m_Handle = static_cast<uint32_t>(handle);
  m_ImageSize = imageSize;
  m_NextOffset = 0;
  m_Started = true;
  m_Ended = false;
  return true;
}

size_t EspIdfPartitionTarget::write(uint32_t offset, const uint8_t *data, size_t length) {
  if (!m_Started || m_Ended || (data == nullptr) || (length == 0) || (offset != m_NextOffset)
      || (offset > m_ImageSize) || (length > (m_ImageSize - offset))) {
    return 0;
  }
  if (esp_ota_write(static_cast<esp_ota_handle_t>(m_Handle), data, length) != ESP_OK) {
    return 0;
  }
  m_NextOffset += static_cast<uint32_t>(length);
  return length;
}

bool EspIdfPartitionTarget::matches(uint32_t offset, const uint8_t *data, size_t length) {
  if (!m_Started || m_Ended || (data == nullptr) || (length == 0) || (offset > m_NextOffset)
      || (length > (m_NextOffset - offset))) {
    return false;
  }
  std::array<uint8_t, 256> buffer {};
  size_t compared = 0;
  while (compared < length) {
    const size_t count = (length - compared) < buffer.size() ? length - compared : buffer.size();
    if (esp_partition_read(partition(m_Partition), offset + compared, buffer.data(), count)
        != ESP_OK) {
      return false;
    }
    for (size_t index = 0; index < count; index++) {
      if (buffer[index] != data[compared + index]) {
        return false;
      }
    }
    compared += count;
  }
  return true;
}

bool EspIdfPartitionTarget::end() {
  if (!m_Started || m_Ended || (m_NextOffset != m_ImageSize)) {
    return false;
  }
  const esp_err_t result = esp_ota_end(static_cast<esp_ota_handle_t>(m_Handle));
  if (result != ESP_OK) {
    esp_ota_abort(static_cast<esp_ota_handle_t>(m_Handle));
    m_Partition = nullptr;
    m_Handle = 0;
    m_ImageSize = 0;
    m_NextOffset = 0;
    m_Started = false;
    m_Ended = false;
    return false;
  }
  m_Handle = 0;
  m_Ended = true;
  return true;
}

bool EspIdfPartitionTarget::activate() {
  return m_Started && m_Ended && (partition(m_Partition) != nullptr)
         && (esp_ota_set_boot_partition(partition(m_Partition)) == ESP_OK);
}

void EspIdfPartitionTarget::abort() {
  if (m_Started && !m_Ended) {
    esp_ota_abort(static_cast<esp_ota_handle_t>(m_Handle));
  }
  m_Partition = nullptr;
  m_Handle = 0;
  m_ImageSize = 0;
  m_NextOffset = 0;
  m_Started = false;
  m_Ended = false;
}

}  // namespace OTA
}  // namespace Furble

#endif
