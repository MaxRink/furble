# 358 - Resiliency round three

## Motivation

Third narrow pass over failure handling, after [339](implemented/339-mqtt-retry-backoff.md)
(MQTT retry backoff), [340](implemented/340-gps-uart-install.md) (GPS UART install) and
[341](341-sd-shutdown-ack.md) (SD writer shutdown acknowledgement). The pass
looks for defects in NVS wear, watchdog coverage, retry storms, connect and
disconnect leaks, storage failure handling, discovered-but-unhandled states and
failures that reach a caller which discards them.

Two defects were contained enough to fix here, each with a host test that fails
before the change. The rest are ranked at the end with file:line evidence.
`FurbleBoard.cpp`, `FurblePlatform.cpp`, the simulator hardware model and the
board profile table were left alone: no finding needed a change there.

## Fix 1: the SD settings backup never completed, and a failed export destroyed the last one

### Defect

`SD::exportSettings()` opened `settings.txt` and looped over
`Settings::getAll()` (`src/FurbleSD.cpp:1133`), treating any `false` from
`serializeSetting()` as fatal. `serializeSetting()` returns `false` for
`Settings::COMPANION_PASSWORD` on purpose (`src/FurbleSD.cpp:266`): the backup
is plain text, and `importSetting()` refuses that key too
(`src/FurbleSD.cpp:617`). The loop therefore stopped at the password entry, and
returned `false`.

`Settings::getAll()` is an `std::unordered_map`, so the entry that ended the loop
was at an arbitrary position in hash order. Every export was a partial subset of
the settings and every export logged `Unable to serialize setting companion_pw.`
followed by `SD settings export failed.` The Storage page confirms nothing
either way, so the user saw no result at all. On the host boundary the same code
produced 486 bytes of a 1,575-byte set.

The second defect in the same function is the write order. The file was opened
with `"w"`, which truncates before any byte of the new copy exists. A card that
filled up partway through the loop destroyed the previous backup and left a
truncated `settings.txt` that a later import applies in part before rejecting a
line.

### Fix

- Skip `Settings::COMPANION_PASSWORD` explicitly, with the reason, instead of
  reading the exclusion as a serialization failure. Fail the export when no
  setting was written at all, because that file would overwrite a usable backup
  on the next import.
- Write `settings.tmp` and `rename()` it over `settings.txt` only after
  `fflush()`, `fsync()` and `fclose()` succeed. A write, flush, close or replace
  failure removes the temporary file and leaves the previous backup in place; a
  complete file is kept when only the replace failed, because it is then the
  only copy.
- Replace in two steps where the file system asks for it. FatFs `f_rename()`
  answers `FR_EXIST` when the destination name is already in use
  (`components/fatfs/src/ff.c`, `f_rename` name-collision branch) and
  `vfs_fat_rename()` maps that to `EEXIST`
  (`components/fatfs/vfs/vfs_fat.c:332`), while a POSIX `rename()` replaces. A
  bare `rename()` therefore passes on the host double and fails on the card from
  the second export on, which is every export a user runs after the first. The
  export drops the old copy and retries the rename when the first call reports
  `EEXIST`.

### What the user sees

`Export Settings` now writes one line per exportable setting. A full card logs
`Unable to flush settings export: File too large.` and `SD settings export
failed.`, leaves `settings.txt` exactly as the last successful export wrote it,
and leaves no `settings.tmp` behind. The companion password is still never
written to the card.

### Evidence

`boundary-sd-export-full` is a new mode in
`tests/host/boundaries/sd_driver_test.cpp`. It exports once, records the file,
changes one setting and exports again, then lowers `RLIMIT_FSIZE` to half of it
(the seam the existing `full` mode uses for a full card) and exports a third
time. It asserts the replacement, the production failure, an unchanged
`settings.txt` and no temporary file. The mode also defines `rename()` with
FatFs semantics, `EEXIST` when the destination exists, because the host libc
replaces and would hide the second defect above. It is registered in
`tests/host/CMakeLists.txt` under the `boundary-sd` resource lock, and
`sd.export_replace` is asserted by `sim/scripts/run-boundary-conformance.sh`.

