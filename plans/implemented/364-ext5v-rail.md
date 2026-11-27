# 364 - EXT_5V rail ownership on the M5StickS3

Status: implemented (firmware, simulator, host tests and docs). Two hardware
gates stay open and are recorded in section 9: nothing here was measured on a
board, and the EXT_5V supply interlock has no bench evidence.

Scope: the switchable 5 V rail on the M5StickS3, which this tree used to model
as a GPS-only rail. The M5PM1 `PWR_CFG` 0x06 byte and its two Furble writers are
in scope; every other PMIC register is not.

## 1. The defect a board-level verification found

Verified facts from the StickS3 board verification that opened this plan:

1. `PowerBackend::Rail::GPS` **is** M5PM1 `PWR_CFG` bit 3. `M5Unified` 0.2.20
   `Power_Class::setExtOutput(true, ext_PA)` on `board_M5StickS3` calls
   `M5PM1_Class::setExtOutput()`, which is
   `bitOn(M5PM1_REG_PWR_CFG, 1 << 3)`
   (`.pio/libdeps/*/M5Unified/src/utility/power/M5PM1_Class.cpp`). The M5PM1
   1.0.6 driver names the same bit `M5PM1_PWR_CFG_BOOST_EN`, the
   "BOOST/GROVE(5VINOUT)" enable (`M5PM1/src/M5PM1.h`).
2. The M5PM1 low-power guide section 1 L2/L3A says EXT_5V feeds the Grove 5 V
   output **and** the infrared circuit. `docs/power/datasheet-register.md`
   already records the StickS3 product page as "EXT_5V feeds Grove, Hat EXT_5V,
   and IR TX/RX". The rail is not a GPS rail.
3. `src/FurbleGPS.cpp` cut that rail in two places: `onProbeFailed()` on a
   failed probe, and `enterRailOff()` on every duty-cycle `RAIL_OFF`. On the
   StickS3 that stopped IR remote triggering for good after one failed GPS
   probe, and browned out any Grove device on the port.
4. Two drivers wrote the same `PWR_CFG` 0x06 byte with independent
   read-modify-write. The LED policy went through Furble's own M5PM1 driver
   (`setLedEnLevel()` → `setPowerConfig(LED_CTRL, …)`, bit 4) while the rail
   went through M5Unified's PMIC object (`Power_Class::setExtOutput()`, bit 3).
   Both are non-atomic read-modify-write over I2C, so a lost update could leave
   the rail on while `setRail()` reported success by reading back the same bit
   it had just tried to clear.
5. A rail cut could land inside an IR burst. `IR::transmitSymbols()` blocks in
   `rmt_tx_wait_all_done()` for up to `RMT_TX_TIMEOUT_MS` (1000 ms) on the IR
   task, and nothing stopped the GPS task from cutting the carrier mid-frame.
6. The rail was written into output mode unconditionally, with no supply check
   at all. See section 5.

Two independent board-level verification passes agreed on findings 1 to 5,
including the lost-update race and the burst-interrupt hazard. Finding 6 came
from the second pass as a separate safety-critical requirement, and this plan
then applied it.

## 2. Why it matters

- IR remote triggering is a shipped feature on the StickS3. A single failed GPS
  probe (no receiver on the port, or an unplugged unit) stopped it silently
  until the next reboot, because the rail stayed down and the GPS driver only
  re-enabled the rail on its own 60 s retry.
- A Grove device on the port draws its 5 V from the same switch. Cutting it
  every duty cycle is a brown-out, not a power saving.
- A lost update is a correctness bug in the other direction: `setRail(false)`
  returning true while the PMIC still has the bit set means the GPS driver
  believed a cut took, so it did not restart a receiver it had stopped for the
  cut. The receiver then sat powered with its GNSS stopped, drawing current and
  producing no fix.
- Output mode with an external 5 V source on the port is the vendor's
  documented short-circuit and device-damage case.

## 3. The ownership model

One rail, one owner, many holders.

- **The owner is `Platform`.** `Platform::switchRailLocked()` is the only code
  that writes the rail bit, and it is only reachable from `Platform::setRail()`,
  `Platform::railAcquire()` and `Platform::railRelease()`. No other module
  touches `PWR_CFG` bit 3 (or, on the AXP boards, the EXTEN/LDOIO0 switch).
- **The holders are named circuits**, `enum class RailHolder { GPS, IR, GROVE }`
  in `include/FurbleRail.h`. `RailAccount` is a bit mask reference count: a
  count per holder, not per call, so a repeated acquire from one task cannot
  inflate it and a stale release cannot strand the rail on.
- **First holder switches on, last holder switches off.**
  `Platform::railAcquire()` drives the rail only when the account was empty or
  the cached state says it is off, and verifies the powered state by read-back
  before trusting the cache. `Platform::railRelease()` cuts only on
  `RailAccount::release_t::cut`, that is only when the last holder went away.
