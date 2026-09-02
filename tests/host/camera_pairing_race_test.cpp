// Deterministic regression for answering a pairing request while NimBLE
// delivers a synchronous disconnect callback and self-deletes the client.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-host";

namespace {

std::mutex g_HookMutex;
std::condition_variable g_HookCondition;
bool g_HandleEntered = false;
bool g_ReleaseHandle = false;
bool g_DisconnectEntered = false;

void getConnHandleHook() {
  std::unique_lock<std::mutex> lock(g_HookMutex);
  g_HandleEntered = true;
  g_HookCondition.notify_all();
  g_HookCondition.wait(lock, [] { return g_ReleaseHandle; });
}

void disconnectCallbackHook() {
  const std::lock_guard<std::mutex> lock(g_HookMutex);
  g_DisconnectEntered = true;
  g_HookCondition.notify_all();
}

void waitFor(bool &value, const char *message) {
  std::unique_lock<std::mutex> lock(g_HookMutex);
  if (!g_HookCondition.wait_for(lock, std::chrono::seconds(2), [&value] { return value; })) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  Furble::FujifilmBasic camera(&advertisement);

  if (!camera.connect(ESP_PWR_LVL_P3, 1000)) {
    std::cerr << "FAIL: the camera connects\n";
    return 1;
  }

  camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 428913);
  NimBLEClient *client = NimBLEDevice::lastClient();
  if (client == nullptr) {
    std::cerr << "FAIL: the connected client is available\n";
    return 1;
  }

  g_HandleEntered = false;
  g_ReleaseHandle = false;
  g_DisconnectEntered = false;
  NimBLEDevice::setGetConnHandleHook(getConnHandleHook);
  NimBLEDevice::setDisconnectCallbackHook(disconnectCallbackHook);

  bool answerResult = false;
  std::thread answer([&] { answerResult = camera.answerPairing(true); });
  waitFor(g_HandleEntered, "answerPairing reaches the client handle read");

  std::thread drop([client] { client->mockDropLinkSelfDelete(0x08); });
  waitFor(g_DisconnectEntered, "the concurrent disconnect enters onDisconnect");

  {
    const std::lock_guard<std::mutex> lock(g_HookMutex);
    g_ReleaseHandle = true;
  }
  g_HookCondition.notify_all();

  drop.join();
  answer.join();
  NimBLEDevice::setGetConnHandleHook(nullptr);
  NimBLEDevice::setDisconnectCallbackHook(nullptr);

  if (!answerResult || camera.isConnected() || (NimBLEDevice::liveClientCount() != 0)
      || NimBLEDevice::clientUseAfterFreeDetected()) {
    std::cerr << "FAIL: pairing answer raced client teardown\n";
    return 1;
  }

  // Re-run the prompt through the reject path. With deferred self-delete
  // enabled, Camera::answerPairing(false) invokes a synchronous disconnect
  // callback and NimBLE frees the client before the answer returns.
  NimBLEDevice::setDeferredClientDelete(true);
  if (!camera.connect(ESP_PWR_LVL_P3, 1000)) {
    std::cerr << "FAIL: the camera reconnects for synchronous rejection\n";
    return 1;
  }
  camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 314159);
  if (!camera.answerPairing(false) || camera.isConnected()
      || (NimBLEDevice::liveClientCount() != 0)) {
    std::cerr << "FAIL: synchronous pairing rejection tears down safely\n";
    return 1;
  }

  NimBLEDevice::resetMock();
  std::cout << "camera pairing lifetime race: PASS\n";
  return 0;
}
