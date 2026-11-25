# 341 - SD shutdown acknowledgement

## Motivation

The power-off path asked the SD writer task to close the track and unmount, and
then threw the answer away. `SD::powerOff()` posted `POWER_OFF` and called
`xSemaphoreTake(m_PowerOffDone, pdMS_TO_TICKS(POWER_OFF_WAIT_MS))` with the
result discarded (`src/FurbleSD.cpp:796`, wait bound at `:55`, 3 s). The writer
task is the only code that closes the GPX track file and unmounts the card, so
a slow mount or a slow FAT write that outlasted the wait left both open.

`Platform::powerOff()` then cut the rail a few lines later
(`src/FurblePlatform.cpp:433` runs the handshake, `:441` is the M5PM1
`shutdown()`; the AXP192 and IP5306 boards fall through to
`M5.Power.powerOff()`). Cutting the rail there loses the track tail written
after the last `fsync()` and leaves FAT in its dirty state, so the next mount
can need a repair.

The defect was structural, not just a missing branch: `void powerOff()` had no
way to report a refusal, so no caller could keep the device alive. That is why
this fix changes a public header rather than only the wait.

## Design

- `SD::powerOff()` returns `bool`. `true` means settled: either there is no
  writer to wait for, or the writer acknowledged. `false` means the card may
  still be writing.
- The wait is unchanged at 3 s (`POWER_OFF_WAIT_MS`), then extends once by 5 s
  (`POWER_OFF_EXTENDED_WAIT_MS`). One extension covers a slow mount or FAT
  flush; the 8 s total stays well inside the 45 s M5PM1 watchdog that the UI
  disables before it calls in.
- The refusal log names the writer task, and the log and `xTaskCreate()` share
  one `WRITER_TASK_NAME` so the name cannot drift:
  - warning: `SD writer task 'sd' did not acknowledge power off in 3000 ms, extending the wait.`
  - error: `SD writer task 'sd' did not acknowledge power off after 8000 ms, refusing to power off.`
- `Platform::powerOff()` returns `false` on a refusal, before its own watchdog
  disable and before `M5PM1.shutdown()` or `M5.Power.powerOff()`, and logs
  `SD writer did not acknowledge, refusing to power off`. The UI disabled the
  watchdog before the call for the teardown, and re-arms it from
  `Settings::WATCHDOG` on the `false` return.
- The existing power-off path is kept for the case with nothing to wait for: no
  SD slot (no queue, no writer task) or a queue that never took the request
  returns `true`, and the rail drops exactly as before.
- `UI::doPowerOff()` already handled a `false` return from the PMIC path, so the
  refusal reuses that path: it logs `Power off failed, resuming.`, restores the
  watchdog and clears `m_PoweringOff`.

## Sequence

1. The UI, the low-battery countdown or the console calls
   `Platform::powerOff()`.
2. `TimeKeeper` flushes the wall-clock checkpoint.
3. `SD::powerOff()` queues `POWER_OFF` and takes `m_PowerOffDone` for 3 s.
4. The writer task closes the GPX track, unmounts the card, publishes the card
   state and gives `m_PowerOffDone`.
5. On timeout the caller extends the wait by 5 s and logs which writer is late.
6. On a second timeout the caller logs the refusal and returns `false`.
7. `Platform::powerOff()` returns `false` with the rail up, before its own
   watchdog disable and before the PMIC call.
8. The UI re-arms the watchdog from `Settings::WATCHDOG` and resumes. Only an
   acknowledged handshake reaches the PMIC shutdown, and only then does the
   watchdog stay disabled, because the rail drops inside that call.

What the user sees: the device does not switch off. The camera has already been
disconnected by `doPowerOff()` before the handshake, the panel stays lit on the
page it was on, and GPX logging keeps running, because the track the writer is
stuck on is the data the refusal protects. On a critically low battery the
warning box returns and the countdown restarts on the next one-second pass.

## Implementation state

Done on `fix/sd-writer-ack`.

| Gate | Result |
| --- | --- |
| `boundary-sd-shutdown-refused` (new) | PASS, `acknowledged=0`, `wait_ms=8003`, exit 0 |
| `boundary-sd-mount-write`, `-mount-fail`, `-full`, `-removed` | PASS |
| Same test against the defect (take result discarded, `return true`) | 4 failures and exit 1: `acknowledged=1` after `wait_ms=3002`, wait bound not held, both log lines missing |
| `python3 tools/pre_review.py --base origin/master` | PASS, 10 gates |
| ASan plus UBSan build of all five SD modes | PASS, no sanitizer finding |

## Evidence

- Defect and fix: `src/FurbleSD.cpp` (`POWER_OFF_WAIT_MS`,
  `POWER_OFF_EXTENDED_WAIT_MS`, `WRITER_TASK_NAME`, `SD::powerOff()`),
  `src/FurblePlatform.cpp` (`Platform::powerOff()`).
- Boundary double: `tests/host/boundaries/sd_driver_test.cpp` holds
  `esp_vfs_fat_sdcard_unmount()` behind a condition variable, so a hung writer
  is representable. The new `shutdown-refused` mode asserts the return value,
  the elapsed wait, the two log lines, the still-mounted card state and the
  intact track after the write is released.
- Registration: `tests/host/CMakeLists.txt` (mode list plus `RESOURCE_LOCK`),
  `sim/scenarios/e2e/hw-boundary-sd.txt`,
  `sim/scripts/run-boundary-conformance.sh` and
  `sim/coverage/hardware_quirks.json` carry the new tokens.
- Docs: `docs/power/board-power.md` section 3.6, the Storage section of
  `docs/settings-and-controls.md`, and the SD row of `docs/sim-coverage.md`.

## Deviations and open items

- `Platform::powerOffUntil()` and `Platform::restart()` do not run the
  handshake. A timed shutdown (StickS3 PM1 timer or Plus2 RTC alarm) and a
  restart both leave the track for the next mount to repair, which is the same
  data-loss class as this defect. It is out of scope here and not covered by a
  test.
- The 5 s extension is a judgement call, not a measurement. No card was
  measured for worst-case mount or `fsync()` time on hardware.
- A wedged writer keeps its queue full. `SD::logPoint()` uses a zero timeout, so
  those points are dropped instead of blocking the caller, and the queue length
  is 8. Coverage of what is lost in that state rests on the existing dropped
  point behaviour, not on a new test.
- The critical-battery path retries `doPowerOff()` on every one-second pass
  while `m_LowBatteryPowerOffPending` stays set, so a wedged writer turns that
  into a refusal loop that stalls the UI for 8 s per pass. No policy change was
  made; a future fix should bound the retries.
- Hardware validation is pending: the refusal was proven on the host boundary
  only. Confirm on a Core with a card that pulling the card mid-write and asking
  for power off keeps the device alive and logs the refusal.
