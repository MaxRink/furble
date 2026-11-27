# 341 - SD shutdown acknowledgement

## Motivation

The power-off path asked the SD writer task to close the track and unmount, and
then threw the answer away. `SD::powerOff()` posted `POWER_OFF` and called
`xSemaphoreTake(m_PowerOffDone, pdMS_TO_TICKS(POWER_OFF_WAIT_MS))` with the
result discarded (wait bound `src/FurbleSD.cpp:57`, 3 s). The writer task is the
only code that closes the GPX track file and unmounts the card, so a slow mount
or a slow FAT write that outlasted the wait left both open.

`Platform::powerOff()` then cut the rail a few lines later
(`src/FurblePlatform.cpp:600-645` runs the handshake, `:458` is the M5PM1
`shutdown()`; the AXP192 and IP5306 boards fall through to
`M5.Power.powerOff()`). Cutting the rail there loses the track tail written
after the last `fsync()` and leaves FAT in its dirty state, so the next mount
can need a repair.

The defect was structural, not just a missing branch: `void powerOff()` had no
way to report a refusal, so no caller could keep the device alive. That is why
this fix changes a public header rather than only the wait.

## Design

- `SD::powerOff()` returns `bool` (`src/FurbleSD.cpp:797`). `true` means
  settled: either there is no writer to wait for, or the writer acknowledged.
  `false` means the card may still be writing.
- Two shapes refuse, because both leave the track open:
  - The writer did not take the request. The queue is full, which is what a
    wedged writer looks like once `SD::logPoint()` has filled it with points.
    There is nothing to wait for, so this returns `false` after the 100 ms send
    timeout instead of the full wait.
  - The writer took the request and did not acknowledge. The wait is unchanged
    at 3 s (`POWER_OFF_WAIT_MS`), then extends once by 5 s
    (`POWER_OFF_EXTENDED_WAIT_MS`).
- The refusal logs name the writer task, and the logs and `xTaskCreate()` share
  one `WRITER_TASK_NAME` (`src/FurbleSD.cpp:90`) so the name cannot drift:
  - error: `SD writer task 'sd' did not take the power off request in 100 ms, refusing to power off.`
  - warning: `SD writer task 'sd' did not acknowledge power off in 3000 ms, extending the wait.`
  - error: `SD writer task 'sd' did not acknowledge power off after 8000 ms, refusing to power off.`
- `Platform::powerOff()` returns `false` on a refusal, before its own watchdog
  disable and before `M5PM1.shutdown()` or `M5.Power.powerOff()`, and logs
  `SD writer did not acknowledge, refusing to power off`. The UI disabled the
  watchdog before the call for the teardown, and re-arms it from
  `Settings::WATCHDOG` on the `false` return.
- The plain off path is kept only for the case with nothing to wait for: no SD
  slot means no queue and no writer task, so `SD::powerOff()` returns `true` and
  the rail drops exactly as before.
- `UI::doPowerOff()` already handled a `false` return from the PMIC path, so the
  refusal reuses that path: it logs `Power off failed, resuming.`, restores the
  watchdog and clears `m_PoweringOff`.
- A refusal also starts a 30 s backoff for the two automatic paths
  (`include/FurbleAutoOff.h`, `AutoOff::retryBlocked()`), because every attempt
  blocks the UI task for the whole handshake. An explicit Off or console request
  is not gated: the user retrying is new intent. On a critical battery the
  warning box returns so the still-on device has a visible reason.

## Sequence

1. The UI, the low-battery countdown or the console calls
   `Platform::powerOff()`.
2. `TimeKeeper` flushes the wall-clock checkpoint.
3. `SD::powerOff()` queues `POWER_OFF`. If the queue will not take it, it logs
   the refusal and returns `false` without waiting.
4. Otherwise it takes `m_PowerOffDone` for 3 s.
5. The writer task closes the GPX track, unmounts the card, publishes the card
   state and gives `m_PowerOffDone`.
6. On the first timeout the caller extends the wait by 5 s and logs which writer
   is late. On a second timeout it logs the refusal and returns `false`.
7. `Platform::powerOff()` returns `false` with the rail up, before its own
   watchdog disable and before the PMIC call.