| Run | Result |
| --- | --- |
| Fix 1 in place | PASS, exit 0: `sd.export_first bytes=1575`, `sd.export_replace replaced=1 bytes=1574`, `sd.export_full backup_preserved=1 bytes=1574` |
| `src/FurbleSD.cpp` reverted to `origin/master` | FAIL, exit 1: `the first settings export did not complete`, `the first settings export carries no settings`. Log shows `Unable to serialize setting companion_pw.` then `SD settings export failed.` |
| Only the password skip kept, temp file and rename reverted | FAIL, exit 1: `a failed export replaced the previous settings backup`, `backup_preserved=0 bytes=787` against the 1,575-byte first export |
| Bare `rename()`, two-step replace removed, FatFs semantics in place | FAIL, exit 1: `a second settings export did not replace the previous backup`, `a replacing export left its temporary file behind`, `replaced=0 bytes=1575` |
| `boundary-sd-mount-write`, `-mount-fail`, `-full`, `-removed` | PASS, exit 0 for each |

## Fix 2: a refused camera notification stayed missing on the phone

### Defect

`CompanionService::notifyCameras()` publishes the camera cache before it enters
the transport and only sends the records that differ from that cache
(`src/FurbleCompanionService.cpp:458`). The transport seam returns `void`
(`include/FurbleCompanionService.h:33`), and `CompanionGatt::notify()` drops the
`NimBLECharacteristic::notify()` result (`src/FurbleCompanion.cpp:982`, `:988`),
which is `false` when the controller has no free TX buffer or the link dropped
mid-call.

So a refused notification was invisible to the service, and the cache already
said the state had been published. `notifyStatus()` survives that case because
it re-sends every 30 s as a keepalive
(`src/FurbleCompanionService.cpp:284`). The camera path had no keepalive, so a
camera that connected, disconnected or failed while the phone's BLE stack was
busy stayed wrong in the app's list until that camera changed again, which for a
terminal state such as `lost` may be never.

### Fix

`CAMERA_KEEPALIVE_MS` (30 s, `include/FurbleCompanionService.h:252`) re-publishes
every camera record inside the existing decision
(`src/FurbleCompanionService.cpp:456`). The 1 s `CAMERA_NOTIFY_INTERVAL_MS` rate
gate still applies, so the cost is four short notifications every 30 s, the same
cadence the status packet already uses.

The cadence needs its own stamp. `notifyCameras()` is called once a second from
the companion task loop (`src/FurbleCompanion.cpp:722`), and it refreshed
`m_LastCameraNotificationMs` on every call that passed the rate gate, including
the calls that found nothing to send. The measured gap was therefore one second
forever and the keepalive could not fire outside a test that advances the clock
thirty seconds in one step.

`m_LastCameraNotificationMs` now means "last notification handed to the
transport", which is what `notifyStatus()` has always done, and the keepalive
measures from `m_LastCameraFullPublishMs`, written when every record is
published. Two stamps, not one: a single stamp taken from any send would let a
list that changes on every pass hold the measure at one second for as long as
any camera keeps moving, and a record the controller refused would stay missing
for exactly the case the keepalive exists to cover.

### What the user sees

The camera list in the companion app converges within 30 s of any refused
notification instead of keeping a stale row for the session.

### Evidence

`companion_gatt_test` drives the real `CompanionService` over `MockCentral` with
the mock clock. The assertions now call `notifyCameras()` once a second for
thirty calls, which is the production cadence, and require nothing before the
keepalive and every record at it; a single thirty-second jump would still pass
with the stamp defect above. Then they advance 1 s and require nothing, so the
rate limit still holds. Next `MockCentral` drops the next camera notification,
which is the only place a refusal can be modelled because the transport returns
no delivery result, and the test requires the keepalive to put that record back
on the phone. Finally one camera is toggled on every one-second call for thirty
calls and the test requires exactly one full re-publish in that window, which
fails if the keepalive measures from a single record instead of a full publish.

| Run | Result |
| --- | --- |
| Fix 2 in place | PASS, exit 0, `companion mock-central tests: PASS` |
| `src/FurbleCompanionService.cpp` reverted to `origin/master` | FAIL, exit 1: `FAIL: the keepalive re-publishes every camera record`, `1 FAILED` |
| Stamp refreshed on every call, test at the one-second cadence | FAIL, exit 1: `FAIL: the keepalive re-published nothing over thirty one-second calls`, `FAIL: the keepalive re-publishes every camera record`, `FAIL: the keepalive puts a refused record back on the phone` |
| Keepalive measured from the last single record instead of the last full publish | FAIL, exit 1: `FAIL: a list that changes every second still gets exactly one full re-publish in thirty`, `1 FAILED` |

