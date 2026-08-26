#include <cstdlib>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

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

  // stop() must not release a transport while init() is still executing.
  // The production implementation publishes the transport before calling its
  // init hook, making this interleaving possible without lifecycle locking.
  FurbleHost::MockEthNetif concurrent;
  concurrent.blockInit();
  bool concurrentInitResult = false;
  std::thread initThread([&] {
    concurrentInitResult = Furble::Ethernet::init(concurrent);
  });
  concurrent.waitForInitEntered();
  std::thread stopThread([] { Furble::Ethernet::stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const bool stoppedBeforeInitReleased = concurrent.stopCalls() != 0;
  concurrent.releaseInit();
  initThread.join();
  stopThread.join();
  check(!stoppedBeforeInitReleased,
        "stop waits for an in-flight transport init before releasing it");
  check(concurrentInitResult, "the serialized concurrent init succeeds");
  check(concurrent.stopCalls() == 1, "the concurrent stop cleans up once init completes");
  check(!Furble::Ethernet::isConnected(), "concurrent stop leaves Ethernet disconnected");
  Furble::Ethernet::stop();

  FurbleHost::MockEthNetif concurrentStart;
  concurrentStart.blockStart();
  bool concurrentStartResult = false;
  std::thread startThread([&] {
    concurrentStartResult = Furble::Ethernet::init(concurrentStart);
  });
  concurrentStart.waitForStartEntered();
  std::thread stopDuringStartThread([] { Furble::Ethernet::stop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const bool stoppedBeforeStartReleased = concurrentStart.stopCalls() != 0;
  concurrentStart.releaseStart();
  startThread.join();
  stopDuringStartThread.join();
  check(!stoppedBeforeStartReleased,
        "stop waits for an in-flight transport start before releasing it");
  check(concurrentStartResult, "the serialized concurrent start succeeds");
  check(concurrentStart.stopCalls() == 1, "the concurrent start stop cleans up once");
  check(!Furble::Ethernet::isConnected(), "stop during start leaves Ethernet disconnected");
  Furble::Ethernet::stop();

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

  mock.emitIpLost();
  check(!Furble::Ethernet::isConnected(), "IP loss clears the connected state");
  check(Furble::Ethernet::getIP().empty(), "IP loss clears the active IP");
  check(callbackCount == 2, "IP loss does not fire a network-up callback");
  mock.emitGotIp("198.51.100.7");
  check(Furble::Ethernet::isConnected(), "the same IP can be reacquired after loss");
  check(callbackCount == 3, "same-IP reacquisition fires network-up");

  Furble::Ethernet::stop();
  check(!Furble::Ethernet::isConnected(), "stop clears the connected state");
  check(Furble::Ethernet::getIP().empty(), "stop clears the active IP");
  // These model callbacks that were already queued by the event loop before
  // stop() unregistered the real transport handlers. They must still be
  // rejected by production's g_Initialized guard.
  mock.emitQueuedEvent(Furble::Ethernet::Transport::Event::LINK_DOWN);
  mock.emitQueuedEvent(Furble::Ethernet::Transport::Event::GOT_IP, "203.0.113.1");
  check(callbackCount == 3, "events after stop are ignored");

  check(Furble::Ethernet::init(mock), "the transport can be initialized again after stop");
  check(mock.initCalls() == 2, "restart initializes the transport again");
  check(mock.startCalls() == 2, "restart starts the transport again");
  check(mock.callbackGenerationCount() == 2, "restart installs a new callback generation");
  mock.emitQueuedEventFromGeneration(0, Furble::Ethernet::Transport::Event::LINK_UP);
  mock.emitQueuedEventFromGeneration(0, Furble::Ethernet::Transport::Event::GOT_IP, "203.0.113.8");
  check(!Furble::Ethernet::isConnected(), "a queued old-generation event cannot revive a restart");
  check(Furble::Ethernet::getIP().empty(), "old-generation IP cannot mutate restart state");
  check(callbackCount == 3, "old-generation events do not notify network-up");
  mock.emitLinkUp();
  mock.emitGotIp("203.0.113.9");
  check(callbackCount == 4, "a restarted transport reports network-up");
  check(Furble::Ethernet::getIP() == "203.0.113.9", "restart reports the new IP");
  Furble::Ethernet::stop();

  // Repeat stop/reinit twice more. Each callback generation represents an
  // event-loop registration that may still have queued work when stop() runs.
  // Old generations must remain harmless across rapid successive restarts.
  check(Furble::Ethernet::init(mock), "the transport supports a second restart");
  check(mock.callbackGenerationCount() == 3, "second restart installs a third callback generation");
  mock.emitQueuedEventFromGeneration(1, Furble::Ethernet::Transport::Event::LINK_UP);
  mock.emitQueuedEventFromGeneration(1, Furble::Ethernet::Transport::Event::GOT_IP, "203.0.113.10");
  check(!Furble::Ethernet::isConnected(), "the prior generation cannot revive a second restart");
  check(Furble::Ethernet::getIP().empty(), "the prior generation cannot set a second IP");
  mock.emitLinkUp();
  mock.emitGotIp("203.0.113.11");
  check(callbackCount == 5, "the second restart reports network-up");
  check(Furble::Ethernet::getIP() == "203.0.113.11", "second restart reports its new IP");
  Furble::Ethernet::stop();

  check(Furble::Ethernet::init(mock), "the transport supports a third restart");
  check(mock.callbackGenerationCount() == 4, "third restart installs a fourth callback generation");
  mock.emitQueuedEventFromGeneration(2, Furble::Ethernet::Transport::Event::LINK_UP);
  mock.emitQueuedEventFromGeneration(2, Furble::Ethernet::Transport::Event::GOT_IP, "203.0.113.12");
  check(!Furble::Ethernet::isConnected(), "an older generation cannot revive a third restart");
  mock.emitLinkUp();
  mock.emitGotIp("203.0.113.13");
  check(callbackCount == 6, "the third restart reports network-up");
  check(Furble::Ethernet::getIP() == "203.0.113.13", "third restart reports its new IP");
  Furble::Ethernet::stop();

  std::cout << "ethernet transport: PASS\n";
  return 0;
}
