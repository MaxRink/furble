#include <cstdlib>
#include <iostream>
#include <string>

#include "eth/MockEthNetif.h"

namespace {

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main(void) {
  FurbleHost::MockEthNetif mock;
  int callbackCount = 0;
  std::string callbackIP;

  Furble::Ethernet::setNetworkUpCallback([&](const std::string &ip) {
    callbackCount++;
    callbackIP = ip;
  });

  FurbleHost::MockEthNetif initFailure;
  initFailure.setInitResult(false);
  check(!Furble::Ethernet::init(initFailure), "an Ethernet init failure is reported");
  check(initFailure.initCalls() == 1, "the failed transport init is attempted once");
  check(initFailure.stopCalls() == 1, "a failed init stops and cleans up the transport");
  check(!initFailure.started(), "a failed init leaves the transport stopped");
  check(!Furble::Ethernet::isConnected(), "a failed init leaves Ethernet disconnected");

  FurbleHost::MockEthNetif startFailure;
  startFailure.setStartResult(false);
  check(!Furble::Ethernet::init(startFailure), "an Ethernet start failure is reported");
  check(startFailure.initCalls() == 1, "the start-failure transport is initialized once");
  check(startFailure.startCalls() == 1, "the failed transport start is attempted once");
  check(startFailure.stopCalls() == 1, "a failed start stops and cleans up the transport");
  check(!Furble::Ethernet::isConnected(), "a failed start leaves Ethernet disconnected");

  check(Furble::Ethernet::init(mock), "the injected W5500 transport initializes after recovery");
  check(mock.initCalls() == 1, "the recovered transport is initialized once");
  check(mock.startCalls() == 1, "the recovered transport starts once");
  check(!Furble::Ethernet::isConnected(), "Ethernet starts disconnected");
  check(Furble::Ethernet::getIP().empty(), "Ethernet starts without an IP");

  mock.emitGotIp("192.0.2.1");
  check(!Furble::Ethernet::isConnected(), "an IP before link-up is ignored");
  check(callbackCount == 0, "an IP before link-up does not fire network-up");

  mock.emitLinkUp();
  check(!Furble::Ethernet::isConnected(), "link-up waits for an IP event");
  check(callbackCount == 0, "link-up alone does not fire network-up");

  mock.emitGotIp("");
  check(!Furble::Ethernet::isConnected(), "an empty IP is not usable");
  check(callbackCount == 0, "an empty IP does not fire network-up");

  mock.emitGotIp("192.0.2.42");
  check(callbackCount == 1, "GOT_IP fires network-up once");
  check(callbackIP == "192.0.2.42", "network-up receives the expected IP");
  check(Furble::Ethernet::isConnected(), "Ethernet is connected after link-up and GOT_IP");
  check(Furble::Ethernet::getIP() == "192.0.2.42", "the IP getter reports the active address");

  mock.emitLinkDown();
  check(!Furble::Ethernet::isConnected(), "link-down clears connected state");
  check(Furble::Ethernet::getIP().empty(), "link-down clears the active IP");
  mock.emitGotIp("192.0.2.99");
  check(!Furble::Ethernet::isConnected(), "a stale IP after link-down is ignored");
  check(callbackCount == 1, "a stale IP after link-down does not refire network-up");

  mock.emitLinkUp();
  mock.emitGotIp("198.51.100.7");
  check(callbackCount == 2, "a later network-up fires the callback again");
  check(callbackIP == "198.51.100.7", "the later network-up receives its IP");
  check(Furble::Ethernet::isConnected(), "Ethernet reconnects after a new IP event");

  mock.emitGotIp("198.51.100.7");
  check(callbackCount == 2, "duplicate IP events do not refire network-up");

  Furble::Ethernet::stop();
  check(!Furble::Ethernet::isConnected(), "stop clears the connected state");
  check(Furble::Ethernet::getIP().empty(), "stop clears the active IP");
  mock.emitLinkDown();
  mock.emitGotIp("203.0.113.1");
  check(callbackCount == 2, "events after stop are ignored");

  check(Furble::Ethernet::init(mock), "the transport can be initialized again after stop");
  check(mock.initCalls() == 2, "restart initializes the transport again");
  check(mock.startCalls() == 2, "restart starts the transport again");
  mock.emitLinkUp();
  mock.emitGotIp("203.0.113.9");
  check(callbackCount == 3, "a restarted transport reports network-up");
  check(Furble::Ethernet::getIP() == "203.0.113.9", "restart reports the new IP");
  Furble::Ethernet::stop();

  std::cout << "ethernet transport: PASS\n";
  return 0;
}