## Ranked findings not implemented

Severity is user impact against cost. Every line number is against this branch.

| # | Severity | Class | Finding | Evidence | Why not here |
| --- | --- | --- | --- | --- | --- |
| 1 | HIGH | NVS wear, retry storm | A GPS option roller applies, saves NVS and restarts the receiver on every detent. Scrolling Rate from 1 Hz to 5 Hz runs four `GPS::reloadSetting()` calls. Each one parks the GPS task, flushes the UART, resets the parser, powers the rail, restarts the acquisition deadline and, at the default Auto baud, runs the autobaud ladder again, which then re-identifies the receiver (`MON-VER`, two attempts a second apart). | handlers `src/FurbleUI.cpp:9736`, `:9747`, `:9756`, `:9766`, `:9777`, `:9789`, all wired at `src/FurbleUI.cpp:10012`; `GPS::reloadSetting()` at `src/FurbleGPS.cpp:2325` calls `enable()` at `:354`, and `onProbeLocked()` at `:499` clears the backend | The fix is a value-changed guard plus a settle window in `addOptionMenu`; it changes UI input behavior, needs the screenshot matrix and docs, and would collide with the in-flight UI work |
| 2 | HIGH | Hot loop, repeated teardown | A low-battery power off that keeps failing retries every second with no new countdown. `m_PoweringOff` is cleared on a `false` return and `m_LowBatteryPowerOffPending` stays set, so the one-second policy pass calls `doPowerOff()` again at once. Each attempt disconnects the camera, pauses the intervalometer and releases a held shutter. After 341 that path is also reachable whenever the SD writer does not acknowledge, and each attempt then costs the full 8 s refusal wait. | `src/FurbleUI.cpp:13332` re-enters, `:13379` clears the guard, `:13339` is the entry point | Needs a bounded-retry policy with its own host test, and it interacts with the in-flight SD round that makes the path common |
| 3 | MEDIUM | Discarded result, storage | `Settings::save<T>()` cannot report a failed write: `saveValue()` ignores both `begin()` and `put()` and the API is `void`. The console `settings set` path then prints the new value as if it persisted, while `companion start`, `companion stop` and `companion password` deliberately verify with a read-back. A full or unwritable NVS therefore lies to the operator for every ordinary setting. | `src/FurbleSettings.cpp:380`, `include/FurbleSettings.h:323`, console arms `src/FurbleConsole.cpp:556` to `:1005`, verifying pattern at `:1013` | A faithful fix changes the save contract or adds a read-back to about ninety console arms, and the settings host stub discards logs, so today there is no red-first seam for a log-only change |
| 4 | MEDIUM | Shared mutable handle | `CompanionGatt` keeps one mutable `Preferences` member and uses it from three sites, against the documented operation-local handle rule, and ignores `begin()`. A refused open leaves `m_BondValid` true in RAM while nothing was persisted, so the next boot refuses the phone outside the pairing window. | member `include/FurbleCompanion.h:195`, uses `src/FurbleCompanion.cpp:607`, `:621`, `:633`, `:645` | All four sites hold `m_BondMutex`, so this is not a race today; a red-first test needs an NVS failure seam in the companion host target, which is a test-infrastructure change |
| 5 | MEDIUM | Half-done transfer | `importSettings()` applies the file key by key with no staging. A rejected value or a read error leaves the settings half applied, returns `false`, and does not restart, so the device runs a mix of old and new values with no rollback. Fix 1 guarantees the file is complete, which removes the truncated-file cause but not a hand-edited or older file. | `src/FurbleSD.cpp:1110`, per-key save `src/FurbleSD.cpp:349` to `:473` | Needs a staged apply or a validated-file-then-apply design, which is a larger change than this pass allows |
| 6 | MEDIUM | Policy surprise | Three consecutive SD point failures disable GPX logging and persist `SD_GPX` false. A card that is momentarily unavailable, a bump or a slow mount, silently turns a user setting off. | `src/FurbleSD.cpp:902`, `MAX_FAILURES` at `:60` | It is a deliberate policy, so changing it is a product decision; it also needs a decision on what the user should see |
| 7 | LOW | Discarded result | `CompanionGatt::indicate()` returns early for camera records and drops the `indicate()` result, while the settings and auth branch logs it. A refused list record leaves the phone's list short with nothing in the log. | `src/FurbleCompanion.cpp:992`, compare `:1008` | Log only: the value is visibility, and the mock NimBLE double always returns true, so a red-first test needs a failure knob in the shared BLE double; recorded here instead of touching a double another in-flight branch may use |
| 8 | LOW | Scheduled work lost | `WebUI::startManualLease()` stores the hold deadline and then ignores a refused `esp_timer_start_once()`. The release then waits for the next one-second supervisor pass instead of the lease deadline. | `src/FurbleWebUI.cpp:1313` | Bounded to one second, and the deadline check in `serviceTimerEvents()` already covers the late case |
| 9 | LOW | Test-double data race | `FujifilmVirtualCamera::writes()` handed out a reference to its write log while the peer write handler appended to it from a Control task, so a test that walked the log could read a reallocated buffer. Seen once under ASan as `heap-use-after-free` in `shutterWriteCount()` at `tests/host/companion_gatt_test.cpp:74`, called from `:692`. | `lib/testing/peer/FujifilmVirtualCamera.cpp:139`, `:794`, reader `tests/host/companion_gatt_test.cpp:74` | Fixed in the review of this branch, not left recorded. `writes()`, `notifications()` and `lastGeotag()` now return a snapshot instead of a reference, which is the fix the finding named. It could not be left as a recorded finding: the double backs the `companion-gatt` case that gates every pull request, the ASan abort had already failed one suite run, and the defect was characterised precisely enough to fix without a reproduction. |

