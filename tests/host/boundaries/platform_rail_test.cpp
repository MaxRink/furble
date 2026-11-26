// Host test for the production src/FurblePlatform.cpp EXT_5V rail path.
//
// The rail fix lives in one translation unit that no host target compiled
// before this test, so a reverted decision there stayed green: tests/host
// rail-account pins the pure accounting and the policy header, and the
// simulator links sim/FurblePlatformSim.cpp, a second implementation. This
// target compiles the real src/FurblePlatform.cpp against a register-bus fake
// M5PM1 (platform_shims/M5PM1.h, the same pattern lib/hal/AXP2101.h and
// tests/host/hal_drivers_test.cpp use) and drives the real Platform methods.
//
// Asserted here, each proved by a mutation recorded in
// plans/364-review-feedback.md:
//   * A refused enable performs no write at all: the interlock runs before the
//     PWR_CFG 0x06 read-modify-write, so the vendor default of INPUT survives.
//     Refusal covers an external source on the port, a supply PWR_SRC does not
//     define (4 to 7), the UNKNOWN value and an unreadable register.
//   * A refused cut performs no write at all: a second holder keeps the rail up.
//     The opposite direction is asserted too, so a cut decision that always
//     refuses fails as well.
//   * An interleaved LED update and rail update both survive: each write keeps
//     the other owner's bit, and a second writer cannot enter the byte while one
//     update is between its read and its write.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "FurblePlatform.h"
#include "FurbleRail.h"
#include "M5PM1.h"

using Furble::Platform;
using Furble::PowerBackend;
using Furble::RailHolder;
using Furble::PlatformPolicy::RailEnableVerdict;

namespace {

int g_Failures = 0;

void check(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << '\n';
    g_Failures++;
  }
}

Platform &platform(void) {
  return Platform::getInstance();
}

M5PM1 &pmic(void) {
  return M5PM1::instanceForTest();
}

constexpr uint8_t PWR_CFG_BOOST = M5PM1_PWR_CFG_BOOST_EN;
constexpr uint8_t PWR_CFG_LED = M5PM1_PWR_CFG_LED_CTRL;
// The vendor power-on default for PWR_CFG 0x06: charge, 3.3 V DCDC and LDO on,
// 5 V boost off, status LED high. See docs/power/board-power.md section 7.
constexpr uint8_t PWR_CFG_DEFAULT = 0x17;

/**
 * Put the rail and the register model back in the boot state.
 *
 * The counters are cumulative by design, so every case asserts a delta.
 */
void resetRail(void) {
  (void)platform().railRelease(PowerBackend::Rail::EXT_5V, RailHolder::GPS);
  (void)platform().railRelease(PowerBackend::Rail::EXT_5V, RailHolder::IR);
  pmic().setInterleaveHook(nullptr);
  pmic().setPowerSourceUnreadable(false);
  pmic().setPowerSourceRaw(M5PM1_PWR_SRC_BAT);
  pmic().setPowerConfig(PWR_CFG_DEFAULT);
  // A cut with no holder is allowed and refreshes the cached state from the
  // read-back, which is what the boot state needs.
  (void)platform().setRail(PowerBackend::Rail::EXT_5V, false);
  pmic().setPowerConfig(PWR_CFG_DEFAULT);
  pmic().clearWrites();
}

/**
 * No register write happened and PWR_CFG still holds the vendor default.
 *
 * The context names the case, so a mutation failure says which refusal wrote.
 */
void checkNoWrite(const std::string &context) {
  const std::string writes = "no register was written for " + context;
  const std::string unchanged = "PWR_CFG 0x06 is unchanged after " + context;
  const std::string no_write = "no PWR_CFG 0x06 write for " + context;
  check(pmic().registerWrites().empty(), writes.c_str());
  check(pmic().powerConfig() == PWR_CFG_DEFAULT, unchanged.c_str());
  check(pmic().powerConfigWrites().empty(), no_write.c_str());
}

