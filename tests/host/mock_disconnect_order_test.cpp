// Deterministic ordering guard for a stalled BLE disconnect.
//
// A secureConnection() waiter must not return from a peer disconnect until the
// disconnect callback and client-side cleanup have finished. The real NimBLE
// host releases the blocked task only after its disconnect event has completed;
// waking it from the peer teardown first lets Camera::connect() reclaim the
// client while the event thread still touches it.

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << "\n";
    g_Failures++;
  }
}

class BlockingDisconnectCallbacks final: public NimBLEClientCallbacks {
 public:
  void onDisconnect(NimBLEClient *, int) override {
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Started = true;
    m_Signal.notify_all();
    m_Signal.wait(lock, [this]() { return m_Release; });
    m_Completed = true;
    m_Signal.notify_all();
  }

  bool waitStarted(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_Mutex);
    return m_Signal.wait_for(lock, timeout, [this]() { return m_Started; });
  }

  void release() {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Release = true;
    m_Signal.notify_all();
  }

  bool completed() const {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Completed;
  }

 private:
  mutable std::mutex m_Mutex;
  std::condition_variable m_Signal;
  bool m_Started = false;
  bool m_Release = false;
  bool m_Completed = false;
};

bool waitReturned(std::mutex &mutex,
                  std::condition_variable &signal,
                  bool &returned,
                  std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex);
  return signal.wait_for(lock, timeout, [&returned]() { return returned; });
}

void testCleanupPrecedesSecureWake() {
  std::cout << "test: stalled disconnect cleanup completes before secure wake\n";
  NimBLEDevice::resetMock();

  Furble::Host::FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  NimBLEClient *client = NimBLEDevice::createClient();
  check(client != nullptr, "the mock creates a client");
  if (client == nullptr) {
    return;
  }

  BlockingDisconnectCallbacks callbacks;
  client->setClientCallbacks(&callbacks, false);
  const auto address = peer.advertisement().getAddress();
  const bool connected = client->connect(address);
  check(connected, "the client connects to the peer");
  if (!connected) {
    NimBLEDevice::deleteClient(client);
    NimBLEDevice::resetMock();
    return;
  }

  peer.setSecureConnectionStallMs(1000);
  std::mutex secureMutex;
  std::condition_variable secureSignal;
  bool secureReturned = false;
  bool secureResult = true;
  std::thread secure([&]() {
    secureResult = client->secureConnection();
    const std::lock_guard<std::mutex> lock(secureMutex);
    secureReturned = true;
    secureSignal.notify_all();
  });

  const auto entryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while ((peer.secureStallEntries() == 0)
         && (std::chrono::steady_clock::now() < entryDeadline)) {
    std::this_thread::yield();
  }
  check(peer.secureStallEntries() == 1, "secureConnection reaches the parked waiter");

  client->mockStallTerminate();
  std::thread completion([&]() { client->mockCompleteStalledTerminate(0x08); });

  const bool callbackStarted = callbacks.waitStarted(std::chrono::seconds(1));
  check(callbackStarted, "the disconnect callback starts before completion can finish");
  if (callbackStarted) {
    // This is a watchdog only. The callback barrier makes the ordering assertion
    // deterministic: on the broken mock the earlier peer wake lets this wait
    // complete while onDisconnect is deliberately held open.
    const bool returnedBeforeCallback =
        waitReturned(secureMutex, secureSignal, secureReturned, std::chrono::milliseconds(200));
    check(!returnedBeforeCallback,
          "the blocked secure call cannot return while onDisconnect is still running");
  }

  callbacks.release();
  completion.join();
  secure.join();
  check(callbacks.completed(), "the disconnect callback completes");
  check(!secureResult, "the terminated secure handshake reports failure");
  check(secureReturned, "the secure call returns after disconnect cleanup");
  check(!peer.connected(), "the peer cleanup completed before the secure call returned");
  check(NimBLEDevice::deleteClient(client), "the disconnected client is reclaimed once");
  check(NimBLEDevice::liveClientCount() == 0, "no client remains after the regression");
  NimBLEDevice::resetMock();
}

}  // namespace

int main() {
  testCleanupPrecedesSecureWake();
  if (g_Failures != 0) {
    std::cout << "mock disconnect order: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "mock disconnect order: PASS\n";
  return 0;
}
