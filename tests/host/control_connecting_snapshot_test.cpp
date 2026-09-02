// Deterministic host regression for the Control connecting-camera snapshot.
// The UI pairing timer reads this shared_ptr while the control task publishes
// it around an unlocked BLE connect attempt. The reader threads are started and
// synchronised before the connect command is queued, so the publication and
// every snapshot read overlap on every run. Build with -DFURBLE_ENABLE_TSAN=ON
// for the sanitizer version of this same regression.

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "freertos/FreeRTOS.h"

const char *LOG_TAG = "furble-control-snapshot";

namespace {

constexpr size_t READER_COUNT = 4;

}  // namespace

int main() {
  FurbleHostTaskScope taskScope;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);

  Furble::Host::FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  auto camera = std::make_shared<Furble::FujifilmBasic>(&advertisement);
  auto &control = Furble::Control::getInstance();
  control.disconnect();
  control.addActive(camera);
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  std::atomic<size_t> ready {0};
  std::atomic<bool> start {false};
  std::atomic<bool> stop {false};
  std::atomic<bool> sawConnectingCamera {false};
  std::vector<std::thread> readers;
  readers.reserve(READER_COUNT);
  for (size_t i = 0; i < READER_COUNT; i++) {
    readers.emplace_back([&]() {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      while (!stop.load(std::memory_order_acquire)) {
        const auto snapshot = control.getConnectingCamera();
        if (snapshot == camera) {
          sawConnectingCamera.store(true, std::memory_order_release);
        }
        std::this_thread::yield();
      }
    });
  }

  while (ready.load(std::memory_order_acquire) != READER_COUNT) {
    std::this_thread::yield();
  }
  start.store(true, std::memory_order_release);

  // Keep the connecting-camera publication live long enough for all readers
  // to observe it while the real control task is inside the BLE connect.
  NimBLEDevice::setConnectDelayMs(500);
  control.connectAll(false);

  const auto activeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!camera->isConnected() && std::chrono::steady_clock::now() < activeDeadline) {
    std::this_thread::yield();
  }

  stop.store(true, std::memory_order_release);
  for (auto &reader : readers) {
    reader.join();
  }

  const bool observed = sawConnectingCamera.load(std::memory_order_acquire);
  const bool connected = camera->isConnected();
  // Stop the firmware task before returning. This keeps the regression focused
  // on the shared_ptr publication and avoids folding the pre-existing state
  // snapshot race into the TSAN result.
  furbleHostStopTasks();
  camera->disconnect();
  if (!observed || !connected) {
    std::cerr << "FAIL: synchronized readers did not observe the connecting camera\n";
    return 1;
  }

  std::cout << "control connecting-camera snapshot: PASS\n";
  return 0;
}
