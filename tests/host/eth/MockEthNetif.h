#ifndef FURBLE_HOST_MOCK_ETH_NETIF_H
#define FURBLE_HOST_MOCK_ETH_NETIF_H

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "FurbleEthernet.h"

namespace FurbleHost {

/** Deterministic W5500/netif event source for host lifecycle tests. */
class MockEthNetif final: public Furble::Ethernet::Transport {
 public:
  bool init(EventCallback callback) override;
  bool start(void) override;
  void stop(void) override;

  void emitLinkUp(void);
  void emitLinkDown(void);
  void emitGotIp(const std::string &ip);
  void emitIpLost(void);

  // Simulate an event already queued by the real event loop before stop().
  // Ordinary emit* methods remain transport-faithful and require started().
  void emitQueuedEvent(Event event, const std::string &ip = {});
  void emitQueuedEventFromGeneration(size_t generation, Event event, const std::string &ip = {});

  size_t callbackGenerationCount(void) const { return m_Callbacks.size(); }

  void setInitResult(bool result) { m_InitResult = result; }
  void setStartResult(bool result) { m_StartResult = result; }

  // Hold init() after production publishes the transport, so lifecycle tests
  // can race stop() against the real initialization sequence.
  void blockInit(void);
  void waitForInitEntered(void);
  void releaseInit(void);

  int initCalls(void) const { return m_InitCalls; }
  int startCalls(void) const { return m_StartCalls; }
  int stopCalls(void) const { return m_StopCalls.load(); }
  bool started(void) const { return m_Started; }

 private:
  EventCallback m_Callback;
  std::vector<EventCallback> m_Callbacks;
  bool m_InitResult = true;
  bool m_StartResult = true;
  bool m_Started = false;
  int m_InitCalls = 0;
  int m_StartCalls = 0;
  std::atomic<int> m_StopCalls = 0;
  std::mutex m_InitGateMutex;
  std::condition_variable m_InitGateCondition;
  bool m_BlockInit = false;
  bool m_InitEntered = false;
};

}  // namespace FurbleHost

#endif