## Inspected, no defect found

These were checked against the listed classes and are recorded so the next pass
does not repeat the work:

- MQTT retry backoff, classifier and the watchdog feed in the owner loop
  (`src/FurbleMQTT.cpp:400`).
- The WebUI supervisor and companion service loop subscribe, feed every pass and
  unsubscribe around the unbounded wait (`src/FurbleWebUI.cpp:359`,
  `src/FurbleCompanion.cpp:661`).
- `TimeKeeper` enforces its normal and checkpoint NVS budgets before every
  write, and deduplicates an unchanged record (`src/FurbleTimeKeeper.cpp:373`).
- The crash record lives in RTC memory and never writes flash
  (`src/FurbleCrashLog.cpp`), and `CrashLog::update()` throttles to 1 Hz.
- The OTA partition sink aborts and rolls the target back on every refused
  write, order error or digest mismatch (`src/FurbleOTAPartitionSink.cpp:107`).
- The IR task creates its RMT channel once, checks every error, and disables the
  channel on every path (`src/FurbleIR.cpp:264`).
- The feedback LED, speaker and vibration patterns stop on every terminal path
  and pause their timer when idle (`src/FurbleFeedback.cpp:482`).

## What hardware must confirm

Both fixes are proven on the host only.

- Fix 1 on an M5Stack Core with a card: export once, then fill the card (a small
  FAT partition or a large dummy file) and export again. The device must log the
  failure, keep the previous `settings.txt`, leave no `settings.tmp`, and an
  import of that file must still apply every setting it names. Confirm the file
  has no `companion_pw=` line.
- Fix 2 on a phone: pair an Android and an iOS companion, connect and disconnect
  a camera while the link is idle, and confirm the list converges. A stronger
  check is to induce a refused notification, for example by dropping the link
  mid-session, and confirm the record arrives again within 30 s without a state
  change. The host test cannot show whether the real controller refuses the
  notification.

## Implementation state

