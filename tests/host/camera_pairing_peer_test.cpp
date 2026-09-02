// The camera pairing prompt driven through the real NimBLE security
// callbacks, not the host state seam. The Ricoh virtual peer runs the MITM
// half of LE Secure Connections: it raises onConfirmPasskey (or
// onPassKeyDisplay) on the production Camera and blocks until the central
// injects an answer, exactly as the NimBLE host task does. The answer is sent
// from this thread while the connect runs on another, so the test has the two
// threads the device has: the host task inside secureConnection and the UI
// task answering the modal.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include "Camera.h"
#include "Device.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"
#include "RicohVirtualCamera.h"

const char *LOG_TAG = "camera-pairing-peer-test";

namespace {

std::mutex g_Mutex;
std::condition_variable g_Cv;
Furble::Camera *g_Camera = nullptr;
Furble::Camera::PairingType g_Type = Furble::Camera::PairingType::NONE;
uint32_t g_Code = 0;
uint32_t g_Count = 0;

// Stands in for the UI request queue: the security callback runs on the
// NimBLE host task and must not block there, so it only records the request
// and wakes the answering thread.
void pairingCallback(Furble::Camera *camera) {
  {
    const std::lock_guard<std::mutex> lock(g_Mutex);
    g_Camera = camera;
    g_Type = camera->getPairingType();
    g_Code = camera->getPairingCode();
    g_Count++;
  }
  g_Cv.notify_all();
}

void resetRequest(void) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  g_Camera = nullptr;
  g_Type = Furble::Camera::PairingType::NONE;
  g_Code = 0;
  g_Count = 0;
}

bool waitForRequest(void) {
  std::unique_lock<std::mutex> lock(g_Mutex);
  return g_Cv.wait_for(lock, std::chrono::seconds(5), []() { return g_Count > 0; });
}

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  Furble::Camera::setPairingRequestCallback(pairingCallback);

  // Numeric comparison, accepted. The camera shows a code, furble shows the
  // same code, and Confirm drives the real injectConfirmPasskey.
  {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    resetRequest();

    Furble::Host::RicohVirtualCamera::Config config;
    config.pairing_code = 428913;
    Furble::Host::RicohVirtualCamera peer(config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);

    std::atomic<bool> connected {false};
    std::thread host(
        [&camera, &connected]() { connected.store(camera.connect(ESP_PWR_LVL_P3, 5000)); });

    if (!check(waitForRequest(), "the peer handshake publishes a pairing request")) {
      host.join();
      return 1;
    }
    if (!check(g_Camera == &camera, "the request carries the connecting camera")
        || !check(g_Type == Furble::Camera::PairingType::NUMERIC_COMPARISON,
                  "a MITM peer raises numeric comparison")
        || !check(g_Code == 428913, "the displayed code is the code the camera generated")) {
      host.join();
      return 1;
    }

    if (!check(camera.answerPairing(true), "the pending request accepts")) {
      host.join();
      return 1;
    }
    host.join();
    if (!check(connected.load(), "an accepted numeric comparison completes the connection")
        || !check(NimBLEDevice::mockPasskeyConfirmCount() == 1,
                  "the answer reaches NimBLE exactly once")
        || !check(NimBLEDevice::mockLastPasskeyAccept(), "the injected answer is an accept")
        || !check(!camera.hasPendingPairing(), "the request is cleared after the answer")) {
      return 1;
    }
    camera.disconnect();
  }

  // Numeric comparison, rejected. Cancel must reject the handshake and leave
  // the camera disconnected instead of silently pairing.
  {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    resetRequest();

    Furble::Host::RicohVirtualCamera::Config config;
    config.pairing_code = 314159;
    Furble::Host::RicohVirtualCamera peer(config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);

    std::atomic<bool> connected {true};
    std::thread host(
        [&camera, &connected]() { connected.store(camera.connect(ESP_PWR_LVL_P3, 5000)); });

    if (!check(waitForRequest(), "the rejected handshake publishes a request")) {
      host.join();
      return 1;
    }
    if (!check(g_Code == 314159, "the rejected prompt shows its own code")) {
      host.join();
      return 1;
    }
    camera.cancelPairing();
    host.join();
    if (!check(!connected.load(), "a rejected numeric comparison fails the connection")
        || !check(NimBLEDevice::mockPasskeyConfirmCount() == 1,
                  "the rejection reaches NimBLE exactly once")
        || !check(!NimBLEDevice::mockLastPasskeyAccept(), "the injected answer is a reject")
        || !check(!camera.hasPendingPairing(), "rejection clears the request")) {
      return 1;
    }
  }

  // Passkey display. The camera has a keypad, so furble displays the code and
  // the user types it there; the peer accepts only the code furble showed.
  {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    resetRequest();

    Furble::Host::RicohVirtualCamera::Config config;
    config.pairing_code = 123456;
    config.passkey_display = true;
    Furble::Host::RicohVirtualCamera peer(config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);

    const bool connected = camera.connect(ESP_PWR_LVL_P3, 5000);
    if (!check(connected, "a matching displayed passkey completes the connection")
        || !check(g_Count == 1, "the display prompt is published once")
        || !check(g_Type == Furble::Camera::PairingType::PASSKEY_DISPLAY,
                  "a display peer raises passkey display")
        || !check(g_Code == 123456, "the displayed passkey is the code furble showed")
        || !check(NimBLEDevice::mockPasskeyConfirmCount() == 0,
                  "passkey display never injects a numeric comparison answer")
        || !check(!camera.hasPendingPairing(),
                  "authentication completion clears the display prompt")) {
      return 1;
    }
    camera.disconnect();
  }

  Furble::Camera::setPairingRequestCallback(nullptr);
  NimBLEDevice::resetMock();
  std::cout << "camera pairing peer handshake: PASS\n";
  return 0;
}
