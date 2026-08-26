#include "MockEthNetif.h"

#include <utility>

namespace FurbleHost {

bool MockEthNetif::init(EventCallback callback) {
  ++m_InitCalls;
  m_Callback = std::move(callback);
  return m_InitResult;
}

bool MockEthNetif::start(void) {
  ++m_StartCalls;
  if (!m_StartResult) {
    return false;
  }
  m_Started = true;
  return true;
}

void MockEthNetif::stop(void) {
  ++m_StopCalls;
  m_Started = false;
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

void MockEthNetif::emitQueuedEvent(Event event, const std::string &ip) {
  if (m_Callback) {
    m_Callback(event, ip);
  }
}

}  // namespace FurbleHost
