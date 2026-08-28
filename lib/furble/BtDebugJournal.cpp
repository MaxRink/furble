#include "BtDebugJournal.h"

#include <algorithm>
#include <cstring>
#include <new>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace Furble {

namespace {
constexpr char HEX[] = "0123456789abcdef";

uint8_t hexValue(char value) {
  if (value >= '0' && value <= '9')
    return static_cast<uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<uint8_t>(value - 'a' + 10);
  if (value >= 'A' && value <= 'F')
    return static_cast<uint8_t>(value - 'A' + 10);
  return 0xff;
}

bool encodeHex(const char *source, uint8_t *destination, size_t bytes) {
  size_t sourceIndex = 0;
  for (size_t byte = 0; byte < bytes; ++byte) {
    while (source[sourceIndex] == '-' || source[sourceIndex] == ':' || source[sourceIndex] == ' ')
      ++sourceIndex;
    if (source[sourceIndex] == '\0')
      return false;
    const uint8_t high = hexValue(source[sourceIndex++]);
    if (source[sourceIndex] == '\0')
      return false;
    const uint8_t low = hexValue(source[sourceIndex++]);
    if (high == 0xff || low == 0xff)
      return false;
    destination[byte] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

size_t boundedLength(const char *source, size_t maximum) {
  if (source == nullptr)
    return 0;
  size_t length = 0;
  while (length < maximum && source[length] != '\0')
    ++length;
  return length;
}

void copyText(char *destination,
              size_t destinationBytes,
              const char *source,
              size_t sourceBytes = SIZE_MAX) {
  if (destinationBytes == 0)
    return;
  const size_t length = std::min(destinationBytes - 1, boundedLength(source, sourceBytes));
  memcpy(destination, source, length);
  destination[length] = '\0';
}

void copyUuid(char *destination,
              size_t destinationBytes,
              const uint8_t *source,
              uint8_t sourceBytes) {
  if (destinationBytes == 0 || sourceBytes == 0 || sourceBytes > 16)
    return;
  size_t output = 0;
  for (uint8_t byte = 0; byte < sourceBytes && output + 2 < destinationBytes; ++byte) {
    if (sourceBytes == 16 && (byte == 4 || byte == 6 || byte == 8 || byte == 10)) {
      if (output + 1 >= destinationBytes)
        break;
      destination[output++] = '-';
    }
    destination[output++] = HEX[source[byte] >> 4];
    destination[output++] = HEX[source[byte] & 0x0f];
  }
  destination[output] = '\0';
}

void encodeUuid(const char *source, uint8_t *destination, uint8_t &length) {
  length = 0;
  if (source == nullptr || source[0] == '\0')
    return;
  size_t hexDigits = 0;
  for (const char *cursor = source; *cursor != '\0'; ++cursor) {
    if (*cursor == '-' || *cursor == ' ')
      continue;
    if (hexValue(*cursor) == 0xff || hexDigits >= 32)
      return;
    ++hexDigits;
  }
  if ((hexDigits != 4 && hexDigits != 8 && hexDigits != 32)
      || !encodeHex(source, destination, hexDigits / 2)) {
    length = 0;
    return;
  }
  length = static_cast<uint8_t>(hexDigits / 2);
}

bool encodeAddress(const char *source, uint8_t *destination) {
  return source != nullptr && source[0] != '\0' && encodeHex(source, destination, 6);
}

void decodeAddress(char *destination, size_t destinationBytes, const uint8_t *source) {
  if (destinationBytes < 18)
    return;
  for (size_t byte = 0; byte < 6; ++byte) {
    destination[byte * 3] = HEX[source[byte] >> 4];
    destination[byte * 3 + 1] = HEX[source[byte] & 0x0f];
    if (byte != 5)
      destination[byte * 3 + 2] = ':';
  }
  destination[17] = '\0';
}
}  // namespace

const char *btGapReasonName(int reason) {
  switch (reason) {
    case 0x05:
      return "authentication-failure";
    case 0x08:
      return "connection-timeout";
    case 0x13:
      return "remote-user-terminated";
    case 0x16:
      return "local-host-terminated";
    case 0x22:
      return "ll-response-timeout";
    case 0x3d:
      return "connection-accept-timeout";
    case 0x3e:
      return "synchronous-connection-failed";
    case -1:
      return "scan-start-failed";
    default:
      return "unknown";
  }
}

BtDebugJournal &BtDebugJournal::instance() {
  static BtDebugJournal journal;
  return journal;
}

BtDebugJournal::BtDebugJournal() = default;

BtDebugJournal::~BtDebugJournal() {
  std::lock_guard<std::mutex> lock(m_Mutex);
  releaseLocked();
}

bool BtDebugJournal::allocateLocked() {
  if (m_Events != nullptr)
    return true;
#if defined(ESP_PLATFORM)
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(CONFIG_SPIRAM)
  m_Events = static_cast<BtDebugRecord *>(
      heap_caps_calloc(MAX_EVENTS, sizeof(BtDebugRecord), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  m_EventCapacity = m_Events != nullptr ? MAX_EVENTS : 0;
  if (m_Events == nullptr) {
    // Keep an S3 build useful when its PSRAM is absent or unavailable.
    m_Events = static_cast<BtDebugRecord *>(heap_caps_calloc(
        INTERNAL_EVENTS, sizeof(BtDebugRecord), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    m_EventCapacity = m_Events != nullptr ? INTERNAL_EVENTS : 0;
  }
#else
  m_Events = static_cast<BtDebugRecord *>(
      heap_caps_calloc(MAX_EVENTS, sizeof(BtDebugRecord), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  m_EventCapacity = m_Events != nullptr ? MAX_EVENTS : 0;
#endif
  if (m_Events != nullptr) {
    for (size_t index = 0; index < m_EventCapacity; ++index) {
      new (&m_Events[index]) BtDebugRecord();
    }
  }
#else
  m_Events = new (std::nothrow) BtDebugRecord[MAX_EVENTS]();
  m_EventCapacity = m_Events != nullptr ? MAX_EVENTS : 0;
#endif
  return m_Events != nullptr;
}

void BtDebugJournal::releaseLocked() {
  if (m_Events == nullptr)
    return;
#if defined(ESP_PLATFORM)
  for (size_t index = 0; index < m_EventCapacity; ++index) {
    m_Events[index].~BtDebugRecord();
  }
  heap_caps_free(m_Events);
#else
  delete[] m_Events;
#endif
  m_Events = nullptr;
  m_EventCapacity = 0;
}

bool BtDebugJournal::setEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(m_Mutex);
  if (enabled && !m_Enabled) {
    if (!allocateLocked())
      return false;
    ++m_SessionId;
    if (m_SessionId == 0)
      ++m_SessionId;
    m_NextAttemptId = 0;
    m_DroppedCount = 0;
  }
  if (!enabled && m_Enabled) {
    releaseLocked();
    m_Count = 0;
    m_WriteSequence = 0;
    m_LiveSequence = 0;
  }
  m_Enabled = enabled;
  if (enabled)
    m_LiveSequence = m_WriteSequence;
  return true;
}

uint32_t BtDebugJournal::nextAttempt() {
  std::lock_guard<std::mutex> lock(m_Mutex);
  ++m_NextAttemptId;
  if (m_NextAttemptId == 0)
    ++m_NextAttemptId;
  return m_NextAttemptId;
}

uint32_t BtDebugJournal::sessionId() const {
  std::lock_guard<std::mutex> lock(m_Mutex);
  return m_SessionId;
}

bool BtDebugJournal::isEnabled() const {
  std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Enabled;
}

void BtDebugJournal::clear() {
  std::lock_guard<std::mutex> lock(m_Mutex);
  m_Count = 0;
  m_WriteSequence = 0;
  m_LiveSequence = 0;
  m_DroppedCount = 0;
}

void BtDebugJournal::encode(const BtDebugEvent &event, BtDebugRecord &record) {
  record = BtDebugRecord {};
  record.timestamp_ms = event.timestamp_ms;
  record.generation = event.generation;
  record.attempt_id = event.attempt_id;
  record.reason = event.reason;
  record.duration_ms = event.duration_ms;
  record.interval_before = event.interval_before;
  record.latency_before = event.latency_before;
  record.timeout_before = event.timeout_before;
  record.interval_after = event.interval_after;
  record.latency_after = event.latency_after;
  record.timeout_after = event.timeout_after;
  record.payload_length = event.payload_length;
  record.kind = static_cast<uint8_t>(event.kind);
  record.address_type = event.address_type;
  record.identity_type = event.identity_type;
  record.key_size = event.key_size;
  record.rssi = event.rssi;
  if (event.success)
    record.flags |= FLAG_SUCCESS;
  if (event.response)
    record.flags |= FLAG_RESPONSE;
  if (event.encrypted)
    record.flags |= FLAG_ENCRYPTED;
  if (event.authenticated)
    record.flags |= FLAG_AUTHENTICATED;
  if (event.bonded)
    record.flags |= FLAG_BONDED;
  if (event.physical)
    record.flags |= FLAG_PHYSICAL;
  if (event.logical)
    record.flags |= FLAG_LOGICAL;
  if (event.begin)
    record.flags |= FLAG_BEGIN;
  if (event.payload_truncated || event.payload_length > sizeof(record.payload)) {
    record.flags |= FLAG_PAYLOAD_TRUNCATED;
  }
  if (encodeAddress(event.address, record.address))
    record.flags |= 1U << 9;
  if (encodeAddress(event.identity, record.identity))
    record.flags |= 1U << 10;
  encodeUuid(event.service_uuid, record.service_uuid, record.service_uuid_length);
  encodeUuid(event.characteristic_uuid, record.characteristic_uuid,
             record.characteristic_uuid_length);
  copyText(record.operation, sizeof(record.operation), event.operation);
  copyText(record.owner, sizeof(record.owner), event.owner);
  copyText(record.state, sizeof(record.state), event.state);
  copyText(record.result, sizeof(record.result), event.result);
  copyText(record.reason_text, sizeof(record.reason_text), event.reason_text);
  copyText(record.name, sizeof(record.name), event.name);
  copyText(record.manufacturer, sizeof(record.manufacturer), event.manufacturer);
  memcpy(record.payload, event.payload, sizeof(record.payload));
}

void BtDebugJournal::decode(const BtDebugRecord &record,
                            uint64_t sequence,
                            uint32_t session,
                            BtDebugEvent &event) {
  event = BtDebugEvent {};
  event.timestamp_ms = record.timestamp_ms;
  event.sequence = sequence;
  event.session_id = session;
  event.attempt_id = record.attempt_id;
  event.kind = static_cast<BtDebugEventKind>(record.kind);
  event.generation = record.generation;
  event.reason = record.reason;
  event.address_type = record.address_type;
  event.identity_type = record.identity_type;
  event.key_size = record.key_size;
  event.rssi = record.rssi;
  event.duration_ms = record.duration_ms;
  event.interval_before = record.interval_before;
  event.latency_before = record.latency_before;
  event.timeout_before = record.timeout_before;
  event.interval_after = record.interval_after;
  event.latency_after = record.latency_after;
  event.timeout_after = record.timeout_after;
  event.payload_length = record.payload_length;
  event.success = (record.flags & FLAG_SUCCESS) != 0;
  event.response = (record.flags & FLAG_RESPONSE) != 0;
  event.encrypted = (record.flags & FLAG_ENCRYPTED) != 0;
  event.authenticated = (record.flags & FLAG_AUTHENTICATED) != 0;
  event.bonded = (record.flags & FLAG_BONDED) != 0;
  event.physical = (record.flags & FLAG_PHYSICAL) != 0;
  event.logical = (record.flags & FLAG_LOGICAL) != 0;
  event.begin = (record.flags & FLAG_BEGIN) != 0;
  event.payload_truncated = (record.flags & FLAG_PAYLOAD_TRUNCATED) != 0;
  if ((record.flags & (1U << 9)) != 0)
    decodeAddress(event.address, sizeof(event.address), record.address);
  if ((record.flags & (1U << 10)) != 0) {
    decodeAddress(event.identity, sizeof(event.identity), record.identity);
  }
  copyUuid(event.service_uuid, sizeof(event.service_uuid), record.service_uuid,
           record.service_uuid_length);
  copyUuid(event.characteristic_uuid, sizeof(event.characteristic_uuid), record.characteristic_uuid,
           record.characteristic_uuid_length);
  copyText(event.operation, sizeof(event.operation), record.operation);
  copyText(event.owner, sizeof(event.owner), record.owner);
  copyText(event.state, sizeof(event.state), record.state);
  copyText(event.result, sizeof(event.result), record.result);
  copyText(event.reason_text, sizeof(event.reason_text), record.reason_text);
  copyText(event.name, sizeof(event.name), record.name);
  copyText(event.manufacturer, sizeof(event.manufacturer), record.manufacturer);
  memcpy(event.payload, record.payload, sizeof(record.payload));
}

void BtDebugJournal::record(const BtDebugEvent &event) {
  std::lock_guard<std::mutex> lock(m_Mutex);
  if (!m_Enabled || m_Events == nullptr || m_EventCapacity == 0)
    return;
  BtDebugRecord record;
  encode(event, record);
  if (m_Count == m_EventCapacity)
    ++m_DroppedCount;
  m_Events[m_WriteSequence % m_EventCapacity] = record;
  ++m_WriteSequence;
  m_Count = std::min(m_EventCapacity, m_Count + 1);
}

size_t BtDebugJournal::droppedCount() const {
  std::lock_guard<std::mutex> lock(m_Mutex);
  return m_DroppedCount;
}

size_t BtDebugJournal::size() const {
  std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Count;
}

size_t BtDebugJournal::capacity() const {
  std::lock_guard<std::mutex> lock(m_Mutex);
  return m_EventCapacity;
}

size_t BtDebugJournal::storageBytes() const {
  std::lock_guard<std::mutex> lock(m_Mutex);
  return m_EventCapacity * sizeof(BtDebugRecord);
}

size_t BtDebugJournal::dump(size_t count, Emit emit, void *context) const {
  if (emit == nullptr)
    return 0;
  size_t requested;
  uint64_t start;
  uint64_t end;
  {
    std::lock_guard<std::mutex> lock(m_Mutex);
    requested = std::min(count == 0 ? m_Count : count, m_Count);
    start = m_WriteSequence - requested;
    end = m_WriteSequence;
  }
  size_t emitted = 0;
  for (uint64_t sequence = start; sequence < end; ++sequence) {
    BtDebugEvent event;
    {
      std::lock_guard<std::mutex> lock(m_Mutex);
      const uint64_t oldest = m_WriteSequence - m_Count;
      if (sequence < oldest || sequence >= m_WriteSequence || m_Events == nullptr)
        continue;
      decode(m_Events[sequence % m_EventCapacity], sequence, m_SessionId, event);
    }
    emit(event, context);
    ++emitted;
  }
  return emitted;
}

size_t BtDebugJournal::drain(size_t count, Emit emit, void *context) {
  if (emit == nullptr)
    return 0;
  size_t emitted = 0;
  while (count == 0 || emitted < count) {
    BtDebugEvent event;
    {
      std::lock_guard<std::mutex> lock(m_Mutex);
      const uint64_t oldest = m_WriteSequence - m_Count;
      if (m_LiveSequence < oldest)
        m_LiveSequence = oldest;
      if (m_LiveSequence >= m_WriteSequence || m_Events == nullptr)
        break;
      decode(m_Events[m_LiveSequence % m_EventCapacity], m_LiveSequence, m_SessionId, event);
      ++m_LiveSequence;
    }
    emit(event, context);
    ++emitted;
  }
  return emitted;
}

}  // namespace Furble
