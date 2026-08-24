#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "FurbleEthernet.h"

namespace {

class MockW5500Transport final: public Furble::Ethernet::Transport {
 public:
  bool init(EventCallback callback) override {
    m_Callback = std::move(callback);
    return true;
  }

  bool start(void) override {
    m_Started = true;
    return true;
  }

  void stop(void) override { m_Started = false; }

  void raiseLinkUp(void) {
    if (m_Started && m_Callback) {
      m_Callback(Event::LINK_UP, "");
    }
  }

  void raiseLinkDown(void) {
    if (m_Started && m_Callback) {
      m_Callback(Event::LINK_DOWN, "");
    }
  }

  void raiseGotIp(const std::string &ip) {
    if (m_Started && m_Callback) {
      m_Callback(Event::GOT_IP, ip);
    }
  }

 private:
  EventCallback m_Callback;
  bool m_Started = false;
};

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main(void) {
  MockW5500Transport mock;
  int callbackCount = 0;
  std::string callbackIP;

  Furble::Ethernet::setNetworkUpCallback([&](const std::string &ip) {
    callbackCount++;
    callbackIP = ip;
  });

  check(Furble::Ethernet::init(mock), "the injected W5500 transport initializes");
  check(!Furble::Ethernet::isConnected(), "Ethernet starts disconnected");
  check(Furble::Ethernet::getIP().empty(), "Ethernet starts without an IP");

  mock.raiseGotIp("192.0.2.1");
  check(!Furble::Ethernet::isConnected(), "an IP before link-up is ignored");
  check(callbackCount == 0, "an IP before link-up does not fire network-up");

  mock.raiseLinkUp();
  check(!Furble::Ethernet::isConnected(), "link-up waits for an IP event");
  check(callbackCount == 0, "link-up alone does not fire network-up");

  mock.raiseGotIp("");
  check(!Furble::Ethernet::isConnected(), "an empty IP is not usable");
  check(callbackCount == 0, "an empty IP does not fire network-up");

  mock.raiseGotIp("192.0.2.42");
  check(callbackCount == 1, "GOT_IP fires network-up once");
  check(callbackIP == "192.0.2.42", "network-up receives the expected IP");
  check(Furble::Ethernet::isConnected(), "Ethernet is connected after link-up and GOT_IP");
  check(Furble::Ethernet::getIP() == "192.0.2.42", "the IP getter reports the active address");

  mock.raiseLinkDown();
  check(!Furble::Ethernet::isConnected(), "link-down clears connected state");
  check(Furble::Ethernet::getIP().empty(), "link-down clears the active IP");

  mock.raiseLinkUp();
  mock.raiseGotIp("198.51.100.7");
  check(callbackCount == 2, "a later network-up fires the callback again");
  check(callbackIP == "198.51.100.7", "the later network-up receives its IP");
  check(Furble::Ethernet::isConnected(), "Ethernet reconnects after a new IP event");

  mock.raiseGotIp("198.51.100.7");
  check(callbackCount == 2, "duplicate IP events do not refire network-up");

  Furble::Ethernet::stop();
  check(!Furble::Ethernet::isConnected(), "stop clears the connected state");
  check(Furble::Ethernet::getIP().empty(), "stop clears the active IP");
  mock.raiseLinkDown();
  check(callbackCount == 2, "events after stop are ignored");

  check(Furble::Ethernet::init(mock), "the transport can be initialized again after stop");
  mock.raiseLinkUp();
  mock.raiseGotIp("203.0.113.9");
  check(callbackCount == 3, "a restarted transport reports network-up");
  check(Furble::Ethernet::getIP() == "203.0.113.9", "restart reports the new IP");
  Furble::Ethernet::stop();

  std::cout << "ethernet transport: PASS\n";
  return 0;
}
