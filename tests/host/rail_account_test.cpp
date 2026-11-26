// Host unit test for the EXT_5V rail ownership accounting and interlock policy.
//
// Board verification found the StickS3 rail (M5PM1 PWR_CFG bit 3) had three
// owners and no accounting: the GPS duty cycle cut it on every RAIL_OFF and
// after a failed probe, which stopped the infrared remote circuit and browned
// out Grove devices, and the LED policy wrote the same byte through a second
// driver. See plans/364-ext5v-rail.md.
//
// The fix gives the rail one owner and a reference count of named holders
// (Furble::RailAccount, include/FurbleRail.h) plus a supply interlock for
// output mode (Furble::PlatformPolicy::railEnableVerdict,
// include/FurblePlatformPolicy.h). Both are pure and header only, and the
// firmware and the simulator Platform drive this exact code, so the assertions
// below pin production behaviour.
//
// Mutation checks (the test's teeth):
//   * Make RailAccount::release() return cut=true while another holder is
//     counted, or held=false: testCutIsRefusedWhileAHolderRemains fails.
//   * Drop the per-holder bit and count calls instead: the double acquire and
//     stale release cases fail, because the count no longer reaches zero.
//   * Make railEnableVerdict() allow PORT_5V or an unreadable supply: the
//     interlock cases fail, which is the vendor short-circuit protection.
//   * Return ALLOW for RailSupply::UNKNOWN: the fail-closed case fails.
//   * Return ALLOW for a supply that is not USB_5V or BATTERY, which is the
//     denylist this verdict used to be: the raw 3 to 7 cases fail. Those are the
//     values PWR_SRC 0x04 bits 2:0 can report and the vendor enumeration does
//     not define, so the allowlist is what closes the interlock.

#include <cstdint>
#include <cstring>
#include <iostream>

#include "FurblePlatformPolicy.h"
#include "FurbleRail.h"

using Furble::RailAccount;
using Furble::RailHolder;
using Furble::railHolderBit;
using Furble::PlatformPolicy::RailEnableVerdict;
using Furble::PlatformPolicy::RailSupply;

namespace {

int g_Failures = 0;

void check(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << '\n';
    g_Failures++;
  }
}

void expectVerdict(RailEnableVerdict verdict, const char *name, bool known, RailSupply supply) {
  const RailEnableVerdict actual = Furble::PlatformPolicy::railEnableVerdict(known, supply);
  if (actual != verdict) {
    std::cerr << "FAIL: " << name << " expected "
              << Furble::PlatformPolicy::railEnableVerdictName(verdict) << " got "
              << Furble::PlatformPolicy::railEnableVerdictName(actual) << '\n';
    g_Failures++;
  }
}

// A fresh account is idle, and the first holder is the one that must switch the
// rail on.
void testFirstAcquireSwitchesOn(void) {
  RailAccount account;
  check(!account.held(), "a fresh account holds nothing");
  check(!account.holds(RailHolder::GPS), "a fresh account has no GPS holder");
  check(account.count() == 0, "a fresh account counts nothing");

  check(account.acquire(RailHolder::GPS), "the first acquire must switch the rail on");
  check(account.held(), "the rail is held after the first acquire");
  check(account.holds(RailHolder::GPS), "the GPS holder is counted");
  check(account.mask() == railHolderBit(RailHolder::GPS), "the mask carries only GPS");
  check(account.count() == 1, "one holder is counted");
}