- **A cut is refused while another holder needs the rail.**
  `Platform::setRail(false)` returns false without writing the PMIC when the
  account is non-empty, and `Platform::railRelease()` returns false when other
  holders remain. Both refusals are counted (`railCutRefusals()`) so a refused
  cut can be told apart from a lost write.
- **GPS is one holder** (`GPS::setRailPower()` → `railAcquire`/`railRelease`
  with `RailHolder::GPS`). Its 60 s absent-receiver retry and the duty-cycle
  window both acquire, and both releases can be refused.
- **IR is one holder, held across the whole burst, taken on the IR task.**
  The IR task takes `RailHolder::IR` at the top of the dequeued work, calls
  `transmit()` and releases the hold after it returns, so the rail covers the
  burst and nothing else. `IR::fire()` is a queue send with a zero tick timeout
  and no rail or I2C call, which is what `include/FurbleIR.h` promises the UI
  task. Nothing about the RMT waveform changed, only the ownership around it.
  A queue-full trigger writes no rail state at all, so a dropped request cannot
  cut a burst already in flight and cannot pin the rail either
  (plans/implemented/364-review-feedback.md, B4).
- **GROVE is one holder** with no automatic client today. It exists so a Grove
  device can be kept powered across a GPS duty cycle instead of being cut with
  the receiver, and so the accounting is complete rather than a two-holder
  special case. Nothing in firmware acquires it yet: the console and UI expose
  the receiver hold only, and the Grove hold is available to a future consumer
  through the same public API.

The field case this pins (see the simulator scenario and the host test):
GPS enables and takes the rail, an IR burst is queued and takes its own hold,
the GPS probe fails and releases, the cut is **refused**, the rail stays
powered, the burst window ends, the last holder releases, the rail goes off.

## 4. One serialized `PWR_CFG` 0x06 path

- `Platform::updatePowerConfig(mask, value, before, after)` is the only writer
  of that byte. It performs read, masked write and read-back inside one
  critical section, using the M5PM1 1.0.6 `getPowerConfig()` /
  `setPowerConfig()` pair, which the vendor driver implements as a masked
  read-modify-write of `M5PM1_REG_PWR_CFG`.
- The rail bit (3) and the LED bit (4) both go through it.
  `Platform::setDisplayOff()` no longer calls `m_M5PM1.setLedEnLevel()`, and the
  rail no longer goes through `M5.Power.setExtOutput()`. One writer, one
  register, one critical section.
- The lock is `m_PowerConfigMutex`, a leaf lock. It is never held across a
  delay, an event wait or a call into `Control`, so the UI task cannot block on
  it and it cannot invert against `Control::m_Mutex`. On the AXP boards there is
  no second writer of the switch register, so nothing is serialized there.
- Lock order is `m_RailMutex`, then `m_PowerConfigMutex`. The reverse never
  happens. `railHolders()` and `railCutRefusals()` take only the rail mutex and
  never touch the bus, so a status reader cannot stall a switching task.

## 5. The output-mode interlock

The M5Stack documentation for EXT_5V warns that the interface defaults to INPUT
and that in output mode power may only be applied through USB or 5VIN,
otherwise there is a risk of short circuit and device damage. Firmware used to
write the port into output mode with no check at all, and M5Unified's
`config_t::output_power` default of `true` meant `M5.begin()` did it before any
Furble code ran.

- `cfg.output_power = false` in `Platform::init()`, under `#if FURBLE_HAS_M5PM1`,
  so the port keeps the vendor default of input mode until a holder acquires it.
  The guard is deliberate: on the AXP192 and AXP2101 boards the Grove 5 V output
  has no other owner, so clearing it at boot left the port unpowered with no
  client that could switch it back on. Those boards keep M5Unified's own
  `setExtOutput(true)` at `begin()`, and with it the low-battery cancel
  (plans/implemented/364-review-feedback.md, B2).
- Before a fresh off-to-on transition, `Platform::railOutputPermitted()` reads
  M5PM1 `PWR_SRC` 0x04 bits 2:0 through the vendor `getPowerSource()` and maps
  it to `PlatformPolicy::RailSupply`, which mirrors the vendor enumeration. The
  four static asserts in `src/FurblePlatform.cpp` make a vendor renumbering a
  compile error.