8. The UI re-arms the watchdog from `Settings::WATCHDOG`, clears its power-off
   latch, holds the automatic paths off for the backoff and resumes. Only an
   acknowledged handshake reaches the PMIC shutdown, and only then does the
   watchdog stay disabled, because the rail drops inside that call.

What the user sees: the device does not switch off. The camera has already been
disconnected by `doPowerOff()` before the handshake and the panel stays lit on
the page it was on. Tracking does not advance while the writer is stuck: the
points queued behind it are dropped by `SD::logPoint()`'s zero timeout, and the
track file already open is the data the refusal protects. Once the writer
catches up the next point reopens the track. On a critically low battery the
warning box returns and the automatic retry waits for its backoff.

## Implementation state

Done on `fix/sd-writer-ack`, including the review pass recorded in
[475-review-feedback.md](475-review-feedback.md).

| Gate | Result |
| --- | --- |
| `boundary-sd-shutdown-refused` | PASS, `acknowledged=0 wait_ms=8005 unmount_entered=1` |
| `boundary-sd-shutdown-undelivered` (new) | PASS, `acknowledged=0 queued=8 wait_ms=0` |
| `boundary-sd-mount-write`, `-mount-fail`, `-full`, `-removed` | PASS |
| Same test against the discarded take result | 4 failures, exit 1: `acknowledged=1` after `wait_ms=3000`, wait bound not held, both log lines missing |
| Same test against the early `return true` on an undelivered request | 2 failures, exit 1: `acknowledged=1`, the undelivered-request log missing |
| `python3 tools/pre_review.py --base origin/master` | PASS, 10 gates |
| ASan plus UBSan build of all six SD modes | PASS, no sanitizer finding |

## Evidence

- Defect and fix: `src/FurbleSD.cpp` (`POWER_OFF_WAIT_MS`,
  `POWER_OFF_EXTENDED_WAIT_MS`, `WRITER_TASK_NAME`, `SD::powerOff()`),
  `src/FurblePlatform.cpp` (`Platform::powerOff()`),
  `src/FurbleUI.cpp:13348` (`UI::doPowerOff()`) and
  `include/FurbleAutoOff.h` (the retry backoff predicate).
- Boundary double: `tests/host/boundaries/sd_driver_test.cpp` holds
  `esp_vfs_fat_sdcard_unmount()` behind a condition variable, so a hung writer
  is representable. `shutdown-refused` asserts the return value, the elapsed
  wait, the two log lines, the still-mounted card state and the intact track
  after the write is released. `shutdown-undelivered` fills the writer queue
  behind a held unmount and asserts the refusal, its log line, that it returns
  before a full wait, and that the released writer finishes the held unmount.
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
  test; section 3.6 of `docs/power/board-power.md` records it.
- The 5 s extension and the 30 s retry backoff are judgement calls, not
  measurements. No card was measured for worst-case mount or `fsync()` time on
  hardware.
- `sdSlot` and `canPowerOff` are set on the same profile row only for Core2
  (`tests/host/board_profile_test.cpp`). The SD refusal is therefore reachable
  on the Core2 image only. The M5PM1 watchdog ordering this plan describes is
  not a reachable combination with an SD refusal: the StickS3 reports no SD
  slot, so its only refusal is the pre-existing PMIC one.
- The console `power off` command reports `queued: power off` and its request
  result stays `ok`; it does not see the outcome, because the UI task runs the
  handshake asynchronously. A refusal is visible in the device log and, on a
  critical battery, in the returned warning box, not in the console reply.
- A permanently wedged writer keeps the device on: the automatic paths back off
  to one attempt per 30 s and never force the cut, so a critical pack drains to
  brownout rather than dropping the rail on an open track. That is the same data
  outcome as a brownout today, and the refusal is logged on every attempt.
- A momentarily full queue refuses too. Eight points can sit in the queue while
  a healthy writer is inside one write, so a power off in that window reports a
  refusal and costs the user a retry. That is the safe direction (the rail
  stays up), and the 30 s backoff keeps an automatic path from hammering it, but
  it is a behaviour change for a writer that is not wedged.
- Hardware validation is pending: the refusal was proven on the host boundary
  only. Confirm on a Core2 with a card that pulling the card mid-write and
  asking for power off keeps the device alive and logs the refusal.
