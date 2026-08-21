// Regression guard: FujifilmBasic::_connect() must not crash when the camera
// does not expose the shutter GATT service.
//
// The original bug: getService(SVC_SHUTTER_UUID) returning nullptr was only
// logged, then the next line dereferenced the null pointer via
// pSvc->getCharacteristic(CHR_SHUTTER_UUID), a hard fault on device.
// FujifilmBasic::_connect now `return false` when the shutter service is null,
// matching FujifilmSecure and Nikon.
//
// The connect runs in a forked child so any crash cannot take down the harness
// process. The correct contract is: connect() returns false and the child exits
// cleanly. This test returns 0 only when that contract holds. With the guard the
// child exits cleanly and this test passes as a normal test.

#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <memory>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-host";

namespace {

// Runs the connect against a peer with the shutter service suppressed. Returns
// the child exit code: 0 for a clean failed connect (the correct contract), 2 if
// the connect wrongly reports success. A null-dereference terminates the child
// by signal instead of returning here.
int connectWithSuppressedShutterService() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  peer.suppressService(Furble::Host::FujifilmVirtualCamera::shutterServiceUUID());

  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  auto camera = std::make_shared<Furble::FujifilmBasic>(&advertisement);

  const bool connected = camera->connect(ESP_PWR_LVL_P3, 1000);
  return connected ? 2 : 0;
}

}  // namespace

int main() {
  std::fprintf(stdout,
               "test: FujifilmBasic connect with a missing shutter service must not crash\n");

  const pid_t pid = fork();
  if (pid == 0) {
    _exit(connectWithSuppressedShutterService());
  }

  int status = 0;
  waitpid(pid, &status, 0);

  if (WIFSIGNALED(status)) {
    std::fprintf(stderr,
                 "  FAIL: connect crashed with signal %d (null dereference in "
                 "FujifilmBasic::_connect)\n",
                 WTERMSIG(status));
    return 1;
  }
  if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
    std::fprintf(stdout, "  connect failed cleanly without crashing\n");
    return 0;
  }
  std::fprintf(stderr, "  FAIL: connect wrongly reported success on a camera with no shutter\n");
  return 1;
}
