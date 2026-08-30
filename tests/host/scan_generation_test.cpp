#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "NimBLEDevice.h"
#include "Scan.h"

namespace Furble {
namespace {
size_t g_Matches = 0;

class CancelOnResult final: public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice *) override { Scan::getInstance().stop(); }
};

void check(bool condition, const char *message, int &failures) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
}  // namespace

// The production queue test only needs a matcher seam. The real CameraList
// matcher remains covered by advertisement_dispatch_test.
bool CameraList::match(const NimBLEAdvertisedDevice *) {
  ++g_Matches;
  return true;
}
}  // namespace Furble

int main() {
  using namespace Furble;
  int failures = 0;
  NimBLEDevice::resetMock();
  auto &scan = Scan::getInstance();
  auto *nimble = NimBLEDevice::getScan();

  NimBLEAdvertisedDevice advertisement;
  advertisement.setName("async-camera");
  size_t rows = 0;
  size_t failedStartEnds = 0;
  const auto testThread = std::this_thread::get_id();
  std::thread::id callbackThread;
  scan.setTimeout(0);
  nimbleMockSetGapScanStartAllowed(false);
  check(!scan.start([](void *) {}, nullptr,
                    [&failedStartEnds](void *) { ++failedStartEnds; }),
        "physical scan start failure propagates", failures);
  check(!scan.isActive() && !nimble->isScanning(),
        "failed physical start unwinds logical scan state", failures);
  scan.processPendingCallbacks();
  check(failedStartEnds == 0, "failed scan start has no synthetic completion callback", failures);
  nimbleMockSetGapScanStartAllowed(true);
  scan.start(
      [&rows, &callbackThread](void *) {
        rows++;
        callbackThread = std::this_thread::get_id();
      },
      nullptr);
  auto *oldCallbacks = nimble->callbacks();
  std::thread callbackWorker([oldCallbacks, &advertisement]() {
    for (size_t i = 0; i < Scan::MAX_PENDING_RESULTS; ++i) {
      oldCallbacks->onResult(&advertisement);
    }
    oldCallbacks->onResult(&advertisement);
  });
  callbackWorker.join();
  check(scan.droppedResultCount() == 1, "overflow is visible", failures);
  check(rows == 0 && g_Matches == 0, "BLE callback does not touch CameraList", failures);
  scan.processPendingCallbacks();
  check(rows == Scan::MAX_PENDING_RESULTS, "every queued row is drained", failures);
  check(g_Matches == Scan::MAX_PENDING_RESULTS, "matching runs on drain task", failures);
  check(callbackThread == testThread, "discovery callback runs on the drain task", failures);

  scan.start([&rows](void *) { ++rows; }, nullptr);
  auto *firstGeneration = oldCallbacks;
  auto *secondGeneration = nimble->callbacks();
  firstGeneration->onResult(&advertisement);
  firstGeneration->onScanEnd(NimBLEScanResults {}, 0);
  // Starting a new scan after the old callback sequence quiesced invalidates
  // its queued events. The stable proxy is then reused for the new generation.
  scan.start([&rows](void *) { ++rows; }, nullptr);
  secondGeneration = nimble->callbacks();
  secondGeneration->onResult(&advertisement);
  secondGeneration->onScanEnd(NimBLEScanResults {}, 0);
  scan.processPendingCallbacks();
  check(rows == Scan::MAX_PENDING_RESULTS + 1, "old generation events cannot feed the new scan",
        failures);
  check(!scan.isActive(), "current scan ends exactly once", failures);

  scan.start([](void *) {}, nullptr);
  auto *thirdGeneration = nimble->callbacks();
  scan.stop();
  thirdGeneration->onResult(&advertisement);
  thirdGeneration->onScanEnd(NimBLEScanResults {}, 0);
  scan.processPendingCallbacks();
  check(!scan.isActive(), "stopped scan rejects late callbacks", failures);

  scan.setTimeout(1);
  scan.start([](void *) {}, nullptr);
  const size_t stopCountBeforeObservation = nimble->stopCount();
  // The mock does not run a physical GAP timer. Sleeping past the logical
  // duration intentionally does not manufacture an end event; emitEnd()
  // below stands for NimBLE's physical completion boundary.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  check(scan.isActive(), "finite timeout remains active until the scan-end callback", failures);
  check(nimble->stopCount() == stopCountBeforeObservation,
        "isActive does not stop the physical scanner", failures);
  nimble->emitEnd();
  check(!scan.isActive(), "scan-end callback completes the logical scan", failures);
  check(!nimble->isScanning(), "scan-end callback observes a stopped physical scanner", failures);

  size_t endCallbacks = 0;
  scan.setTimeout(0);
  scan.start([](void *) {}, nullptr, [&endCallbacks](void *) { ++endCallbacks; });
  nimble->emitEnd();
  nimble->emitEnd();
  scan.processPendingCallbacks();
  check(endCallbacks == 1, "duplicate scan-end events deliver one completion callback", failures);

  CancelOnResult cancel;
  scan.setTimeout(0);
  scan.start(&cancel, 1000);
  nimble->emitResult(&advertisement);
  check(!scan.isActive(), "custom callback cancellation does not self-deadlock", failures);

  for (size_t i = 0; i < 1000; ++i) {
    scan.start([](void *) {}, nullptr);
    scan.stop();
  }
  scan.start([](void *) {}, nullptr);
  check(scan.isActive(), "sequential scans continue after 1000 starts", failures);
  scan.stop();

  // A completion may race the last advertisement at the NimBLE boundary.
  // Once end is accepted, a result delivered afterward must not be handed to
  // the saved-camera or discovery consumer.
  size_t reorderedRows = 0;
  scan.start([&reorderedRows](void *) { ++reorderedRows; }, nullptr, [](void *) {});
  auto *reorderedCallbacks = nimble->callbacks();
  reorderedCallbacks->onScanEnd(NimBLEScanResults {}, 0);
  reorderedCallbacks->onResult(&advertisement);
  scan.processPendingCallbacks();
  check(reorderedRows == 0, "result after end is rejected", failures);
  check(!scan.isActive(), "end-before-result leaves scan inactive", failures);

  if (failures != 0) {
    return EXIT_FAILURE;
  }
  std::cout << "scan generation: PASS\n";
  return EXIT_SUCCESS;
}
