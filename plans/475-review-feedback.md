# 475 - Review feedback for the SD shutdown acknowledgement

## Scope

This is the adversarial review record for #494 (`fix/sd-writer-ack`), which turns
`SD::powerOff()` into a handshake that reports a refusal and makes
`Platform::powerOff()` keep the rail up on one. It reviews the branch at
`1b44408c`, fixes what it found, and records what it judged false. The
implementation plan is [341-sd-shutdown-ack.md](341-sd-shutdown-ack.md). Every
finding is recorded as fixed, already fixed or a false positive, with evidence.

Line references are positions in the tree this PR produces on master.

## Method

Read the diff against the code it calls, not the prose: the writer task, the
queue ownership, `Platform::powerOff()`, `powerOffUntil()`, `restart()`,
`UI::doPowerOff()`, both automatic callers, the console path and the board rows.
Then prove the tests bite by putting the defect back in the scratch tree and
running the new boundary mode, and fix what the review found.

The test-teeth run reproduced the claim in the PR body exactly. With the take
result discarded (`(void)xSemaphoreTake(...)`, `return true`),
`boundary_sd_test shutdown-refused` prints `acknowledged=1 wait_ms=3000
unmount_entered=1` and fails four assertions, exit 1:

```
SD FAIL: a hung writer still reported an acknowledged power off
SD FAIL: the refusal came before the 3000 ms wait and its 5000 ms extension elapsed
SD FAIL: the extended-wait log is missing
SD FAIL: the refusal log is missing
sd_driver_test: 4 failure(s)
```

Restoring the branch source returned the suite to green, and `cmp` confirmed the
restored file was byte-identical to the branch version.

## Findings fixed

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | The handshake could be bypassed entirely. `SD::powerOff()` returned `true` (settled) whenever `request()` failed, and `request()` fails after 100 ms when the writer queue is full. A full queue is exactly what a wedged writer looks like: `SD::logPoint()` drops points with a zero timeout, so the queue stays full at 8, which plan 341's own deviation records. In that state the new refusal never ran and the rail dropped on the open track the fix exists to protect. | Fixed. Only `m_Queue == nullptr` (no SD slot, so no writer task and nothing open) returns `true`. A queue that will not take the request now logs `did not take the power off request` and returns `false` without waiting, because there is nothing to acknowledge. | `src/FurbleSD.cpp:797-813`; new mode `boundary-sd-shutdown-undelivered`, which fails twice against the old `return true` (`acknowledged=1`, missing log line). |
| 2 | The refusal left the automatic paths retrying on the next one-second pass, with no bound. `m_PoweringOff` is cleared, the inactivity clock is untouched, so `processAutoOff()` fires again immediately; the low-battery countdown deadline is also already past, so `processLowBattery()` calls `doPowerOff()` on every pass. Each pass blocks the UI task for the whole handshake (8 s, or 100 ms plus a queue send attempt), so on a wedged writer the panel stays lit but the UI is stalled almost continuously, forever. Plan 341 recorded the low-battery half and deferred it as a "future fix", and did not record the auto-off half at all. | Fixed. `AutoOff::retryBlocked()` and `POWER_OFF_RETRY_MS` (30 s, wrap-safe) hold both automatic paths off after a refusal. An explicit Off or console request is not gated, because the user retrying is new intent. | `include/FurbleAutoOff.h`; the gate in `src/FurbleUI.cpp:13167` and `:13343`; `tests/host/auto_off_policy_test.cpp` covers the boundary and the 32-bit wrap. |
| 3 | Two user-visible claims in the new documentation were false. `docs/power/board-power.md` said the user sees "GPX logging still running", but a wedged writer drops the queued points, so tracking stops advancing; the settings paragraph in the same PR said points are dropped, contradicting it. The same section said the critical warning box "returns and the countdown restarts on the next one-second pass". `doPowerOff()` had already closed the box, `m_LowBatteryWarned` stays true and `m_LowBatteryPowerOffPending` stays set, so the box never returned and the deadline never restarted: the device retried silently. | Fixed both ways. The docs now state that tracking does not advance while the writer is stuck and that the box returns. `UI::doPowerOff()` re-shows the critical box when the low-battery shutdown is pending, so the documented behaviour is the real one. | `docs/power/board-power.md` section 3.6, `docs/settings-and-controls.md`; `src/FurbleUI.cpp:13398`. |
| 4 | The head was red on three checks. Two are this PR's own doing: master landed the doc-citation gate (#511) after this branch's base, and the branch adds five lines to `src/FurblePlatform.cpp` without re-pointing the citations below them. The failures were `plans/341-sd-shutdown-ack.md:13` (`Platform::powerOff` cited at `:433`, defined at `:442`), `plans/implemented/13-auto-off-low-batt.md:223` (`M5PM1::shutdown` cited at `:453`, now `:458`) and `plans/implemented/26-pm1-watchdog.md:203` (`Platform::update` cited at `:992-1020`, now `:997-1025`). The third, `Measure firmware coverage and enforce the floor`, is `E: Failed to fetch ... libasound2t64 ... 404 Not Found` from `security.ubuntu.com` during the SDL/ALSA install, which no source change can cause. | Fixed. All citations re-pointed, including the two more that this review's own `FurbleUI.cpp` edit shifted (`plans/34-ota-partitions.md` `UI::task`, `plans/implemented/26-pm1-watchdog.md` `vTaskDelay`). `python3 tools/check_doc_citations.py` now reports 0 errors of 2774 citations in 149 files. The coverage job needs a re-run, not a code change. | CI logs on `1b44408c`; `python3 tools/check_doc_citations.py`; `python3 tools/pre_review.py --base origin/master`. |
| 5 | The boundary-case count claim went stale. `CLAUDE.md`, `AGENTS.md`, `docs/sim.md` and `docs/sim-coverage.md` said `sh sim/scripts/run-boundary-conformance.sh` runs 18 linked cases. That number counted the script's selection, 17 `boundary-*` cases plus `real-wifi-lifecycle`; the script selects 20 on this branch (19 `boundary-*` plus `real-wifi-lifecycle`). | Fixed in all four files. | `ctest -N -R '^(boundary-|real-wifi-lifecycle$)'` reports `Total Tests: 20`. |
| 6 | The plan and the PR body implied the SD refusal and the M5PM1 watchdog ordering interact. `sdSlot` and `canPowerOff` are set on the same board row only for Core2, so the SD refusal is reachable on that image alone; the StickS3 reports no SD slot, so its only refusal is the pre-existing PMIC one, and Core2 compiles the watchdog branches out. | Fixed in the docs and plan 341: the reachable board is stated, and the watchdog sentence is scoped to what can happen. | `tests/host/board_profile_test.cpp` board table; `include/FurbleBoardCaps.h:25`. |
| 7 | The refusal comment in `UI::doPowerOff()` said "The PMIC refused", which is now only one of two sources. | Fixed to name both. | `src/FurbleUI.cpp:13382`. |