// A refused enable must not commit the port to output mode.
void testRefusedEnableWritesNothing(void) {
  struct Case {
    uint8_t raw;
    bool unreadable;
    RailEnableVerdict verdict;
    const char *what;
  };
  const Case cases[] = {
      {M5PM1_PWR_SRC_5VINOUT,     false, RailEnableVerdict::REFUSE_EXTERNAL_SOURCE,
       "an external 5 V source on the port"                                                                },
      {M5PM1_PWR_SRC_UNKNOWN,     false, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,
       "the UNKNOWN supply value"                                                                          },
      {M5PM1_PWR_SRC_UNDEFINED_4, false, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,
       "PWR_SRC value 4, which the vendor enumeration does not define"                                     },
      {M5PM1_PWR_SRC_UNDEFINED_5, false, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,
       "PWR_SRC value 5, which the vendor enumeration does not define"                                     },
      {M5PM1_PWR_SRC_UNDEFINED_6, false, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,
       "PWR_SRC value 6, which the vendor enumeration does not define"                                     },
      {M5PM1_PWR_SRC_UNDEFINED_7, false, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,
       "PWR_SRC value 7, which the vendor enumeration does not define"                                     },
      {M5PM1_PWR_SRC_BAT,         true,  RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,  "an unreadable PWR_SRC"},
  };

  for (const Case &c : cases) {
    resetRail();
    pmic().setPowerSourceRaw(c.raw);
    pmic().setPowerSourceUnreadable(c.unreadable);

    const uint32_t refusals = platform().railEnableRefusals();
    check(!platform().railAcquire(PowerBackend::Rail::EXT_5V, RailHolder::GPS),
          "a refused enable reports false");
    checkNoWrite(c.what);
    check(platform().railHolders(PowerBackend::Rail::EXT_5V) == 0,
          "a refused enable leaves no hold behind");
    check(platform().railEnableRefusals() == refusals + 1, "a refused enable is counted once");
    check(platform().railLastVerdict() == c.verdict, "the refusal reports its verdict");
  }

  // The allowed direction, so the interlock is not simply always refusing.
  resetRail();
  pmic().setPowerSourceRaw(M5PM1_PWR_SRC_BAT);
  check(platform().railAcquire(PowerBackend::Rail::EXT_5V, RailHolder::GPS),
        "battery only permits output mode");
  check((pmic().powerConfig() & PWR_CFG_BOOST) != 0, "the rail bit is set");
  check(platform().railLastVerdict() == RailEnableVerdict::ALLOW, "the verdict is allow");

  resetRail();
  pmic().setPowerSourceRaw(M5PM1_PWR_SRC_5VIN);
  check(platform().railAcquire(PowerBackend::Rail::EXT_5V, RailHolder::GPS),
        "USB 5 V input permits output mode");
  check((pmic().powerConfig() & PWR_CFG_BOOST) != 0, "the rail bit is set on USB");
}

// A cut refused for another holder must not touch the PMIC, and the last
// release must still cut: both directions of the decision are pinned.
void testRefusedCutWritesNothing(void) {
  resetRail();
  check(platform().railAcquire(PowerBackend::Rail::EXT_5V, RailHolder::GPS),
        "the GPS hold powers the rail");
  check(platform().railAcquire(PowerBackend::Rail::EXT_5V, RailHolder::IR),
        "the infrared hold joins the powered rail");
  pmic().clearWrites();

  const uint32_t cuts = platform().railCutRefusals();
  check(!platform().railRelease(PowerBackend::Rail::EXT_5V, RailHolder::GPS),
        "a cut is refused while the infrared circuit holds the rail");
  check(pmic().registerWrites().empty(), "a refused release writes no register");
  check((pmic().powerConfig() & PWR_CFG_BOOST) != 0, "the rail stays powered");
  check(platform().railCutRefusals() == cuts + 1, "the refused cut is counted once");
  check(platform().railHolders(PowerBackend::Rail::EXT_5V) == Furble::railHolderBit(RailHolder::IR),
        "only the infrared hold remains");

  // The public switch refuses the same cut, and writes nothing either.
  check(!platform().setRail(PowerBackend::Rail::EXT_5V, false),
        "setRail refuses a cut another holder needs");
  check(pmic().registerWrites().empty(), "a refused setRail writes no register");

  check(platform().railRelease(PowerBackend::Rail::EXT_5V, RailHolder::IR),
        "the last holder release cuts the rail");
  check(pmic().powerConfigWrites().size() == 1, "the cut is one PWR_CFG write");
  check((pmic().powerConfig() & PWR_CFG_BOOST) == 0, "the rail bit is clear");
  check(pmic().powerConfig() == (PWR_CFG_DEFAULT & static_cast<uint8_t>(~PWR_CFG_BOOST)),
        "the cut kept the neighbouring bits");
}