- `PlatformPolicy::railEnableVerdict(known, supply)` is an **allowlist**: only
  these two supplies allow output mode, and every other value refuses.

  | PWR_SRC 0x04 bits 2:0 | supply | verdict |
  | :--- | :--- | :--- |
  | 0 | USB/DC 5 V input (`5VIN`) | allow |
  | 1 | external 5 V on the bidirectional port (`5VINOUT`) | refuse as an external source |
  | 2 | battery only (`BAT`) | allow |
  | 3 | unknown | refuse |
  | 4 to 7 | undefined by the vendor enumeration | refuse |

  `railOutputPermitted()` clamps anything above `BAT` to `UNKNOWN` before the
  cast, because the vendor `getPowerSource()` returns the raw three-bit field.
  A denylist would have let 4 to 7 through (plans/implemented/364-review-feedback.md, B1).

- **Failure direction is closed.** A refusal leaves `PWR_CFG` 0x06 untouched,
  so the port stays in input mode, `Platform::railAcquire()` drops the hold it
  just took, and both `setRail()` and `railAcquire()` report false. The refusal
  is counted (`railEnableRefusals()`) and logged at WARN when the verdict
  changes, at DEBUG for repeats, and it is readable from the console with
  `rail status`.
- Switching the port **back** to input mode is always allowed: it is the vendor
  default and the safe direction, so no interlock, no supply read and no
  refusal path on a cut.
- Why battery is allowed: the receiver, the IR circuit and any Grove device are
  the reason the port exists, and refusing output mode on battery would disable
  GPS on every unplugged StickS3. The vendor sentence is read as the
  prohibition on feeding 5 V **into** the port while the boost drives it, which
  `5VINOUT` reports directly. The residual risk is in section 9.

## 6. The read-back contract

- `Platform::setRail()` and `Platform::railRelease()` return the PMIC read-back,
  never the request. A write the PMIC did not take, a cut refused for another
  holder, and an interlock refusal all return false.
- `GPS::railOffOrRestart()` keeps its promise from `include/FurbleGPS.h`: when
  the cut reports false and `m_UbxStopped` says the receiver's GNSS was stopped
  for it, it sends CFG-RST GNSS start. That path is now reachable for a refused
  cut as well as for a PMIC refusal, which is the new case the refcount creates.
- `GPS::setRailPower()` returns the read-back and logs at DEBUG when a switched
  board did not confirm the request; the platform logs the reason once.

## 7. What changed

- `include/FurbleRail.h` (new): `RailHolder`, `railHolderBit()`, `RailAccount`.
- `include/FurblePlatformPolicy.h`: `RailSupply`, `RailEnableVerdict`,
  `railEnableVerdict()`, `railEnableVerdictName()`.
- `include/FurblePowerBackend.h`: `Rail::GPS` renamed to `Rail::EXT_5V`, named
  by what it feeds, with the ownership contract in the enum comment.
- `include/FurblePlatform.h`: `railAcquire()`, `railRelease()`, `railHolders()`,
  `railCutRefusals()`, `railEnableRefusals()`, the serialized `PWR_CFG` helpers,
  `readRailState()`, and the rail/power-config mutexes.
- `src/FurblePlatform.cpp`: `cfg.output_power = false`, the rail manager, the
  serialized `PWR_CFG` path used by both the rail and the LED, the supply
  interlock, and the honest read-back.
- `include/FurbleGPS.h`, `src/FurbleGPS.cpp`: the receiver hold, and the
  documented refusal semantics.
- `include/FurbleIR.h`, `src/FurbleIR.cpp`: the IR hold across a queued or
  in-flight burst.
- `sim/FurblePlatformSim.cpp`, `sim/shim/FurbleIR.h`, `sim/hardware_model.*`,
  `sim/driver.cpp`, `sim/scenario_action.cpp`: the same accounting and
  interlock over the fake rail, a modelled burst window (`ir_burst_ms`), a
  modelled supply (`rail_supply`), the `rail.*` queries and the `ir-fire`
  action.
- `sim/scenarios/e2e/ext5v-rail-holders.txt`: the failed-probe-with-IR case.
- `sim/scenarios/e2e/ext5v-rail-interlock.txt`: the refusal case.
- `tests/host/rail_account_test.cpp` (new, CTest `rail-account`): the
  accounting and the interlock policy.
- Docs: `include/CLAUDE.md`, `src/CLAUDE.md`, `sim/CLAUDE.md`, `docs/sim.md`,
  `docs/power/board-power.md`, `plans/322-hardware-abstraction.md`.

## 8. Verification

Rebased onto master `4043fba0`, the merge of #488, where the plan number 346 was already taken by
the plans-directory cleanup, so this plan is 364 and its review record is
364-review-feedback.md; every reference in the branch moved with it. The
rebase had to reconcile this branch with two later master designs in the same
files, and kept both: `sim/hardware_model.cpp` owns the shared rail with its
`gps`/`ir` holders and its `rail.state`, `rail.holders` and `rail.refusals`
queries, while the simulator `Platform` runs the production `RailAccount`, the
last-holder cut, the refused cut and the `PWR_SRC` interlock over it. The
branch adds `rail.on`, `rail.supply`, `rail.interlock`, `rail.cuts_refused`,
`rail.enables_refused`, `rail.ir_held` and `rail.ir_bursts`, and `seed
rail_supply` and `seed ir_burst_ms` feed them. Master's own rail scenarios
(`hw-ir-rail-cut-in-flight`, `hw-pmic-pwr-cfg-lost-update`) and the two
scenarios this plan adds all pass on the merged model.