## Findings judged false, already fixed, or out of scope

| # | Claim | Judgement | Evidence |
| ---: | :--- | :--- | :--- |
| 8 | "The refusal strands an open GPX file, so the track is no worse off than before." | False. The file the refusal leaves open is the file the wedged writer was already holding; the pre-existing defect is what stranded it. `doDisconnect()` touches no SD or GPX API, so the camera teardown does not close the track either, and the refusal is what keeps the writer alive to close it later. | `UI::doDisconnect()` has no `SD::` or `GPX::` call; `SD::handleRequest()` closes the track only from the writer task. |
| 9 | "The refusal leaves the watchdog disabled." | False. `Platform::powerOff()` returns before its own `watchdogEnable(false)`, so the platform never disables it, which is what the plan claims. `UI::doPowerOff()` disabled it earlier for the BLE teardown and re-arms it from `Settings::WATCHDOG`, which is the same value the boot path arms it with (`src/main.cpp:471`). Residual, recorded, no change: if a `flash prepare` left the watchdog deliberately off and a refusal then fired, the UI would restore the configured value rather than the pre-call platform state. That is the safer direction and the flash path already restores it on `flash cancel`. | `src/FurblePlatform.cpp:442-451`; `src/FurbleUI.cpp:13381-13399`. |
| 10 | "The 5 s extension is presented as measured." | False. Plan 341 calls it a judgement call, not a measurement, and section 3.6 of `docs/power/board-power.md` says a chosen bound. The 30 s retry bound this review adds is recorded the same way. | `plans/341-sd-shutdown-ack.md`, `docs/power/board-power.md` section 3.6. |
| 11 | "`restart()` or `powerOffUntil()` reach the same writer and skip the handshake, and this was discovered late." | True and already recorded, not a defect in this PR. `Platform::restart()` runs `prepareRestart()` and `esp_restart()`, and `powerOffUntil()` with a non-zero delay programs the PMIC timer or the Plus2 RTC alarm and shuts down without the handshake. Both leave the track for the next mount to repair. Plan 341 lists them as an out-of-scope deviation and section 3.6 states it, so this is recorded rather than discovered later. | `src/FurblePlatform.cpp:176-179`, `:728-812`; docs 3.6. |
| 12a | "The undelivered refusal only fires for a wedged writer." | False in the strict sense, and recorded as a trade-off: eight points can sit in the queue while a healthy writer is inside one write, so a power off in that window also refuses and costs the user a retry. It is the safe direction and the backoff keeps an automatic path from hammering it. | plan 341 deviations. |
| 12 | "The extended wait can be reached repeatedly, so a wedge keeps the device alive indefinitely." | True for the count, which is by design, and no longer true for the rate. Finding 2 bounds the rate to one attempt per 30 s per automatic path; the count stays unbounded because the alternative is cutting the rail on an open track. A permanently wedged writer therefore drains a critical pack to brownout instead of dropping the rail, which is the same data outcome as a brownout today and is logged on every attempt. | Finding 2; plan 341 deviations. |
| 13 | "The console path reports the refusal." | Not a defect, recorded instead. The console task prints `queued: power off` and queues `UI::Request::POWER_OFF`; the UI task runs the handshake asynchronously, so the reply can only ever describe the queueing. A refusal is visible in the device log and, on a critical battery, in the returned warning box. The plan now says so. | `src/FurbleConsole.cpp:1908-1927`; `src/FurbleUI.cpp:8437`. |
| 14 | "The re-shown critical box double-frees or races the async close." | False. `closeLowBatteryWarning()` clears the two member pointers before `lv_msgbox_close_async()` is processed, so the new box is a distinct object and the old one is deleted on the next LVGL cycle. Both run in the UI task. | `UI::closeLowBatteryWarning()`; `UI::showLowBatteryWarning()`. |

