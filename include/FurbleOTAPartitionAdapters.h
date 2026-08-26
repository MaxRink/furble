#ifndef FURBLE_OTA_PARTITION_ADAPTERS_H
#define FURBLE_OTA_PARTITION_ADAPTERS_H

#include "FurbleOTAPartitionSink.h"

namespace Furble {
namespace OTA {

/** ESP-IDF inactive-app-partition implementation. */
class EspIdfPartitionTarget final: public PartitionTarget {
 public:
  bool begin(uint32_t imageSize, uint32_t partitionSize) override;
  size_t write(uint32_t offset, const uint8_t *data, size_t length) override;
  bool matches(uint32_t offset, const uint8_t *data, size_t length) override;
  bool end() override;
  bool activate() override;
  void abort() override;

 private:
  const void *m_Partition = nullptr;
  uint32_t m_Handle = 0;
  uint32_t m_ImageSize = 0;
  uint32_t m_NextOffset = 0;
  bool m_Started = false;
  bool m_Ended = false;
};

/** Arduino-ESP32 inactive-app-partition implementation. */
class ArduinoPartitionTarget final: public PartitionTarget {
 public:
  bool begin(uint32_t imageSize, uint32_t partitionSize) override;
  size_t write(uint32_t offset, const uint8_t *data, size_t length) override;
  bool matches(uint32_t offset, const uint8_t *data, size_t length) override;
  bool end() override;
  bool activate() override;
  void abort() override;

 private:
  const void *m_Partition = nullptr;
  uint32_t m_Handle = 0;
  uint32_t m_ImageSize = 0;
  uint32_t m_NextOffset = 0;
  bool m_Started = false;
  bool m_Ended = false;
};

}  // namespace OTA
}  // namespace Furble

#endif
