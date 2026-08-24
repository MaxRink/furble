#ifndef FURBLE_HOST_MOCK_ETH_NETIF_H
#define FURBLE_HOST_MOCK_ETH_NETIF_H

#include <string>

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

  void setInitResult(bool result) { m_InitResult = result; }
  void setStartResult(bool result) { m_StartResult = result; }

  int initCalls(void) const { return m_InitCalls; }
  int startCalls(void) const { return m_StartCalls; }
  int stopCalls(void) const { return m_StopCalls; }
  bool started(void) const { return m_Started; }

 private:
  EventCallback m_Callback;
  bool m_InitResult = true;
  bool m_StartResult = true;
  bool m_Started = false;
  int m_InitCalls = 0;
  int m_StartCalls = 0;
  int m_StopCalls = 0;
};

}  // namespace FurbleHost

#endif