// A second holder of a powered rail must not re-drive it, and must keep it up
// when the first releases. This is the field case: the GPS RAIL_OFF must not
// cut the infrared circuit.
void testCutIsRefusedWhileAHolderRemains(void) {
  RailAccount account;
  check(account.acquire(RailHolder::GPS), "GPS takes the rail");
  check(!account.acquire(RailHolder::IR), "a second holder must not re-drive the rail");
  check(account.count() == 2, "both holders are counted");

  const RailAccount::release_t gps = account.release(RailHolder::GPS);
  check(gps.held, "the GPS release reports another holder");
  check(!gps.cut, "the GPS release must not cut a rail the IR circuit needs");
  check(!gps.ignored, "the GPS release is not ignored");
  check(account.held(), "the rail is still held by the IR circuit");
  check(!account.holds(RailHolder::GPS), "the GPS hold is gone");
  check(account.mask() == railHolderBit(RailHolder::IR), "only the IR hold remains");

  const RailAccount::release_t ir = account.release(RailHolder::IR);
  check(ir.cut, "the last holder release must cut the rail");
  check(!ir.held, "nothing holds the rail after the last release");
  check(!account.held(), "the account is idle after the last release");
  check(account.mask() == 0, "the mask is empty after the last release");
}

// A repeated acquire from one task must not inflate the count, and a stale
// release must not cut a rail another holder still needs.
void testRepeatedAcquireAndStaleRelease(void) {
  RailAccount account;
  check(account.acquire(RailHolder::GPS), "the first GPS acquire switches the rail on");
  check(!account.acquire(RailHolder::GPS), "a repeated acquire must not switch it on again");
  check(account.count() == 1, "a repeated acquire counts one holder");

  account.acquire(RailHolder::GROVE);
  const RailAccount::release_t stale = account.release(RailHolder::IR);
  check(stale.ignored, "a release from a holder that never acquired is ignored");
  check(!stale.cut, "an ignored release must not cut the rail");
  check(account.held(), "the rail is still held after an ignored release");
  check(account.count() == 2, "the count is unchanged by an ignored release");
}

// The three named holders are independent bits, which is what the platform
// reports through Platform::railHolders().
void testHoldersAreIndependentBits(void) {
  RailAccount account;
  account.acquire(RailHolder::GPS);
  account.acquire(RailHolder::IR);
  account.acquire(RailHolder::GROVE);
  check(account.count() == 3, "all three holders are counted");
  check(account.mask()
            == (railHolderBit(RailHolder::GPS) | railHolderBit(RailHolder::IR)
                | railHolderBit(RailHolder::GROVE)),
        "each holder is a distinct bit");

  account.release(RailHolder::IR);
  check(account.holds(RailHolder::GPS) && account.holds(RailHolder::GROVE),
        "releasing one holder leaves the others");
  check(!account.release(RailHolder::GPS).cut, "two holders remain, so no cut");
  check(account.release(RailHolder::GROVE).cut, "the last of three cuts the rail");

  account.acquire(RailHolder::IR);
  account.reset();
  check(!account.held(), "reset drops every holder");
}

// The real sequence the board verification described: a failed GPS probe with
// an IR burst in flight keeps the rail on, and the last release turns it off.
void testFailedProbeWithIrInFlight(void) {
  RailAccount account;

  // GPS enables the receiver and takes the rail.
  check(account.acquire(RailHolder::GPS), "GPS enable takes the rail");
  // An IR burst is queued, so the infrared circuit takes its own hold.
  check(!account.acquire(RailHolder::IR), "the IR hold does not re-drive the rail");

  // The probe fails and the GPS driver releases. The cut must be refused.
  const RailAccount::release_t failedProbe = account.release(RailHolder::GPS);
  check(!failedProbe.cut, "a failed GPS probe must not cut the IR circuit's rail");
  check(failedProbe.held, "the IR hold is reported back to the caller");

  // The burst finishes; the last holder release cuts it.
  const RailAccount::release_t burstDone = account.release(RailHolder::IR);
  check(burstDone.cut, "the last holder release turns the rail off");
  check(account.mask() == 0, "no holder remains");
}