Gates on this tree:

- `pio run -e m5stick-s3` builds.
- `boundary-platform-rail` passes, and it is the target that compiles the
  production `src/FurblePlatform.cpp` and drives `railAcquire()`,
  `railRelease()`, `setRail()` and `setDisplayOff()`. Its `FurbleSD.h` shim
  and `platform_rail_support.cpp` follow master's `SD::powerOff()` returning a
  refusal instead of void.
- `rail-account` passes, including the raw `PWR_SRC` 0 to 7 walk.
- `console-commands` passes with the `rail status` command.
- The full host suite passes: 221 of 221.
- `ext5v-rail-holders` and `ext5v-rail-interlock` pass on the `m5stick-s3`
  board model, and so do `gps-ubx-rail-fixed`, `gps-ubx-rail-refused`,
  `hw-ir-rail-cut-in-flight` and `hw-pmic-pwr-cfg-lost-update`.
- `python3 tools/pre_review.py --base origin/master` passes.
- `sh sim/scripts/check-doc-tokens.sh` reports the tokens are present.

The first CI run on this head failed four checks, and each had a cause in this
branch rather than a stale base:

- `tests/build_inventory_exemptions.json` still listed `src/FurblePlatform.cpp`
  as hardware-bound, but `boundary-platform-rail` builds it, and the inventory
  test fails a redundant exemption. The entry is gone.
- `clang-tidy` reported `PWR_CLICK_THRESHOLD_MS` and `m_PMICClickTime` as
  unused private fields. Both belong to the synthetic click streak, which only
  runs on the boards without an M5PM1, so both are declared under
  `#if !FURBLE_HAS_M5PM1` now. `m_PMICClickCount` stays unguarded because
  `getPWRClickCount()` reads it on every board.
- The same job reported two `signed char` to `gpio_num_t` conversions in the
  two `powerHoldPin` casts. Both cast through `uint8_t` after the existing
  `>= 0` guard, so no negative pin can reach the GPIO call.
- `build (m5stack-core-debug)` was 293 bytes over the slot at 1,741,093 of
  1,740,800 on the pre-#524 base. With #524, #497 and #510 in the tree the
  lane builds at 1,736,688 of 1,740,800, 4,112 bytes left, so nothing was
  trimmed and no log level changed.

## 9. Not verified, and the gates that stay open

- **No board measurement.** Nothing here was run on hardware. Which Grove pin
  a StickS3 user actually loads, and how much current the IR circuit draws from
  EXT_5V, are unmeasured.
- **`PWR_SRC` on the StickS3 is unverified.** The interlock trusts M5PM1
  `PWR_SRC` 0x04 bits 2:0 to report an external source on the bidirectional
  port. The register is documented in the M5PM1 1.0.6 driver and used by
  M5Unified, but no capture in this tree shows what a StickS3 reports with a
  Grove device driving the port. Gate: probe the port with a 5 V source on a
  bench unit and confirm `5VINOUT` is what it reports.
- **The vendor's own permission is broader than this rule.** The M5Stack note
  says output mode is allowed when the board is powered through USB or 5VIN,
  while this rule refuses only an external source on the port and allows
  battery-only output. A bench check with a scope on the port, plus the
  StickS3 schematic, must settle whether output mode is safe on battery. Until
  then the rule above is the conservative direction that keeps the feature
  usable.
- **A source driving EXT_5V without `PWR_SRC` reporting `5VINOUT`** cannot be
  detected by firmware. It stays a wiring-level risk, documented for users.
- **The StickC family EXTEN routing to the Grove 5 V pin is UNVERIFIED.** The
  schematics were unreachable, so this plan changes nothing on those boards
  beyond the shared ownership API: the AXP192 EXTEN and AXP2101 LDOIO0 paths
  keep going through M5Unified, which already refuses external power on a low
  battery with no ACIN supply, and no `PWR_CFG` interlock applies to a PMIC
  that has no such register. Gate: obtain the StickC, Plus and Core2
  schematics, then decide whether those boards need an equivalent interlock.
- **Bench gate for the deferred cut.** `RMT_TX_TIMEOUT_MS` bounds the transmit
  wait, so the IR hold can be held for up to a second by a stalled channel.
  That is the intended bound, but a stalled-channel run on hardware has not
  been done.