## The review questions

**Can the refusal strand an open file after the camera is already
disconnected?** No. The camera teardown does not touch the SD writer, and the
track file is the one the wedged writer holds. What the refusal changes is that
the rail no longer drops while that file is open, so the writer still exists to
close it. The finding was the opposite direction: the refusal could be bypassed
by a full queue (finding 1), which did drop the rail.

**Can the extended wait be reached repeatedly, and is it bounded?** It could,
on every one-second UI pass, which is now bounded to one attempt per 30 s per
automatic path (finding 2). The number of attempts is deliberately unbounded.

**Is the 5 s figure chosen or measured, and does the plan say so?** Chosen, and
the plan and section 3.6 both say so (judgement 10).

**Does the refusal path leave the watchdog in the state it claims?** Yes for
both branches it describes: the platform returns before its own disable, and the
UI restores the configured value. The SD refusal itself cannot fire on the board
that has an M5PM1 watchdog, which finding 6 corrects in the docs.

**Do `restart()` and `powerOffUntil()` skip the handshake, and is that
recorded?** They skip it, and it was recorded before this review (judgement 11).

**Do the console and the low-battery path behave correctly on a refusal?** The
console reports the queueing and cannot report the outcome (judgement 13). The
low-battery path did not: the box never returned and the retry had no bound
(findings 2 and 3).

## Gates run

| Gate | Result |
| --- | --- |
| `boundary-sd-mount-write`, `-mount-fail`, `-full`, `-removed`, `-shutdown-refused`, `-shutdown-undelivered` | `100% tests passed out of 6`, `-shutdown-refused` 8.16 s |
| same six modes under ASan and UBSan | all pass, no sanitizer finding |
| `boundary-sd-shutdown-refused` against the discarded take result | 4 failures, exit 1, as the PR body claims |
| `boundary-sd-shutdown-undelivered` against the early `return true` | 2 failures, exit 1 |
| `python3 tools/pre_review.py --base origin/master` | `PASS: 13 gates` |
| `python3 tools/check_doc_citations.py` | 0 errors of 2774 citations in 149 files |
| `python3 tools/check_format.py --base origin/master` | `5 changed files are formatted` |
| `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` | SUCCESS, flash 59.2% |
| `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stack-core2` | SUCCESS, flash 30.4% |

## Advisory findings

`python3 tools/check_pr_hygiene.py` warns `[doc-coupling] src/FurbleUI.cpp:1: UI
source changed without a detected page or query token`. Checked by hand: the
change adds no page, widget, string or navigation token. The only visible
difference is an existing message box reappearing after a refusal, in a state no
scenario captures, so `docs/ui-walkthrough.md` and its screenshots are unchanged.
The `ui-screenshots` job certifies the captures it already takes.

## Size and merge

The m5stack-core-debug slot blocker is cleared on this head: CI reports
`flash_used_bytes 1715525` of `flash_slot_bytes 1740800`, 25,275 bytes of
headroom, after #518 merged. The 1,740,944 byte figure in the PR body predates
that. No size change was made in this PR and no log level was touched, which the
current repository policy requires.
