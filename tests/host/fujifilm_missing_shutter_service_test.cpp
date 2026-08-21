// KNOWN-FAILING regression test (registered WILL_FAIL in CMakeLists).
//
// Finding: FujifilmBasic::_connect() null-dereferences when the camera does not
// expose the shutter GATT service.
//
// FujifilmBasic.cpp getService(SVC_SHUTTER_UUID) returning nullptr is only
// logged, then the very next line calls pSvc->getCharacteristic(CHR_SHUTTER_UUID)
// on that null pointer:
//
//     pSvc = m_Client->getService(SVC_SHUTTER_UUID);
//     if (pSvc == nullptr) {
//       ESP_LOGI(LOG_TAG, "Failed to get shutter service");   // logs, does not return
//     }
//     ...
//     m_Shutter = pSvc->getCharacteristic(CHR_SHUTTER_UUID);  // null dereference
//
// The sibling vendors guard this. FujifilmSecure.cpp and Nikon.cpp both
// `return false` when the shutter or primary service is missing. FujifilmBasic
// does not, so a camera that completes pairing and configuration but exposes no
// shutter service crashes furble during the connect. On device this is a hard
// fault, not a clean failed connect.
//
// The connect runs in a forked child so the crash cannot take down the harness
// process. The correct contract is: connect() returns false and the child exits
// cleanly. This test returns 0 only when that contract holds. Today the child
// crashes with SIGSEGV, so the test returns non-zero and CI stays green through
// the WILL_FAIL marker. When FujifilmBasic gains the missing `return false`, the
// child exits cleanly, this test returns 0, and the WILL_FAIL marker flips the
// job red, prompting removal of the marker.

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
