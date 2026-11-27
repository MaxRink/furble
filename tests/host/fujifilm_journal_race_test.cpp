// Thread-sanitizer regression for the Fujifilm peer write journal.
//
// FujifilmVirtualCamera::write() appends to m_Writes on the control task while
// it holds Host::peerWriteJournalMutex(); WriteJournal.h states that this one
// mutex keeps the append and the snapshot from overlapping, because the
// simulator's ble.peer queries and the host tests read the same journal from
// another task. FujifilmVirtualCamera::writes() returned a snapshot without
// taking it, so a copy could run while push_back() was reallocating the
// vector.
//
// The ASan and UBSan host job found it as a torn vector: control-e2e
// multi-connect-fujifilm polled writes() while the control task appended, and
// the copy bound a Write reference through a null iterator, reported as
// "reference binding to null pointer of type 'struct Write'" in
// stl_uninitialized.h. A rare window, so the sanitizer that can see the cause
// rather than the corruption is the one that belongs in the suite.
//
// This test is that overlap, run as a probe rather than an argument: one
// thread appends shutter writes through the peer's write() while the main
// thread takes snapshots and clears the journal. With the snapshot unlocked,
// ThreadSanitizer reports the vector mutation in write() against the copy in
// writes(). run_tsan_race.sh is the fail-closed gate: every sanitizer warning
// and every non-zero child status fails, with no report-name filter or
// suppression.
//
// The probe touches the journal only. The peer has other pre-existing
// unsynchronised state (mockDropLink() and disconnect() are reported under the
// control-e2e scenarios), and driving those here would make this test fail for
// reasons the journal fix does not address.

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"
#include "WriteJournal.h"

const char *LOG_TAG = "furble-fujifilm-journal";

namespace {

using Furble::Host::FujifilmVirtualCamera;

// Enough appends that the reader cannot miss every reallocation window, and
// short enough to stay far inside the 180 s ctest budget.
constexpr int WRITE_COUNT = 4000;
constexpr int SNAPSHOT_COUNT = 4000;
// The cleared journal is emptied from the reader too, so the clear/enlarge
// overlap is covered as well as the copy/append one.
constexpr int CLEAR_EVERY = 64;

// The shutter characteristic accepts exactly two bytes, high byte zero, low
// byte 0, 1 or 2 (fujifilm-handlers.md section 4). kShutterFullPress is 2.
const std::vector<uint8_t> FULL_PRESS {0x02, 0x00};

}  // namespace

int main(void) {
  NimBLEDevice::resetMock();

  FujifilmVirtualCamera::Config config;
  config.name = "FUJIFILM X100VI A";
  config.address = NimBLEAddress(0x112233445501ULL, 0);
  config.token = {0x11, 0x22, 0x33, 0x44};

  FujifilmVirtualCamera peer(config);
  NimBLEClient *client = NimBLEDevice::createClient();
  if ((client == nullptr) || !peer.acceptConnection(*client, config.address)) {
    std::cerr << "  FAIL: could not bring the virtual camera up\n";
    return 1;
  }

  std::atomic<bool> go {false};

  std::thread writer([&]() {
    while (!go.load(std::memory_order_acquire)) {
    }
    for (int i = 0; i < WRITE_COUNT; i++) {
      peer.write(*client, FujifilmVirtualCamera::shutterServiceUUID(),
                 FujifilmVirtualCamera::shutterCharacteristicUUID(), FULL_PRESS,
                 /*response=*/true);
    }
  });

  std::thread reader([&]() {
    while (!go.load(std::memory_order_acquire)) {
    }
    for (int i = 0; i < SNAPSHOT_COUNT; i++) {
      const auto snapshot = peer.writes();
      (void)snapshot;
      if ((i % CLEAR_EVERY) == 0) {
        peer.clearEvents();
      }
    }
  });

  go.store(true, std::memory_order_release);
  writer.join();
  reader.join();

  // Both threads are gone, so the journal is quiet: one write has to land, and
  // it has to be the only entry, which proves the lock still lets the append
  // through rather than dropping it.
  peer.clearEvents();
  peer.write(*client, FujifilmVirtualCamera::shutterServiceUUID(),
             FujifilmVirtualCamera::shutterCharacteristicUUID(), FULL_PRESS,
             /*response=*/true);
  if (peer.writes().size() != 1) {
    std::cerr << "  FAIL: a quiet journal did not record exactly one write\n";
    return 1;
  }
  if (peer.shutterAccepted() != static_cast<size_t>(WRITE_COUNT + 1)) {
    std::cerr << "  FAIL: the peer did not interpret every shutter write\n";
    return 1;
  }

  // The simulator's ble.peer queries hold Host::peerWriteJournalMutex() around
  // this accessor, because the other peer families return a view and need the
  // caller's lock. A peer that reached for that same non-recursive mutex inside
  // writes() therefore hung the UI task there, which the simulator watchdog
  // reported as exit 3. Lock it around the call here so the calling convention
  // is covered: a nested lock of a non-recursive mutex never returns, and the
  // ctest timeout turns that into a failure rather than a hang.
  {
    const std::lock_guard<std::mutex> journal(Furble::Host::peerWriteJournalMutex());
    if (peer.writes().size() != 1) {
      std::cerr << "  FAIL: the simulator's lock ordering did not reach the snapshot\n";
      return 1;
    }
    peer.clearEvents();
  }

  std::cout << "fujifilm-journal-race: PASS\n";
  return 0;
}