// The interlock: output mode only with an established, non-port supply. The
// failure direction is closed, because the vendor note warns of short circuit
// and device damage otherwise.
void testInterlock(void) {
  expectVerdict(RailEnableVerdict::ALLOW, "battery only allows output mode", true,
                RailSupply::BATTERY);
  expectVerdict(RailEnableVerdict::ALLOW, "USB/5VIN allows output mode", true, RailSupply::USB_5V);
  expectVerdict(RailEnableVerdict::REFUSE_EXTERNAL_SOURCE,
                "an external source on the 5 V port refuses output mode", true,
                RailSupply::PORT_5V);
  expectVerdict(RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY, "an unreported supply is refused", true,
                RailSupply::UNKNOWN);
  expectVerdict(RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY, "an unreadable supply is refused", false,
                RailSupply::BATTERY);
  expectVerdict(RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY, "an unreadable supply stays refused",
                false, RailSupply::PORT_5V);

  // PWR_SRC 0x04 bits 2:0 report 0 to 7, and the vendor enumeration names only
  // 0 to 3. The verdict is an allowlist, so every value the register can report
  // is decided here: 0 USB 5 V and 2 battery allow, 1 refuses as an external
  // source, and 3 with the undefined 4 to 7 refuse as an unknown supply.
  struct RawCase {
    uint8_t raw;
    RailEnableVerdict verdict;
    const char *what;
  };
  const RawCase raw_cases[] = {
      {0, RailEnableVerdict::ALLOW,                  "raw 0 is USB 5 V and allows output mode"    },
      {1, RailEnableVerdict::REFUSE_EXTERNAL_SOURCE, "raw 1 is the 5 V port and refuses"          },
      {2, RailEnableVerdict::ALLOW,                  "raw 2 is the battery and allows output mode"},
      {3, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,  "raw 3 is unknown and refuses"               },
      {4, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,  "raw 4 is undefined and refuses"             },
      {5, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,  "raw 5 is undefined and refuses"             },
      {6, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,  "raw 6 is undefined and refuses"             },
      {7, RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,  "raw 7 is undefined and refuses"             },
  };
  for (const RawCase &c : raw_cases) {
    expectVerdict(c.verdict, c.what, true, static_cast<RailSupply>(c.raw));
  }

  // An unreported supply refuses whatever value accompanied it.
  for (uint8_t raw = 0; raw <= 7; raw++) {
    expectVerdict(RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY,
                  "an unreadable supply refuses any value", false, static_cast<RailSupply>(raw));
  }
}

// The vendor numbering is what src/FurblePlatform.cpp static_asserts against, so
// pin it here too: a renumbering must not silently reinterpret a register.
void testSupplyNumbering(void) {
  check(static_cast<uint8_t>(RailSupply::USB_5V) == 0, "5VIN is 0");
  check(static_cast<uint8_t>(RailSupply::PORT_5V) == 1, "5VINOUT is 1");
  check(static_cast<uint8_t>(RailSupply::BATTERY) == 2, "battery is 2");
  check(static_cast<uint8_t>(RailSupply::UNKNOWN) == 3, "unknown is 3");
  check(Furble::RAIL_HOLDER_COUNT == 3, "three named holders");
}

// The verdict names are read in logs and simulator queries, so keep them
// stable.
void testVerdictNames(void) {
  check(
      std::strcmp(Furble::PlatformPolicy::railEnableVerdictName(RailEnableVerdict::ALLOW), "allow")
          == 0,
      "allow names");
  check(std::strcmp(Furble::PlatformPolicy::railEnableVerdictName(
                        RailEnableVerdict::REFUSE_EXTERNAL_SOURCE),
                    "refuse-external-source")
            == 0,
        "external source refusal names");
  check(std::strcmp(
            Furble::PlatformPolicy::railEnableVerdictName(RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY),
            "refuse-unknown-supply")
            == 0,
        "unknown supply refusal names");
}

}  // namespace

int main(void) {
  testFirstAcquireSwitchesOn();
  testCutIsRefusedWhileAHolderRemains();
  testRepeatedAcquireAndStaleRelease();
  testHoldersAreIndependentBits();
  testFailedProbeWithIrInFlight();
  testInterlock();
  testSupplyNumbering();
  testVerdictNames();

  if (g_Failures != 0) {
    std::cerr << g_Failures << " check(s) failed\n";
    return 1;
  }

  std::cout << "rail-account: PASS\n";
  return 0;
}