// The LED bit and the rail bit share PWR_CFG 0x06. Each writer must keep the
// other's bit, in both interleavings.
void testInterleavedLedAndRailUpdates(void) {
  resetRail();
  check((pmic().powerConfig() & PWR_CFG_LED) != 0, "the default has the status LED high");

  check(platform().railAcquire(PowerBackend::Rail::EXT_5V, RailHolder::GPS),
        "the rail comes up with a holder");
  check((pmic().powerConfig() & PWR_CFG_LED) != 0, "the rail write kept the LED bit");

  platform().setDisplayOff(true);
  check((pmic().powerConfig() & PWR_CFG_LED) == 0, "the LED policy cleared the LED bit");
  check((pmic().powerConfig() & PWR_CFG_BOOST) != 0, "the LED write kept the rail bit");
  check(
      platform().railHolders(PowerBackend::Rail::EXT_5V) == Furble::railHolderBit(RailHolder::GPS),
      "the LED write did not disturb the accounting");

  platform().setDisplayOff(false);
  check((pmic().powerConfig() & (PWR_CFG_BOOST | PWR_CFG_LED)) == (PWR_CFG_BOOST | PWR_CFG_LED),
        "both bits survive the LED restore");

  check(platform().railRelease(PowerBackend::Rail::EXT_5V, RailHolder::GPS),
        "the last release cuts the rail");
  check((pmic().powerConfig() & PWR_CFG_LED) != 0, "the cut kept the LED bit");

  // The exact write sequence: the rail write, the LED clear, the LED restore,
  // the cut. A writer that used a stale byte would lose a neighbouring bit here.
  const std::vector<uint8_t> writes = pmic().powerConfigWrites();
  const std::vector<uint8_t> expected = {
      static_cast<uint8_t>(PWR_CFG_DEFAULT | PWR_CFG_BOOST),
      static_cast<uint8_t>((PWR_CFG_DEFAULT | PWR_CFG_BOOST) & static_cast<uint8_t>(~PWR_CFG_LED)),
      static_cast<uint8_t>(PWR_CFG_DEFAULT | PWR_CFG_BOOST),
      static_cast<uint8_t>(PWR_CFG_DEFAULT & static_cast<uint8_t>(~PWR_CFG_BOOST)),
  };
  check(writes == expected, "each PWR_CFG write kept the other owner's bit");
}

// The serialization itself: while one update is between its read and its write,
// no other writer may enter the byte. The hook runs inside the vendor
// read-modify-write, so a second Platform call there is the exact lost-update
// window the single writer closes.
void testSecondWriterIsSerialized(void) {
  resetRail();

  std::mutex mutex;
  std::condition_variable entered;
  std::condition_variable release;
  bool in_window = false;
  bool may_leave = false;
  std::atomic<bool> second_returned {false};

  // Only the first transaction parks: a second writer that really did reach the
  // PMIC must be able to finish, or the check below could not tell a serialized
  // writer from one that is stuck in the same hook.
  std::atomic<bool> first_entry {true};
  pmic().setInterleaveHook([&]() {
    if (!first_entry.exchange(false)) {
      return;
    }
    std::unique_lock<std::mutex> lock(mutex);
    in_window = true;
    entered.notify_all();
    release.wait_for(lock, std::chrono::seconds(2), [&]() { return may_leave; });
  });

  std::thread first([&]() { platform().railAcquire(PowerBackend::Rail::EXT_5V, RailHolder::GPS); });

  {
    std::unique_lock<std::mutex> lock(mutex);
    entered.wait_for(lock, std::chrono::seconds(2), [&]() { return in_window; });
  }
  check(in_window, "the first update reached its read-modify-write window");

  std::thread second([&]() {
    // The LED policy writes the same byte through the same path.
    platform().setDisplayOff(true);
    second_returned = true;
  });

  // A serialized writer cannot complete while the first holds the byte.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  check(!second_returned.load(), "a second PWR_CFG writer cannot enter the critical section");

  {
    const std::lock_guard<std::mutex> lock(mutex);
    may_leave = true;
  }
  release.notify_all();

  first.join();
  second.join();
  pmic().setInterleaveHook(nullptr);

  check(second_returned.load(), "the second writer completes once the first leaves");
  check((pmic().powerConfig() & PWR_CFG_BOOST) != 0, "the rail bit was set");
  check((pmic().powerConfig() & PWR_CFG_LED) == 0, "the LED policy cleared the LED bit");
}

}  // namespace

int main(void) {
  testRefusedEnableWritesNothing();
  testRefusedCutWritesNothing();
  testInterleavedLedAndRailUpdates();
  testSecondWriterIsSerialized();

  if (g_Failures != 0) {
    std::cerr << g_Failures << " check(s) failed\n";
    return 1;
  }

  std::cout << "platform-rail: PASS\n";
  return 0;
}
