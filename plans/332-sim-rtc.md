# 332 - Battery-backed RTC in the simulator

Status: implemented on `feat/455-sim-rtc` for issue #455, delivered in #507.
Simulator only; no firmware behaviour changes. Issue #455 is closed against that
PR; the hardware item below stays open in this plan.

## Gap

`UI::Intervalometer` has two clock-dependent decisions, both gated on
`trustedWallClock()` (`src/FurbleUIIntervalometer.cpp:28`):

- `loadResume()` (`:96`) rejects a record whose wake time is more than an hour
  old, or earlier than the alarm margin allows.
- `startResume()` (`:185`) fires a late wake at once instead of waiting
  `RESUME_WAKE_MARGIN_S` from boot.

`trustedWallClock()` needs a valid sample whose source is not NVS, and an RTC
sample is only trusted when `status.rtc_available`. In the simulator that was
never true:

- `sim/hardware_model.cpp` carried only a static `rtc` capability flag
  (`:43`, `:1327`, `hw.rtc`). There was no clock behind it.
- The SDL build compiles M5Unified from source, but M5GFX's host I2C bus
  (`src/lgfx/v1/platforms/sdl/common.cpp`) fails every transaction, so
  `M5.Rtc.begin()` never installs a chip and `M5.Rtc.isEnabled()` is false.
- `TimeKeeper` therefore always fell to NVS or the retained-clock path, whose
  source is demoted and refused by `trustedWallClock()`.

`sim/scenarios/deep-sleep/intervalometer-deep-sleep-stale.txt` said so in its
header, and #450 had to rewrite the `stale`, `late` and `stable-id` scenarios
to assert the StickS3 M5PM1 behaviour instead. Neither trusted branch had any
simulator or host coverage.

## Model shape

The seam stays where the firmware reads it. The firmware build still calls
`M5.Rtc`; nothing in `FurbleUIIntervalometer.cpp` changed.

- `sim/hardware_model.cpp` gains a calendar state: mode (`off`, `unbacked`,
  `backed`), the register reading, a virtual-clock anchor, the voltage-low flag
  and the restored flag. `hwRtcRead()`/`hwRtcWrite()` are the chip; the reading
  advances with the virtual clock, so `wait` moves wall time.
- The board rows gain the two columns of `include/FurbleBoard.h` that decide the
  wake path: `rtcBatteryBacked` and `timedWake`. The StickC Plus2 row is the
  only `rtc-alarm` row, which is what `Sim`'s `Platform::canTimedWake()` now
  answers beside the StickS3 M5PM1 answer.
- `sim/wall_clock.cpp` is the seam: it defines `std::time()` and
  `TimeKeeper`'s existing `furble_sim_system_time_us`/
  `furble_sim_set_system_time_us` weak hooks, plus four new
  `furble_sim_rtc_*` hooks (present, battery backed, read, write).
- `src/FurbleTimeKeeper.cpp` grows `#if defined(FURBLE_SIM)` branches that use
  those hooks when `M5.Rtc` has no instance. The firmware compile is unchanged
  and a host target that wires a real `M5.Rtc` keeps priority, so
  `tests/host/time_keeper_persistence_test.cpp` still exercises the register
  path.
- Persistence: `<FURBLE_SIM_PREFS>.rtc` (or `FURBLE_SIM_RTC_STATE`) holds the
  register reading, whether it was set and the voltage-low flag.
  `Platform::powerOffUntil()` advances it by the sleep interval, `restart`
  persists it, a cold boot (`poweron`, `brownout`, `power_glitch`) discards it,
  and a scenario without `rtc_model` removes it.
- Seeds: `rtc_model off|unbacked|backed`, `rtc_epoch <unix-seconds>`,
  `rtc_clock_lost true|false`.
