#include "BtDebugJournal.h"

#include <algorithm>
#include <cstring>

namespace Furble {

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

bool BtDebugJournal::setEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(m_Mutex);
  if (enabled && !m_Enabled) {
    ++m_SessionId;
    if (m_SessionId == 0) {
      ++m_SessionId;
    }
    m_NextAttemptId = 0;
    m_DroppedCount = 0;
  }
  m_Enabled = enabled;
  if (enabled) {
    m_LiveSequence = m_WriteSequence;
  }
  return true;
}

uint32_t BtDebugJournal::nextAttempt() {
  std::lock_guard<std::mutex> lock(m_Mutex);
  ++m_NextAttemptId;
  if (m_NextAttemptId == 0) {
    ++m_NextAttemptId;
  }
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

void BtDebugJournal::record(const BtDebugEvent &event) {
  std::lock_guard<std::mutex> lock(m_Mutex);
  if (!m_Enabled) {
    return;
  }
  BtDebugEvent stamped = event;
  stamped.sequence = m_WriteSequence;
  stamped.session_id = m_SessionId;
  if (m_Count == MAX_EVENTS) {
    ++m_DroppedCount;
  }
  m_Events[m_WriteSequence % MAX_EVENTS] = stamped;
  ++m_WriteSequence;
  m_Count = std::min(MAX_EVENTS, m_Count + 1);
}

size_t BtDebugJournal::droppedCount() const {
  std::lock_guard<std::mutex> lock(m_Mutex);
  return m_DroppedCount;
}

size_t BtDebugJournal::size() const {
  std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Count;
}

size_t BtDebugJournal::dump(size_t count, Emit emit, void *context) const {
  if (emit == nullptr) {
    return 0;
  }
  size_t requested;
  uint64_t start;
  uint64_t end;
  {
    std::lock_guard<std::mutex> lock(m_Mutex);
    requested = std::min(count == 0 ? m_Count : count, m_Count);
    start = m_WriteSequence - requested;
    end = m_WriteSequence;
  }
  // The callback normally prints to a potentially blocking console. Copy one
  // record at a time so NimBLE callbacks never wait for the whole dump.
  size_t emitted = 0;
  for (uint64_t sequence = start; sequence < end; ++sequence) {
    BtDebugEvent event;
    {
      std::lock_guard<std::mutex> lock(m_Mutex);
      const uint64_t oldest = m_WriteSequence - m_Count;
      if (sequence < oldest || sequence >= m_WriteSequence) {
        continue;
      }
      event = m_Events[sequence % MAX_EVENTS];
    }
    emit(event, context);
    ++emitted;
  }
  return emitted;
}

size_t BtDebugJournal::drain(size_t count, Emit emit, void *context) {
  if (emit == nullptr) {
    return 0;
  }
  size_t emitted = 0;
  while (count == 0 || emitted < count) {
    BtDebugEvent event;
    {
      std::lock_guard<std::mutex> lock(m_Mutex);
      const uint64_t oldest = m_WriteSequence - m_Count;
      if (m_LiveSequence < oldest) {
        m_LiveSequence = oldest;
      }
      if (m_LiveSequence >= m_WriteSequence) {
        break;
      }
      event = m_Events[m_LiveSequence % MAX_EVENTS];
      ++m_LiveSequence;
    }
    emit(event, context);
    ++emitted;
  }
  return emitted;
}

}  // namespace Furble
