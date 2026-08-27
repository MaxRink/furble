#include "MockEthNetif.h"

#include <utility>

namespace FurbleHost {

bool MockEthNetif::init(EventCallback callback) {
  ++m_InitCalls;
  m_Callback = std::move(callback);
  m_Callbacks.push_back(m_Callback);

  {
    std::lock_guard<std::mutex> lock(m_InitGateMutex);
    m_InitEntered = true;
  }
  m_InitGateCondition.notify_all();
  std::unique_lock<std::mutex> lock(m_InitGateMutex);
  m_InitGateCondition.wait(lock, [this] { return !m_BlockInit; });
  return m_InitResult;
}

bool MockEthNetif::start(void) {
  ++m_StartCalls;
  {
    std::lock_guard<std::mutex> lock(m_StartGateMutex);
    m_StartEntered = true;
  }
  m_StartGateCondition.notify_all();
  std::unique_lock<std::mutex> lock(m_StartGateMutex);
  m_StartGateCondition.wait(lock, [this] { return !m_BlockStart; });
  if (!m_StartResult) {
    return false;
  }
  m_Started = true;
  return true;
}

void MockEthNetif::stop(void) {
  m_StopCalls.fetch_add(1);
  m_Started = false;
}

void MockEthNetif::blockInit(void) {
  std::lock_guard<std::mutex> lock(m_InitGateMutex);
  m_BlockInit = true;
  m_InitEntered = false;
}

void MockEthNetif::waitForInitEntered(void) {
  std::unique_lock<std::mutex> lock(m_InitGateMutex);
  m_InitGateCondition.wait(lock, [this] { return m_InitEntered; });
}

void MockEthNetif::releaseInit(void) {
  {
    std::lock_guard<std::mutex> lock(m_InitGateMutex);
    m_BlockInit = false;
  }
  m_InitGateCondition.notify_all();
}

void MockEthNetif::blockStart(void) {
  std::lock_guard<std::mutex> lock(m_StartGateMutex);
  m_BlockStart = true;
  m_StartEntered = false;
}

void MockEthNetif::waitForStartEntered(void) {
  std::unique_lock<std::mutex> lock(m_StartGateMutex);
  m_StartGateCondition.wait(lock, [this] { return m_StartEntered; });
}

void MockEthNetif::releaseStart(void) {
  {
    std::lock_guard<std::mutex> lock(m_StartGateMutex);
    m_BlockStart = false;
  }
  m_StartGateCondition.notify_all();
}

void MockEthNetif::emitLinkUp(void) {
  if (m_Started && m_Callback) {
    m_Callback(Event::LINK_UP, "");
  }
}

void MockEthNetif::emitLinkDown(void) {
  if (m_Started && m_Callback) {
    m_Callback(Event::LINK_DOWN, "");
  }
}

void MockEthNetif::emitGotIp(const std::string &ip) {
  if (m_Started && m_Callback) {
    m_Callback(Event::GOT_IP, ip);
  }
}

void MockEthNetif::emitIpLost(void) {
  if (m_Started && m_Callback) {
    m_Callback(Event::IP_LOST, "");
  }
}

void MockEthNetif::emitQueuedEvent(Event event, const std::string &ip) {
  if (m_Callback) {
    m_Callback(event, ip);
  }
}

void MockEthNetif::emitQueuedEventFromGeneration(size_t generation,
                                                 Event event,
                                                 const std::string &ip) {
  if (generation < m_Callbacks.size()) {
    m_Callbacks[generation](event, ip);
  }
}

}  // namespace FurbleHost