- Queries: `hw.rtc_model`, `hw.rtc_battery`, `hw.rtc_clock`,
  `hw.rtc_volt_low`, `hw.rtc_epoch`, `hw.timed_wake`, and the production
  answers `time.valid`, `time.source`, `time.rtc_available`,
  `time.rtc_battery`, `time.epoch_s`.

Deviation from the issue text: it proposed modelling the chip for the BM8563
boards. Only the StickC Plus2 has both a BM8563 and a timed wake
(`Board::TimedWake::RTC_ALARM`); the StickC, StickC Plus and Core2 rows carry a
backup cell but cannot self wake, so `loadResume()` clears the record at
`canTimedWake()` before any clock is consulted. The scenarios therefore seed
`hw_profile stickc-plus2` with `rtc_model backed`, which is a modelled cell
fitted to that chip.

## Scenarios

All under `sim/scenarios/deep-sleep/`, all registered in
`sim/scenarios/manifest.json`, all uncertified for the same reason as their
neighbours: the dedicated deep-sleep runner supplies the power-cycle state.

| Scenario | Assertion |
| --- | --- |
| `intervalometer-deep-sleep-rtc-stale.txt` | A two hour old record on a live chip is rejected: `platform.wake_marker no`, `camera.shutter_presses 0`, `ui.interval_state idle`, with `time.source rtc` and `time.valid yes` |
| `intervalometer-deep-sleep-rtc-valid.txt` | A record 30 s ahead is accepted and waits its own deadline: `ui.interval_state wait` after 20 s, then `finished` with one press |
| `intervalometer-deep-sleep-rtc-late.txt` | A record 5 s past fires at once: `ui.interval_state shutter`, then `finished` with one press |
| `intervalometer-deep-sleep-rtc-lost.txt` | The same stale record with the cell dropped is accepted: `time.valid no`, `time.source none`, `hw.rtc_clock lost`, then one press |

The stale and lost scenarios are the same fixture on either side of the clock
gate, so the pair proves the gate is the modelled chip and not a timing
accident.

## Evidence

Simulator build. The PlatformIO dependency cache came from the parent checkout
and LVGL 9.4.0, the version `dependencies.lock` pins, from a copy of the
managed component, because a concurrent PlatformIO run cleared the parent's
`managed_components/lvgl__lvgl` while the rebased build ran:

```
FURBLE_DEP_ROOT=<checkout>/.pio/libdeps/m5stick-s3 \
FURBLE_LVGL_DIR=/tmp/lvgl-9.4.0-sim \
FURBLE_SIM_BUILD_DIR=/tmp/furble-455-build sh sim/build.sh
Built /tmp/furble-455-build/furble-sim
```

Every figure below was re-taken on the rebased tree at `dc5be70f` and that
binary; the earlier run on the pre-rebase base produced the same results.

Each new scenario, run headless with `SDL_VIDEODRIVER=dummy
SDL_AUDIODRIVER=dummy` against that binary, exits 0 with every assertion
reported ok:

| Scenario | Assertions | Result |
| --- | --- | --- |
| `rtc-stale` | 10 | pass, logs `Ignoring stale intervalometer resume state` |
| `rtc-valid` | 8 | pass, `camera.shutter_presses 0` after 20 s, then 1 and `finished` |
| `rtc-late` | 6 | pass, 1 press, `finished` |
| `rtc-lost` | 10 | pass, `time.valid no`, one press |
| `rtc-persist` | 14 | pass, epoch 1767225600 then 1767225603 across the restart |
| `rtc-cold-boot` | 9 | pass, epoch 0 and `hw.rtc_clock unset` after the cold boot |
| `rtc-start` + `rtc-resume` | the runner | pass inside `run-deep-sleep.sh` |

Gates run, all passing:

- `FURBLE_SIM_BIN=... sh sim/scripts/run-deep-sleep.sh` (exit 0, 68 assertions,
  the RTC resume leg logs `rtc backed restored from the chip: registers set at
  1767225606 s`).