Done on `fix/358-resilience-round3`, rebased onto `b56acc88` (#505), `85b89931`
(#494), `a8082cc1` (#484) and the master it finally lands on. Two fixes, no hardware gate.

| Gate | Result |
| --- | --- |
| `boundary-sd-export-full` (new) | PASS, exit 0: `sd.export_first bytes=1575`, `sd.export_replace replaced=1 bytes=1574`, `sd.export_full backup_preserved=1 bytes=1574` |
| `boundary-sd-mount-write`, `-mount-fail`, `-full`, `-removed` | PASS, exit 0 |
| `companion_gatt_test` | PASS, exit 0, and it fails on the reverted tree as recorded above |
| `sh sim/scripts/run-boundary-conformance.sh` | PASS, `100% tests passed out of 27`, `Linked boundary conformance passed.`, including the `sd.export_replace` and `sd.export_full` tokens |
| Full host suite, `ctest --test-dir /tmp/b516 -j 4` | PASS, `100% tests passed out of 212` on the first run after the rebase. The rerun on the final tree reported `99% tests passed, 1 tests failed out of 212`: `control-tsan-wrapper-contract` timed out at its 10 s budget under four parallel jobs and passes standalone in 1.8 s, the same load-dependence the author's pass recorded |
| `python3 tools/check_sim_coverage.py` | PASS, 206 hardware entries, 20 crash entries, 11 fuzz targets, 6,912 permutations |
| `python3 tools/check_sim_scenarios.py`, `sh sim/scripts/check-doc-tokens.sh` | PASS |
| `python3 tools/check_doc_citations.py` | PASS, 0 errors of 2,973 citations |
| `python3 tools/pre_review.py --base origin/master` | PASS, 13 gates |
| Sanitized host suite | not re-run in the review pass; `host-sanitized.yml` runs it on the pull request, and the author's pass recorded `100% tests passed out of 196` |
| Simulator e2e | not run in this pass; no simulator source changed. `sim/scenarios/e2e/hw-boundary-sd.txt` is the boundary runner's scenario file, which `run-boundary-conformance.sh` reads |

## Deviations

- Plan number 358 is the next free number on `origin/master`; 327 to 357 are
  used by in-flight work elsewhere.
- Fix 1 also changes the export failure log from one line per setting to the
  single `SD settings export failed.` line the caller already printed. An export
  that would write nothing now fails instead of writing a header-only file.
- Fix 1 does not add UI feedback for a failed export. The confirmation box
  closes before the writer runs, and a result dialog would be a UI change with
  its own screenshot matrix. The log line is the only signal today, which is
  recorded as a gap rather than fixed.
- The 30 s keepalive in Fix 2 is a judgement call that mirrors the existing
  status cadence, not a measurement of how long a companion app tolerates a
  stale row.
- The branch was rebased onto the then-current `origin/master` five times while
  other pull requests merged (#476, #479, #487, #482, #485). The only conflict
  was the `plans/README.md` registry table, where the 342 rows landed beside the
  358 row; both rows were kept. Every gate above was rerun on the final tree,
  which is based on `7caf0468`. Master's #473 corrections added four lines to
  `src/FurbleUI.cpp` and `src/FurbleIR.cpp` above the points this doc cites, so
  the ranked line numbers moved by four and were recomputed against the rebased
  tree.
- The sanitized host suite passed `100% tests passed out of 196` on the final
  tree. One earlier run of the same tree aborted once in `companion-gatt`: ASan
  reported a heap-use-after-free in the test double's write log, ranked as
  finding 9, which did not reproduce in 48 stress runs and is not on a path this
  branch edits.
- Two defects in this branch were found by the pre-merge review rather than by
  the author's pass. Fix 1 wrote the backup beside the old copy but replaced it
  with a bare `rename()`, which fails on the card from the second export on
  because FatFs refuses an existing destination. Fix 2 added a keepalive that
  could not fire, because the stamp it measured from was refreshed by every
  one-second call. Both are fixed above, both carry a test that fails without
  the fix, and the test that shipped first did not catch either: the SD case
  never ran a second successful export and the companion case jumped the clock
  thirty seconds in one step instead of calling once a second.
- The linked boundary count in `AGENTS.md`, `CLAUDE.md`, `docs/sim.md`,
  `docs/sim-coverage.md` and `sim/CLAUDE.md` moved from 18 to 27, not 19: master
  registered six more cases with #505's pin-share work and two more with #494's
  shutdown handshake after this branch's base, so `85b89931` has 26 including
  `real-wifi-lifecycle`, and this change adds `boundary-sd-export-full`.
- `sim/scripts/run-boundary-conformance.sh` did not build
  `boundary_shared_pin_test`, while its `ctest -R '^(boundary-|real-wifi-lifecycle$)'`
  selection includes `boundary-shared-pin-*`. That is #505's regression, not
  this branch's, but the script is the runner whose case count this change
  corrects, so the target is added to its build list.
- The full host suite and the sanitized host suite ran in this pass. The first
  full run reported `control-tsan-wrapper-contract` timing out at its 10 s
  ctest budget while eight sanitizer build jobs shared the machine; the
  script passes standalone in 4.6 s and the rerun in the same build tree took
  6.0 s. Two incremental sanitizer builds aborted on a single clang `1 error
  generated` in one translation unit that rebuilt cleanly straight after, so
  the sanitizer numbers above come from a run that started from a consistent
  tree. The simulator e2e suite did not run, because no simulator source
  changed.
