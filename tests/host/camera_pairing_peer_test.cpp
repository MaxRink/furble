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
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
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
bool g_Accept = true;

// Stands in for the UI request queue: the security callback runs on the
// NimBLE host task and must not block there, so it only records the request
// and wakes the answering thread. The return value is the queue result, which
// is what tells Camera whether anything will answer.
bool pairingCallback(Furble::Camera *camera) {
  {
    const std::lock_guard<std::mutex> lock(g_Mutex);
    g_Camera = camera;
    g_Type = camera->getPairingType();
    g_Code = camera->getPairingCode();
    g_Count++;
  }
  g_Cv.notify_all();
  return g_Accept;
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

  // An expired prompt must reject. The window is aligned to the SMP timeout,
  // so a Confirm that arrives after it is a Confirm the stack can no longer
  // honour; downgrading it here is what stops furble authorizing a comparison
  // it had already promised to refuse.
  {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    resetRequest();

    Furble::Host::RicohVirtualCamera::Config config;
    config.pairing_code = 271828;
    Furble::Host::RicohVirtualCamera peer(config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);

    std::atomic<bool> connected {true};
    std::thread host(
        [&camera, &connected]() { connected.store(camera.connect(ESP_PWR_LVL_P3, 5000)); });

    if (!check(waitForRequest(), "the expiring handshake publishes a request")) {
      host.join();
      return 1;
    }
    if (!check(camera.hostExpirePairing(), "the pending request can be expired")
        || !check(camera.pairingTimedOut(), "the expired request reports timed out")) {
      camera.cancelPairing();
      host.join();
      return 1;
    }
    // A late Confirm, exactly what a user walking back to the device produces.
    if (!check(camera.answerPairing(true), "the expired request is still consumed")) {
      host.join();
      return 1;
    }
    host.join();
    if (!check(NimBLEDevice::mockPasskeyConfirmCount() == 1,
               "the expired answer reaches NimBLE exactly once")
        || !check(!NimBLEDevice::mockLastPasskeyAccept(),
                  "a Confirm after the deadline is injected as a reject")
        || !check(!connected.load(), "an expired numeric comparison fails the connection")
        || !check(!camera.hasPendingPairing(), "expiry clears the request")) {
      return 1;
    }
  }

  // Passkey display. The code furble shows must be the code NimBLE will inject,
  // which is NimBLEDevice::getSecurityPasskey(), not a constant compiled into
  // furble. The peer is the camera keypad and accepts only what furble showed.
  {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    resetRequest();
    NimBLEDevice::setSecurityPasskey(802134);

    Furble::Host::RicohVirtualCamera::Config config;
    config.pairing_code = 802134;
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
        || !check(g_Code == 802134, "the published code is the passkey NimBLE reports")
        || !check(peer.lastDisplayedPasskey() == 802134,
                  "the passkey handed to the stack is the one shown")
        || !check(NimBLEDevice::mockPasskeyConfirmCount() == 0,
                  "passkey display never injects a numeric comparison answer")
        || !check(!camera.hasPendingPairing(),
                  "authentication completion clears the display prompt")) {
      return 1;
    }
    camera.disconnect();
  }

  // A headless image has no UI task, so nothing registers a handler. The
  // request must still be answered the way it was before the prompt existed,
  // which is NimBLE's own default onConfirmPasskey: inject true. Anything else
  // turns a connect that worked on waveshare-s3-eth into one that hangs until
  // the peer abandons its SMP procedure.
  {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    resetRequest();
    Furble::Camera::setPairingRequestCallback(nullptr);

    Furble::Host::RicohVirtualCamera::Config config;
    config.pairing_code = 161803;
    Furble::Host::RicohVirtualCamera peer(config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);

    const bool connected = camera.connect(ESP_PWR_LVL_P3, 5000);
    Furble::Camera::setPairingRequestCallback(pairingCallback);
    if (!check(connected, "a headless build still completes numeric-comparison pairing")
        || !check(g_Count == 0, "no handler was called")
        || !check(NimBLEDevice::mockPasskeyConfirmCount() == 1,
                  "the unhandled request is answered exactly once")
        || !check(NimBLEDevice::mockLastPasskeyAccept(),
                  "the unhandled request is accepted, as NimBLE's default does")
        || !check(!camera.hasPendingPairing(), "nothing is left pending with no timer to expire")) {
      return 1;
    }
    camera.disconnect();
  }

  // A handler that exists but cannot take the request is the opposite case, and
  // it must NOT inherit the headless accept. On a display board a declined
  // request means the UI request queue is full: the prompt is never drawn, so
  // nobody compares the code. Accepting there would authorize a comparison no
  // human saw. It has to reject and fail the connect.
  {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    resetRequest();
    g_Accept = false;

    Furble::Host::RicohVirtualCamera::Config config;
    config.pairing_code = 141421;
    Furble::Host::RicohVirtualCamera peer(config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);

    const bool connected = camera.connect(ESP_PWR_LVL_P3, 5000);
    g_Accept = true;
    if (!check(!connected, "a declined request fails the connect")
        || !check(g_Count == 1, "the handler was offered the request")
        || !check(NimBLEDevice::mockPasskeyConfirmCount() == 1,
                  "the declined request is answered exactly once")
        || !check(!NimBLEDevice::mockLastPasskeyAccept(),
                  "a prompt that cannot be shown is rejected, not accepted")
        || !check(!camera.isConnected(), "the declined request drops the link")
        || !check(!camera.hasPendingPairing(), "a declined request is not left pending")) {
      return 1;
    }
  }

  // Two declines in a row: the second must reject as hard as the first. A
  // handler that stays wedged, which is what a UI request queue that never
  // drains looks like, may never drift into accepting.
  {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    resetRequest();
    g_Accept = false;

    Furble::Host::RicohVirtualCamera::Config config;
    config.pairing_code = 173205;
    Furble::Host::RicohVirtualCamera peer(config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);

    const bool first = camera.connect(ESP_PWR_LVL_P3, 5000);
    const bool firstAccept = NimBLEDevice::mockLastPasskeyAccept();
    const bool second = camera.connect(ESP_PWR_LVL_P3, 5000);
    g_Accept = true;
    if (!check(!first && !second, "neither declined connect completes")
        || !check(g_Count == 2, "both requests were offered to the handler")
        || !check(NimBLEDevice::mockPasskeyConfirmCount() == 2,
                  "each declined request is answered exactly once")
        || !check(!firstAccept && !NimBLEDevice::mockLastPasskeyAccept(),
                  "a repeated decline stays a reject")
        || !check(!camera.hasPendingPairing(), "neither declined request is left pending")) {
      return 1;
    }
  }

  // An answer recorded against a connection that has since been replaced must
  // never authorize the live one. The prompt is raised against a handle that is
  // not the client's, so Confirm has to drop the link instead of injecting.
  {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    resetRequest();

    Furble::Host::FujifilmVirtualCamera peer;
    NimBLEDevice::setMockPeer(&peer);
    const NimBLEAdvertisedDevice advertisement = peer.advertisement();
    Furble::FujifilmBasic camera(&advertisement);

    if (!check(camera.connect(ESP_PWR_LVL_P3, 1000), "the stale-handle case connects")) {
      return 1;
    }
    NimBLEClient *client = NimBLEDevice::lastClient();
    if (!check(client != nullptr, "the connected client is available")) {
      return 1;
    }
    const uint16_t stale = static_cast<uint16_t>(client->getConnHandle() + 1);
    camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 428913, stale);
    if (!check(g_Count == 1, "the stale request is published")
        || !check(camera.answerPairing(true), "the stale request is consumed")
        || !check(NimBLEDevice::mockPasskeyConfirmCount() == 0,
                  "a stale answer is never injected into the live link")
        || !check(!camera.isConnected(), "a stale answer drops the link it cannot authorize")) {
      return 1;
    }
  }

  Furble::Camera::setPairingRequestCallback(nullptr);
  NimBLEDevice::resetMock();
  std::cout << "camera pairing peer handshake: PASS\n";
  return 0;
}