- `FURBLE_SIM_BIN=... sh sim/scripts/run-invalid.sh` (exit 0, 52 malformed
  scenarios rejected), which now covers `seed rtc_model`, `seed rtc_epoch` out
  of range and a trailing character.
- `FURBLE_SIM_BIN=... sh sim/scripts/check-hardware-quirks.sh` (exit 0,
  `Hardware quirk gate passed`).
- `FURBLE_SIM_BIN=... sh sim/scripts/check-preferences-lifecycle.sh` (exit 0,
  `preference ownership lifecycle check passed`).
- `python3 tools/check_sim_scenarios.py` (manifest complete, 419 entries: the
  408 of `dc5be70f` plus 11) and `python3 tools/check_sim_coverage.py` (200 of
  200 hardware entries modeled, 11 of 11 fuzz targets, 6,912 permutations).
- `sh sim/scripts/check-doc-tokens.sh` (all tokens present).
- `python3 tools/pre_review.py --base origin/master`: 10 of 10 gates pass
  (whitespace, diff hygiene, settings contracts, clang-format, C++ safety, CI
  workflow contract, simulator scenario manifest, simulator hardware coverage,
  coupling ratchet, scanner unit tests).
- Regression spot checks on the default (model off) path: `rtc` independent
  e2e scenarios `intervalometer-timing`, `interval-stop-reset`,
  `interval-back-trap`, `bulb-timer`, `restart-persist`, `ui-restart-persist`,
  `crash-record-survives-restart`, `timezone-manual-override`,
  `timezone-auto-gps`, `clock-wrap` and `clock-wrap-before` all exit 0 (11 of
  11).
- `time-keeper-persistence` and `time-keeper-policy` pass inside that suite.
  The first still compiles the production `M5.Rtc` register path because a host
  that wires the chip keeps priority over the model.
- `sh tools/run_sanitized_host_tests.sh --clean --jobs 6`: 194 of 194 tests
  passed out of 194 under ASan and UBSan (exit 0, total test time 96 s).
- The full 233-scenario e2e suite, the power gate and the fuzz suites were not
  run for this branch; the clock, intervalometer, restart and timezone subset
  above was, and every other deep-sleep scenario runs inside the deep-sleep
  runner that did pass.

## Implementation state

Delivered on `feat/455-sim-rtc`, rebased onto `origin/master` at `dc5be70f`.

- Model: `sim/hardware_model.{h,cpp}`, `sim/wall_clock.cpp`.
- Seam: `src/FurbleTimeKeeper.cpp` (`FURBLE_SIM` weak hooks only).
- Gates: `sim/FurblePlatformSim.cpp` (`canTimedWake`, `powerOffUntil`),
  `sim/driver.cpp` (seeds, `time.*` queries, `restart` persistence).
- Scenarios and manifest as above; docs in `docs/sim.md` and `sim/CLAUDE.md`.
- Also added: a `future` resume fixture (a minute ahead, inside the acceptance
  window and beyond the boot margin), the `deepsleep` reset reason, three
  malformed-seed fixtures for `rtc_model` and `rtc_epoch`, and one typed-seed
  validator entry in `sim/scripts/check-doc-tokens.sh`.
- Branch base: `origin/master` at `dc5be70f`. The work started from `4af87727`;
  master then took #471, #472, #478 and #477. Only `plans/README.md` conflicted,
  because both sides add rows: every master row is kept and the #455 ownership
  row and the 332 plan-list entry are added beside them. `sim/scenarios/manifest.json`
  merged without a conflict and holds all 408 base entries plus the 11 new ones,
  which `python3 tools/check_sim_scenarios.py` re-checks. `sim/CLAUDE.md` and
  `docs/sim.md` also merged cleanly, and #472 (the heap model) shares no source
  with this change.

Not covered: `M5.Rtc` itself, its register encoding and the BM8563 alarm IRQ are
not exercised on the host. The model asserts the sample `TimeKeeper` reads, not
the I2C transaction that produces it on hardware.
