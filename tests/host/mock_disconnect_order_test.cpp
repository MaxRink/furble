// Deterministic ordering guard for a stalled BLE disconnect.
//
// A secureConnection() waiter must not return from a peer disconnect until the
// disconnect callback and client-side cleanup have finished. The real NimBLE
// host releases the blocked task only after its disconnect event has completed;
// waking it from the peer teardown first lets Camera::connect() reclaim the
// client while the event thread still touches it.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
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
  BlockingDisconnectCallbacks(std::atomic<uint32_t> &order,
                              std::atomic<uint32_t> &finished)
      : m_Order(order), m_Finished(finished) {}
  void onDisconnect(NimBLEClient *, int) override {
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Started = true;
    m_Signal.notify_all();
    m_Signal.wait(lock, [this]() { return m_Release; });
    m_Completed = true;
    m_Finished.store(m_Order.fetch_add(1) + 1);
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
  std::atomic<uint32_t> &m_Order;
  std::atomic<uint32_t> &m_Finished;
};

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

  std::atomic<uint32_t> order {0};
  std::atomic<uint32_t> callbackFinished {0};
  std::atomic<uint32_t> secureFinished {0};
  BlockingDisconnectCallbacks callbacks(order, callbackFinished);
  client->setClientCallbacks(&callbacks, false);
  const auto address = peer.advertisement().getAddress();
  const bool connected = client->connect(address);
  check(connected, "the client connects to the peer");
  if (!connected) {
    NimBLEDevice::deleteClient(client);
    NimBLEDevice::resetMock();
    return;
  }

  peer.setSecureConnectionStallMs(std::numeric_limits<uint32_t>::max());
  bool secureResult = true;
  std::thread secure([&]() {
    secureResult = client->secureConnection();
    secureFinished.store(order.fetch_add(1) + 1);
  });

  const auto entryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while ((peer.secureStallEntries() == 0)
         && (std::chrono::steady_clock::now() < entryDeadline)) {
    std::this_thread::yield();
  }
  check(peer.secureStallEntries() == 1, "secureConnection reaches the parked waiter");

  client->mockStallTerminate();
  std::thread completion([&]() { client->mockCompleteStalledTerminate(0x08); });

  const bool callbackStarted = callbacks.waitStarted(std::chrono::seconds(5));
  check(callbackStarted, "the disconnect callback starts before completion can finish");

  callbacks.release();
  completion.join();
  secure.join();
  check(callbacks.completed(), "the disconnect callback completes");
  check(!secureResult, "the terminated secure handshake reports failure");
  check(callbackFinished.load() != 0, "the disconnect callback records completion");
  check(secureFinished.load() != 0, "the secure call records return");
  check(callbackFinished.load() < secureFinished.load(),
        "disconnect callback completion precedes secure call return");
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
