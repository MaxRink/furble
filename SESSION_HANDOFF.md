# furble session handoff

Written 2026-08-30 at the end of the 2026-08-28/29/30 merge-queue and
sim-realism session. This is an untracked working note in the main checkout,
same convention as `HARDWARE_DEBUG_NOTES.md`. **Do not commit it.**

Repo: fork `MaxRink/furble` for all work, upstream `gkoh/furble` is read-only.
`fork/master` at handoff: **70774434** (2026-09-02). The 2026-08-30 sections below are historical; read the 2026-09-02 addendum first.

---

## 0. 2026-09-02 addendum (read first)

Between 2026-08-30 and 09-01 another agent merged PRs #253 to #258: plan 158
scheduler parity (virtual clock, joinable tasks, orderly shutdown), task
quiescence, scheduler priority, truthful fuzz invariants, strict typed
scenario actions (`sim/scenario_action.{h,cpp}`, nav targets are TWO
allowlists: the parse-time page list and the canonical validator; every action
must publish an outcome via `m_SimActionResult`; hidden targets use
`action expect unavailable nav X`), and plan 160 scenario ownership
(`sim/scenarios/manifest.json`, a LIST of dicts; `tools/check_sim_scenarios.py`;
CI runs only certified entries). Plan 159 is the camera peer certification
design and evidence ledger, no peers yet.

Measured coverage audit on 792815cd (reports were in /private/tmp/cov-reports,
may be gone): host 57.4 %, sim 58.1 %, union **66.0 %** of firmware lines; 10
files compiled by neither harness (19.5 % of SLOC). Ranked plan: R1 real
Control/Camera/CameraList/Scan in the sim (pivot, in flight as
`feat/sim-real-control-2`, plan 161), R2 restart seam (#251), R3 real
FurblePlatform.cpp in the sim, R4 console in host (DONE, #259, plan 162, 0 to
90 %), R5 vendor sweep (gated by plan 159 provenance), R6 feedback/IR in sim,
R7 three-panel coverage floor in CI, R8 power calibration.

Merged 2026-09-02: #247 (focus ring dedup), #246 (Level on the main menu; home
rows padding 3 on 135x240 fit seven rows at all text sizes, zero padding on
80x160; new IR-enabled scenarios home-seven-rows*.txt; device audit on
dev+g223c99ad: labels 7, issues 0), #259 (console host coverage plus
tests/test_build_inventory.py gate with tests/build_inventory_exemptions.json;
a commented-out CMake line no longer counts as built).

Open and where they stand: #251 restart seam at 55e1ed93 (three review majors
fixed: post-restart failure cancels the reboot, queue reset before IDLE with
DISCONNECTING held, zombies reaped via production reapZombieTargets; 87/87
host; targeted re-review in flight; merge next, then R1 absorbs it). #252
sync points at 996a8ab5, plan 157, firmware byte-identical proof, 86/86,
independent review in flight. #245 Fujifilm follow-ups at aec2c7d2, 84/84,
waits on the X100VI (unreachable on 09-02). R1 not yet pushed.

Later on 2026-09-02: MERGED #252 (sync points, plan 157), #260 (coverage floor,
plan 163; tools/coverage.py, monotonic ratchet, clang-18 pinned; every PR gets
a measured coverage table and a floor check; grand union 69.0 percent), #262
(plan 156 follow-ups, ASan control_e2e target). master 153c38b7. Open: #261 R1
(request-changes: nondeterministic reconnect indicator under the real stack,
fuzz deadlock on Camera::m_Mutex held across connect delays, wall-clock
scheduler stall bound, deleted assertions, host thread in Scan.cpp; fix round
running; when it lands ratchet the sim floors with --lower --reason), #245
(parked on the X100VI bench gate). Load incident: reviewers' orphaned CPU
spinners (ppid 1 shell-snapshot shells plus yes) drove load to 135 for hours;
kill them and check uptime whenever agents look slow.

Later still on 2026-09-02: R1 (#261, 2e1084dc, CI 33/33, re-review running)
found a REAL PRODUCTION BUG through the real-Control sim: UI::doConnect()
readies the connect timer while Control::connectAll() has only queued
CMD_CONNECT; if the timer ticks before the control task leaves STATE_IDLE the
handler pauses the timer and nothing resumes it, so the connected page runs
with a dead liveness poll and later drops are never surfaced. Hardware masks
it by preemption order (priority-4 control task). Fix PR owed right after #261
merges (same UI/Control files), with hardware verification; #261 carries
documented xassert expected-fails on ui.reconnecting until then (plans/161
residual gap 4). Second latent bug from fuzz: 320x240 intervalometer page
overflow at seed 3 (pinned XFAIL). Coverage grand union 70.26 (floor 69.26).
GPS status page PR #263 (plan 164) open, review running. #245 still parked on
the X100VI. Never point a build's managed_components at the main checkout: an
agent's symlinked build pruned lvgl__lvgl and h2zero__esp-nimble-cpp there;
restored by copy from ~/furble-build-wt/.mc-seed.

Environment 2026-09-02: OrbStack VM `furble-build` recreated (it had
vanished) with clang, clang-format 21.1.5, SDL2, sim deps at
~/furble/sim/.pio/libdeps/sim; NEVER share ~/furble between concurrent jobs,
give each verification its own `git worktree` there (a concurrent checkout
silently tested the wrong branch once). Stick now on /dev/cu.usbmodem2101,
running dev+g223c99ad (equals master UI). Console driver restored durably at
~/furble-build-wt/serdrive.py, logs in ~/furble-build-wt/bench-logs/.
PlatformIO pipx venv pinned to Python 3.13 and its IDF tooling venv rebuilt
(cryptography/pydantic have no 3.14 wheels); a warm S3 firmware build is about
5 to 12 minutes. Neither camera reachable on 09-02 (GR IV fully off, not in
standby); infinite-reconnect retry cadence measured at ~10 s on the revert build.

---

## 1. Standing goal

Iterate until all plans are done and every PR is reviewed, working, and merged.
The terminal milestone is the first alpha release with working release and app
CI, fork-flasher links, and full release notes. A session Stop hook enforces
the goal and clears itself when met.

The user's operating preference: maximum autonomous progress, no-input tasks
prioritized over hardware-gated ones when there is a choice. Ask only for
decisions that are genuinely theirs.

---

## 2. Hard constraints

These have caused real damage when violated. Carry them forward verbatim.

- **The main checkout is protected.** `/Users/A92615428/git/GitHub/gkoh/furble`
  sits on branch `docs/gps-indoor-feasibility` (HEAD 7b300e78) and is dirty on
  purpose: modified `src/`, `include/`, `lib/furble/`, plus untracked `sim/`,
  `tests/`, `FurbleMQTT.{h,cpp}`, `HARDWARE_DEBUG_NOTES.md`. Never reset,
  clean, rebase, checkout, or stash it. All PR work happens in worktrees under
  `~/furble-build-wt/`.
- **Always qualify GitHub commands** with `-R MaxRink/furble`. Without it `gh`
  resolves to upstream and returns "Could not resolve to a PullRequest".
- **Never push to upstream gkoh/furble.**
- Every PR needs: an independent code review by a separate agent, a
  `plans/NN-*.md` doc plus a row in `plans/README.md`, clang-format 21 clean
  (`clang-format --dry-run -Werror`), 2-space indent, and **no em-dashes or
  en-dashes anywhere** (code, comments, docs, commit messages, PR bodies).
- Camera protocol changes must cite their data source in the PR body.
- Pushes to fork PR branches are pre-authorized by the user (standing, saved in
  memory). Hardware flashing is pre-authorized. Merging is the coordinator's
  call once review, CI on the exact head, and hardware gates all pass.


### User directives added this session

- **Builds run inside the OrbStack VM** with build files stored in the VM.
- **All work must be pushed to git.** Do not leave committed-but-unpushed or
  uncommitted work in a worktree when an agent dies. Push it, mark it clearly
  as unverified in the commit message and PR body, and let review catch up.
---

## 3. What the user cares about right now

The simulator must catch bugs before hardware does. Their words after the
incident below: *"This should have been covered by the simulator."* Sim and
host realism work outranks new features until that gap is closed. A related
standing rule: if the sim fails, do not flash hardware; the sim is the gating
check and hardware is reserved for what is physically irreducible.

---

## 4. The incident that shaped this session

PR #159 (reconnect origin plumbing plus boot-time session restore) was merged
after passing **single-target** console gates, then broke the real two-camera
workflow on the bench. Three distinct failures:

**Failure 1: multi-target disconnect wedge.**
Two targets selected. The Ricoh GR IV was in BLE standby, which behaves
erratically: it accepts the BLE connect, sometimes fails `secureConnection`
with rc=520 (connection timeout), and sometimes completes the full handshake
and then drops the link about 20 s later after emitting a CameraPower `0x00`
notify. The X100VI connected normally. While the connect cycle churned against
the flappy camera, the user issued a disconnect. Result, captured live over the
console:

```
control.state: disconnecting
control.targets: 2
control.connect_in_progress: true
control.connect_abort: true
control.connecting: X100VI
```

`STATE_DISCONNECTING` is terminal for the control task, so every later connect
was refused. Permanent until reflash.

**Failure 2: boot restore loop.**
After a reboot, #159's session restore re-armed both targets and entered an
endless connect cycle against the flappy standby camera. NimBLE logged
`Disconnected client not found, conn_handle=1` repeatedly. A full user restart
did not recover the device; manual connects stayed refused because state never
left `connecting`.

**Failure 3: false-connected UI.**
The furble Connected screen stayed up while **neither** camera had a live link
(confirmed on both camera displays). Only a user restart cleared it. Related
prior art: `plans/75-false-connected.md`.

**Resolution.** PR **#248** reverted the #159 merge (41a765d1) and is merged.
The revert build `dev+g0ee1d06f` was flashed and the user confirmed both
cameras connect again after a restart. Master is back to known-good behavior.

**Reland requirements for #159** are written into PR #248's body: a
multi-target hardware gate, a reboot-restore gate, host tests for a
multi-target cycle with a never-connecting peer, and a re-audit of the rebase
conflict resolutions in `connectAll` / `startConnectRequest` against the PR
#242 cancel token. (During that rebase I moved #242's `clearConnectCancel()`
loop from `connectAll` into `startConnectRequest`; that resolution needs a
second pair of eyes when #159 relands.)

**Caveat the user raised, still open.** Because the original validation was
single-target only, PRs #242/#243/#244 are **not exonerated** either. The
multi-target disconnect scenario should be regression-tested on reverted master
before anything relands. A host repro built during the gap analysis found **no
wedge on reverted master** (16/16 runs plus a 25-iteration stress sweep across
disconnect landing points), which is strong evidence the wedge was #159's, but
it is not a hardware confirmation.

---

## 5. Merged this session

In order: #237, #28 (IMU spirit level, stack 101), #140 (GPS rail-cut and
degraded recovery), #232 (Fujifilm registration gate), #238 (setState sleep-lock
ordering, mutation-proven), #239 (geotag reconnect confirmation, 16.1 s
hardware-verified), #240 (failed-connect reclaim ordering), #241 (Secure
saved-scan dual-UUID matcher, 3/3 hardware reconnects), #242 (teardown connect
cancel token, hardware gated), #243 (GR IV sleep-shutter gate plus a
gatt-journal use-after-free fix with a mutation-proven ASan test), #244
(vendored esp-nimble-cpp 2.5.0 with the taskRelease use-after-scope fix,
survived a 4-minute GR IV soak that crashed the previous build), #159 (later
reverted), #248 (the revert), #249 (sim UI liveness invariant), #250 (host
flappy-peer realism).

#93 was closed as superseded by #229 plus #232.

---

## 6. Open PRs

### Active queue (this session's work)

| PR | Branch | Head | CI at handoff | State |
|---|---|---|---|---|
| #245 | `fix/fujifilm-registration-followups` | `aec2c7d2` | 31 pass | Rebased onto 14a05f39, 84/84 host. Waiting on hardware. |
| #246 | `feat/level-main-menu` | `846cdb9b` | rerunning | Unverified WIP fix pushed as head. |
| #247 | `fix/menu-focus-outline-dedup` | `49d3b2f2` | 30 pass | Reviewed, approved, mergeable. |
| #251 | `test/restart-restore-seam` | `5590d416` | 30 pass | Review was in flight and did not finish. |
| #252 | `test/interleaving-hook` | `3b5ce4f7` | rerunning | Pushed by coordinator after its agent was interrupted. |

**#245, Fujifilm registration follow-ups.** Three items in one PR: Secure
stale-bond recovery salvaged from closed PR #93 (delete the local bond only on
the definitive rejection signature: previously bonded AND link still up AND not
cancelled, so a Ricoh-style rc=520 link-dropping timeout does **not** delete
the bond); a public `FURBLE_HOST_REGISTRATION_TIMEOUT_MS` define wrapped in
`#ifndef` (this required splitting the host-build detector, which now keys off
`ESP_PLATFORM`); and doc reconciliation of the `ad06c7b7` GEOTAG_UPDATE
semantics across plans 75, 76, and 95. New tests `fujifilm-stale-bond` and
`fujifilm-registration-cancel`, both mutation-verified. Plan 151.
**Owed:** X100VI bench gate. Delete the pairing on the camera side, then
reconnect and confirm the stale bond is cleared and a fresh pair succeeds; plus
a normal saved-reconnect regression. Firmware for this exact branch is
**already built** at
`~/furble-build-wt/wt-232-followups/.pio/build/m5stick-s3-debug/`.

**#246, spirit level on the main menu.** Adds a second button for the existing
Level page on the main menu (the IR dual-button pattern), a static
`m_LevelMainButton`, `showIMUWidgets()` gating extended to it, a sim
`nav level_main` action and `ui.level_main_button_visible` query, plus
scenarios `level-main-menu.txt` and extended `imu-gating*.txt`. Plan 153.
**CI failed after the rebase** with:
```
ASSERT FAILED: ui.overflow expected 'no' got 'yes'   (ui.page = main)
```
in the sim-e2e step "Assert modeled-page matrix (135x240, 80x160, 320x240)".
The seventh home row overflows. An **unverified WIP fix** is now the head
commit `846cdb9b`: home rows use padding 5 instead of 6 on
`FURBLE_M5STICKC_PLUS`/`FURBLE_M5STICKS3` (`src/FurbleUI.cpp` around line 1502,
in `UI::addMenuItem`), other pages keep 6. The implementing agent was killed
before it could run the matrix or retake screenshots, so the commit message
says UNVERIFIED. **Next agent must** build the sim for all three panel sizes
and run the matrix scenario before trusting it. If seven rows genuinely cannot
fit 80x160 even fully trimmed, the documented fallback is to allow scrolling on
that panel only and mark that assert as an exception with a comment, keeping
the assert strict on the larger panels.

**#247, drop the duplicate focus ring.** Excludes `lv_menu_cont_class` from the
`outlineTarget` set in the theme apply callback (`src/FurbleUI.cpp` around line
1221), so menu rows keep only LVGL's accent fill and lose the green 3 px ring;
switches, rollers, sliders, and plain buttons keep the ring because LVGL gives
them no focus fill. Scenario `menu-focus-outline.txt` asserts flipped from 1 to
0. Verified: 70/70 sim, 77/77 host at the time, pixel-scan screenshots on
StickS3 Dark, StickS3 Default, StickC Dark showing zero green outline pixels on
menu rows and 34 green pixels still present on a focused Display-page control.
Plan 152. **Owed:** a quick on-device look. Otherwise ready to merge.

**#251, restart/session-restore seam.** The #159 reland gate. Sim gains a
`restart` verb implemented by re-exec of the sim binary with
`FURBLE_SIM_RESTART_STEP` continuation, skipping the fresh-scenario NVS wipe so
`FURBLE_SIM_PREFS` persists like flash (in-process teardown was evaluated and
rejected: the sim's FreeRTOS-shim tasks are detached host threads with no stop
protocol). Host gains `Control::resetForTest()` in `src/FurbleControl.cpp`
guarded by `FURBLE_HOST_CONTROL_TEST`, which only the `control_e2e_test` target
defines. New scenarios `restart-persist.txt` (sim) and
`control-e2e-restart-restore-commandable` (host: healthy plus flappy target,
cycle, reset mid-churn, boot-restore re-arm, disconnect lands IDLE under 3 s
with a continuous 3.5 s no-late-republish watch, manual connect reaches ACTIVE).
Reported 83/83 host, 73/73 sim. Mutation evidence: suppressing the interactive
`disconnect()`'s `setState(STATE_IDLE)` publish reproduces incident failure 1
and fails three checks. Two other mutation candidates were absorbed by
redundant abort channels and are documented honestly in the plan doc. Plan 156.
**Owed:** the independent review (it was dispatched and did not finish) and the
plan-number collision with #252.
Known caveat from the agent: its local firmware build failed in the ESP-IDF
**bootloader subproject** because of the corrupted `framework-espidf` package
described in section 9, reproducible on pristine master, so unrelated to the
change. That package has since been repaired; a local firmware build should now
succeed and is worth redoing before merge.

**#252, deterministic interleaving sync points.** Closes the last unforceable
race class. Adds `include/FurbleTestSync.h` with `FURBLE_TEST_SYNC_POINT`,
compiled to nothing in firmware (`FURBLE_TEST_SYNC` is never defined there),
three named points in `src/FurbleControl.cpp` (`connectall_returned`,
`idle_connect_dequeued`, `disconnect_abort_armed`), a host controller under
`tests/host/testsync/` that installs barriers with per-park timeouts, and
`tests/host/control_interleave_test.cpp` which parks the control task holding a
`STATE_DISCONNECTING` result while the main thread completes `disconnect()` to
IDLE, then releases and asserts the guard suppresses the late republish. The
agent reported the mutation works: removing the republish guard fails the test
with 5 red checks. **Owed and stated in the PR body:** the firmware size
comparison against a clean master baseline proving the points compile out, and
a full host suite tally on the final tree. Plan 156, which **collides with
#251**; #251 opened first, so #252 should renumber to 157.

### Older open PRs, untouched this session

Mostly conflicting with master and needing a rebase before anything else:
#214 (Android companion auth), #195 (iOS/macOS companion apps), #177 (sim
interval deep sleep), #166 (companion password), #161 (MQTT sim, mergeable),
#139 (GPS phase 2), #90 (WebUI), #75 (companion cameras characteristic), #66
(MQTT client), #65 (GPS motion, draft), #63 (pairing codes), #59 (interval deep
sleep), #53 (WiFi provisioning, mergeable), #48 (IMU hardware motion), #47
(dead reckoning), #45 (IMU gestures).

---

## 7. Sim and host realism: the gap analysis and its remediation

This is the user's stated priority. A read-only gap analysis established
exactly why none of the three hardware failures were reachable.

### The structural finding

There are **two disjoint test stacks**, and nothing bridges them.

**Stack A, the sim.** It runs the *real* `src/FurbleUI.cpp` over a **fake
Control**. `sim/FurbleControlSim.cpp` replaces the whole state machine with a
750 ms timer: `connectAll()` sets CONNECTING, `getState()` flips to
ACTIVE/CONNECT_FAILED after `CONNECT_DURATION_MS`, and `disconnect()`
unconditionally clears targets and returns IDLE. There is no DISCONNECTING
logic, no `connect_abort`, no `connect_in_progress`, no zombie drain, no
reconnect loop, no BLE. `sim/CameraSim.cpp` reduces `Camera::connect()` to
`return !Sim::connectShouldFail()`. The entire BLE fault vocabulary was one
boolean seed plus `action drop`. Consequence: wedges, reconnect loops, and
false-connected are **unconstructible by design**, because UI state and link
truth flow from the same trivial code path.

**Stack B, the host tests.** Real Control plus real `lib/furble` cameras plus
`MockNimBLE` plus virtual peers. This stack was already rich (per-address peer
routing, connect fail/delay counts, `mockDropLink`,
`mockMarkLinkDeadEventPending` for the rc=520 model, a `RicohVirtualCamera`
that models GR IV standby). What it lacked was a **scenario**: nothing ever
composed two heterogeneous targets with one persistently flappy peer and a
disconnect mid-cycle. Also `RicohVirtualCamera::subscribe()` discarded its
callback so it could not emit notifications, the rc=520 peer was buried inside
one test, there was no absent-peer scan model, no restart seam anywhere, and no
deterministic interleaving control (sub-50 ms races unforceable).

### Landed remediation

- **#249** (plan 155): driver-tick liveness invariant in `sim/driver.cpp`
  (`checkLivenessInvariant()`), grace period defaulting to 3000 sim-ms and
  overridable with `seed liveness_grace_ms N`, opt-out with
  `seed liveness_check false`, a `ui.liveness_violations` query, a `link_lies`
  seed plus `action link-lies-kill` that kills the fake cameras' link truth
  while Control stays ACTIVE, and scenarios
  `multi-connect-false-connected.txt` and `link-lies-invariant.txt`. The
  invariant runs in every scripted scenario by default. Reviewed and approved.
- **#250** (plan 154): flappy-peer mode `setFlappy(fail_attempts, drop_after_ms)`
  on both `FujifilmVirtualCamera` and `RicohVirtualCamera`, Ricoh subscription
  storage plus `emitNotification()`, `SecureTimeoutPeer` promoted to
  `tests/host/peer/`, absent-peer scan model via
  `NimBLEDevice::setScanAbsentAddress()` filtering `NimBLEScan::emitResult`,
  control-e2e scenarios `multi-flappy-disconnect`, `flappy-cancel-stress`,
  `flappy-peer-autonomous`, plus `ricoh-control-flap` (the repo's first
  Ricoh-through-Control coverage) and `absent-peer-scan`. Reviewed; one major
  finding (the scenario did not actually exercise the `connect_in_progress`
  window because the disconnect landed inside the 2500 ms first-retry wait) was
  fixed by arming `setConnectDelayMs(800)` **before** `mockDropLink`.

### Still owed, wave 3

1. **The long-term fix: run the sim against the real Control.** Replace
   `FurbleControlSim` with `src/FurbleControl.cpp` plus MockNimBLE and the
   virtual peers. The host `control_e2e` harness proves they compile on host and
   the sim already links the real UI. This is the root cause behind failure
   mode 3 and is rated large. Everything else is a workaround.
2. **A latent false positive in #249's invariant.** The fake control's
   reconnect-off partial-drop path deliberately stays ACTIVE with
   `connected < targets` forever ("stay active to keep serving them",
   `sim/FurbleControlSim.cpp` around line 312), while the UI honestly shows a
   "lost" status. That is an honest partial outage, not the false-connected bug,
   but it diverges past any grace period. No current scenario hits it (all
   connect-two/drop scenarios seed `reconnect true`), but the first
   multi-connect plus reconnect-off plus partial-drop scenario will need
   `seed liveness_check false` for correct behavior. Either narrow the predicate
   (exclude when the reconnect/lost indicator is presented) or document the
   exception in `docs/sim.md`.
3. **The invariant does not run under fuzz mode** (`fuzzActive()` early-returns
   before the check). Worth extending.
4. **CI glob discipline.** `sim-e2e.yml` runs only `sim/scenarios/e2e/`;
   `power-gate.yml` matches only top-level `sim/scenarios/*.txt`. A scenario
   placed in any other subdirectory silently never runs. Always add new
   scenarios to `sim/scenarios/e2e/`.

---

## 8. Bench state and hardware procedures

- **Device:** M5StickS3 on `/dev/cu.usbmodem1101`, currently running the PR
  **#248 revert build `dev+g0ee1d06f`**, which the user confirmed works with
  both cameras after a restart.
- **Saved cameras:** index 0 `X-E5`, index 1 `X100VI` (`58:5E:B0:EF:23:76`),
  index 2 `RICOH GR IV` (`34:90:EA:BB:7D:73`).
- **X100VI is offline** as of the user's last message. The GR IV was last seen
  in BLE standby.
- **Do not open the serial port while the user is at the bench.** Opening it
  can reset the device, and queued console commands interleave with their UI
  interaction. Ask or wait.

### Console driver

`serdrive.py` lives in the session scratchpad at
`/private/tmp/claude-503/-Users-A92615428-git-GitHub-gkoh-furble/c391dccf-d875-4fae-b4fc-925218e9e8ba/scratchpad/serdrive.py`.
**Copy it somewhere durable immediately**: session scratch directories get
wiped, and that already happened once mid-session and destroyed the shared
libdeps donor.

```
python serdrive.py /dev/cu.usbmodem1101 <logfile> <total_timeout_s> "cmd;sleep:N;cmd"
```

It is drop tolerant: it reopens the port when USB-CDC drops during light sleep
and logs `PORT_DROP` / `PORT_OPEN`. Use the pipx python:
`/Users/A92615428/.local/pipx/venvs/platformio/bin/python`.

Useful console commands: `version`, `status`, `debug control`, `debug camera`,
`debug ble`, `debug tasks`, `cameras list`, `connect [index]`, `disconnect`,
`shutter press|release`, `scan start|stop|list`, `gps ...`, `settings get|set`,
`flash prepare|cancel`, `reboot`.

`debug control` is the workhorse; it reports `state`, `targets`, `connected`,
`zombies`, `connect_in_progress`, `connect_abort`, `sleep_lock_held`,
`infinite_reconnect`, `reconnect_backoff`, `reconnect_attempt`, `next_origin`
(only on the #159 branch), `adaptive_*`, `rssi_*`, and `connecting`.

### Flashing

```
cd <worktree> && FURBLE_VERSION=dev FURBLE_TEST=0 \
  python3 tools/flash_prepare.py --port /dev/cu.usbmodem1101 --env m5stick-s3-debug
```
This handles the M5PM1 45 s watchdog disarm and re-arm. After a flash, use a
deadline-based retry loop to reopen the console; the USB serial port
re-enumerates on reboot and a single fixed-delay open returns nothing.

**Rescue if the device hard-hangs:** furble disables all M5PM1 power button
gestures at boot, so there is no software reset path. Hold the side button
while replugging USB until the green LED flashes, then reflash.

### Hardware gates still owed

- **#245**: X100VI stale-bond recovery, needs the camera powered on.
- **#246 / #247**: on-device look at the new home row and the ring-free menu
  rows.
- **#63**: Ricoh GR IV numeric-comparison pairing gate; wake the camera
  properly first.
- **#159 reland**: the full multi-target gate (two targets, one absent or
  flappy, disconnect mid-cycle, reboot mid-session, liveness confirmed on the
  cameras' own displays rather than furble's self-report).

---

## 9. Build environment

### OrbStack VM (new, per user directive)

Machine **`furble-build`** exists and is validated: Debian bookworm arm64,
cmake 3.25.1, clang-format 21.1.5, SDL2, build-essential, git, python3-pip,
pipx. The repo is cloned at `~/furble` inside the VM with build dirs under
`~/build/`. **Verified: full host suite 82/82 pass in the VM on master.**

```
orb -m furble-build bash -c 'cd ~/furble && git fetch --quiet origin <branch> \
  && git checkout -q FETCH_HEAD \
  && cmake -S tests/host -B ~/build/host && cmake --build ~/build/host -j8 \
  && ctest --test-dir ~/build/host --timeout 180'
```

Two gotchas found the hard way:

- **Keep the build directory path short.** The `gpx-writer` host test writes
  paths into a `char[64]` buffer. `~/build/host` is fine;
  `~/build/fix-menu-focus-outline-dedup` produced 21 spurious GPX failures that
  look exactly like a code defect. If a file-I/O test fails and the expected
  output file is missing, measure the path length before reading any code.
- **Firmware flashing stays on the macOS host.** OrbStack has no USB
  passthrough. Build wherever, flash from the host.

### macOS host

- PlatformIO via pipx: `~/.local/bin/pio`, python at
  `~/.local/pipx/venvs/platformio/bin/python` (PlatformIO 6.1.19). Do **not**
  use the obsolete `.platformio/penv` path.
- Every build needs `FURBLE_VERSION=dev FURBLE_TEST=0`.
- The global git fsmonitor breaks the first TinyGPSPlus install in a fresh
  libdeps dir. Re-run the same pio command once, or prefix with
  `GIT_CONFIG_GLOBAL=/dev/null`.
- **Shared libdeps donor:** `~/furble-build-wt/wt-restart-seam/.pio/libdeps` is
  currently a real populated directory. New worktrees should symlink to it:
  `mkdir -p .pio && ln -sfn ~/furble-build-wt/wt-restart-seam/.pio/libdeps .pio/libdeps`.
  The old donor under `/private/tmp/furble-connect-context-fix` was wiped when
  the temp directory was cleared; several worktrees still hold dangling
  symlinks to it, which surface as
  `FileNotFoundError: .../.pio/libdeps/m5stick-s3-debug`.

### The framework-espidf corruption (fixed, but know about it)

The PlatformIO `framework-espidf` package became corrupted: its own
`esp_log_level.h` referenced `CONFIG_BOOTLOADER_LOG_LEVEL` while its own Kconfig
tree no longer defined it, breaking the **bootloader subproject** on pristine
master sources. Fixed by moving it aside to
`~/.platformio/packages/framework-espidf.corrupt-bak` and letting PlatformIO
re-download 5.5.3. Delete the backup once you are confident.

**A cold firmware build after that repair took 32 minutes.** Do not assume a
build stalled at `Reading CMake configuration...` is hung; that line precedes a
long silent dependency download and component fetch. I killed two builds on
that wrong assumption and lost about an hour. Poll the log for `SUCCESS` or
`FAILED` markers instead, and capture the real exit code with
`echo "PIO_EXIT=$?"` immediately after the command rather than through a pipe.

---

## 10. Plan number ledger

On master: 141 through 150, plus **154** (#250 flappy-peer realism) and **155**
(#249 sim liveness).

Claimed by open PRs and not yet on master:

| Number | PR | Doc |
|---|---|---|
| 151 | #245 | `151-fujifilm-registration-followups.md` |
| 152 | #247 | `152-menu-focus-outline-dedup.md` |
| 153 | #246 | `153-level-main-menu.md` |
| 156 | #251 | `156-restart-restore-seam.md` |
| 156 | #252 | `156-control-test-sync-points.md` **collision** |

Next free after resolving: **157** for #252, then 158.

**Every rebase in this family conflicts on `plans/README.md`.** The resolution
is always keep-both, ordering the rows by plan number. The same is true for
`add_test(NAME ...)` blocks in `tests/host/CMakeLists.txt`: keep both, order is
irrelevant to cmake.

---

## 11. Test suite state

- Host suite on master: **82 tests** (`ctest --test-dir <build>`), 64
  `add_test` entries because several are `foreach` loops. #245 brings it to 84,
  #251 to 83.
- Control end-to-end scenarios (each registered individually so a failure names
  the scenario): `fresh-connect`, `dead-camera-disconnect-no-freeze`,
  `connect-after-dead-disconnect`, `stale-session-reconnect`,
  `false-connected-guard`, `transient-connect-recovers`,
  `client-pool-exhaustion`, `multi-connect-fujifilm`, `reconnect-shutter-drop`,
  `multi-flappy-disconnect`, `flappy-cancel-stress`, `flappy-peer-autonomous`,
  and on #251 `restart-restore-commandable`.
- Sim e2e scenarios: **73** in `sim/scenarios/e2e/`, all green on master.
- CI workflows: `main.yml` (host ctest plus firmware matrix), `sim-e2e.yml`,
  `camera-tests.yml`, `protocol-tests.yml`, `power-gate.yml`,
  `reproducible.yml`, `ui-screenshots.yml`, `android.yml`, `release.yml`,
  `pages.yml`.
- **Mutation verification is mandatory** for any test claiming to guard a
  specific fix. The pattern: back the file up outside the repo, revert exactly
  the fix, rebuild **only that test target**, run the binary directly, confirm
  the expected failure signature, restore, rebuild, confirm green, then run the
  full suite. Never leave a mutation in the tree.

---

## 12. Immediate next steps, in order

1. Let CI settle on **#246** and **#252**, then read the results.
2. **Validate or replace the #246 padding WIP.** Build the sim for all three
   panel sizes and run the modeled-page matrix:
   - default 135x240: `sh sim/build.sh`
   - 80x160: `FURBLE_SIM_FURBLE_BOARD=FURBLE_M5STICKC FURBLE_SIM_M5GFX_BOARD=board_M5StickC`
   - 320x240: `FURBLE_SIM_FURBLE_BOARD=FURBLE_M5COREX FURBLE_SIM_M5GFX_BOARD=board_M5Stack`
   Run with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`. Touch a source file
   between builds to force a rebuild; the binary always lands at
   `sim/build/furble-sim` regardless of env vars. Retake screenshots.
3. **Finish the #251 review**, resolve the plan-number collision (#252 renumbers
   156 to 157), finish #252's owed size comparison and suite tally, then merge
   both. #251 is the #159 reland gate, so it should land before any reland work.
4. **Merge #247** (approved, one on-device look owed) and **#245** once the
   X100VI is back for its bench gate. #245's firmware is already built.
5. **Wave 3 sim realism**: the real-Control sim, then the liveness invariant's
   partial-outage predicate, then the #159 reland behind its full gate.
6. Then the older backlog:
   - **#59 wire IDs** (audit already done): the branch has `IVL_SLEEP` at 42
     (free, keep) and `IVL_SLEEP_THR` at 43, which **collides** with
     `AUTO_OFF_CHARGING` on master. Reassign to 45 or 47 (46 is IMU, 48 is
     reserved by draft #65 for `GPS_MOTION`), then regenerate golden fixtures
     with `make -C tests/protocol generate` and commit the `.bin` files with the
     change.
   - **GPS status page detail** (user request, scoped): tier A is about 25 lines
     in the `addGPSDataMenu` timer lambda around `src/FurbleUI.cpp:5757-5795`,
     adding fix state, source, HDOP, and the degraded/retries line, all of which
     already exist in `GPS::status_t` and `getCycleStatusSnapshot()`. **Watch
     out:** the sim `gps_fix`/`gps_satellites` queries at
     `src/FurbleUI.cpp:3561-3599` parse that page's labels **by child index**,
     so new labels must be appended at the end or that block must be updated.
     Tier B (power/standby state, rate, last-sentence age, AID state) needs new
     public accessors and mutex-ordered getters in `include/FurbleGPS.h`.
   - **#63** pairing codes (hardware gate owed), **#65** GPS motion.
   - WiFi/MQTT track: #53, #66, #161, #90. Note the flash budget decision: MQTT
     gets a `FURBLE_MQTT` build switch for tight 4 MB boards; Waveshare ETH and
     headless S3 keep full MQTT.
   - Companions: #214, #195, #75, #166. Then #45, #47, #48, #139, the Nordic
     port baseline, and finally the alpha release.

---

## 13. Worktree map

| Path | Branch | Note |
|---|---|---|
| `~/furble-build-wt/wt-232-followups` | `fix/fujifilm-registration-followups` | #245, firmware built and ready to flash |
| `~/furble-build-wt/wt-level-main` | `feat/level-main-menu` | #246, WIP padding fix pushed unverified |
| `~/furble-build-wt/wt-focus-border` | `fix/menu-focus-outline-dedup` | #247, clean |
| `~/furble-build-wt/wt-restart-seam` | `test/restart-restore-seam` | #251, clean except stray `.pio-build*.log`; **holds the live libdeps donor** |
| `~/furble-build-wt/wt-interleave` | `test/interleaving-hook` | #252, clean |
| `~/furble-build-wt/wt-159-rebase` | `revert/159-reconnect-origin` | merged revert; also holds the #159 rebase history worth re-reading before a reland |
| `~/furble-build-wt/wt-nimble-taskdata` | `fix/nimble-taskrelease-race` | #244, merged |
| `~/furble-build-wt/wt-flappy-peer` | `test/flappy-peer-realism` | #250, merged |
| `~/furble-build-wt/wt-sim-liveness` | `test/sim-ui-liveness` | #249, merged |
| `~/furble-build-wt/wt-ricoh-sleep-gate` | `fix/ricoh-sleep-shutter-gate` | #243, merged |

Many older `reb-*`, `pr*`, and `wt-*-hwtest` worktrees are stale. Before
removing any, classify each: dirty means preserve, no upstream means preserve,
clean plus pushed is safe to `git worktree remove` (never `--force`), then
`git worktree prune`. Several `/private/tmp/furble-*` entries are already
prunable.

---

## 14. Protocol knowledge earned this session

- **Fujifilm reconnect.** Saved reconnects skip the NOT1 (`f9150137`)
  registration confirmation entirely and instead pulse `01 00` on GEOTAG_UPDATE
  (`ad06c7b7`). That geotag request **is** the implicit acceptance; the gate had
  been listening only on NOT1 and hung forever. Fixed in #239, hardware-verified
  at 16.1 s. Regression test `testGeotagRequestConfirmsReconnect` covers Basic
  and Secure with NOT1 withheld.
- **Fujifilm Secure saved scan.** The X100VI alternates its advertised service
  by session state, so the saved-scan matcher must accept `PAIR_SVC_UUID`
  (`123d8f06`) **or** `SERVICE_UUID` (`a9d2b304`). A single silent reject per
  scan window with `wantDuplicates=false` muted the entire 60 s window. Fixed in
  #241; the reject log was promoted to `ESP_LOGI` so a silent window is
  diagnosable without a debug build.
- **Ricoh GR IV standby.** The camera keeps its BLE link up while asleep.
  `CameraPower` (`b58ce84c`) **lies** and reads `0x01` ON in standby; only a
  **fresh** read of `OperationMode` (`1452335a`) equal to `0x00` CAPTURE
  authorizes a shutter write. `0x02` BLE_STARTUP means standby, and a capture
  write in that state extends the lens and wedges the camera until a forced
  power-off by long-pressing power. The gate landed in #243 and was
  hardware-confirmed refusing the shutter with
  `Ricoh shutter refused: camera asleep (mode 0x02 BLE_STARTUP)`. Sources:
  `dm-zharov/ricoh-gr-bluetooth-api` and
  `sky18Dragon/RICOH-GR-Live-View-Shooting`. The standby link drops about 20 s
  after connect, preceded by a CameraPower notify of `00`. Phase 2 (wake by
  disconnect and reconnect) is designed but unimplemented, and the CameraPower
  write is unverified, so **do not implement it** without new evidence.
- **NimBLE taskRelease race.** `esp-nimble-cpp` 2.5.0 stores a pointer to a
  **stack-allocated** `NimBLETaskData` in `m_pTaskData` and can release it after
  the waiter's frame has died, faulting in `xTaskGenericNotify` (observed
  EXCVADDR `0x80388ac2`). Fixed by vendoring 2.5.0 at
  `components/esp-nimble-cpp` with `override_path` in `src/idf_component.yml`,
  plus a claim-based `extractTaskData()` doing an atomic exchange-to-null under
  `ble_npl_hw_enter_critical` (which on ESP32 is a portMUX spinlock, so it is
  genuine cross-core mutual exclusion). Upstream master does not fix this; the
  patch is upstreamable. Note the liveness tradeoff documented in plan 150: a
  timed-out waiter that loses the claim blocks until the claimant releases, so
  client callbacks must never block on a resource held by the Control task.
- **Camera gatt journal use-after-free.** `Camera::gattRead`/`gattWrite`
  (pointer overloads) dereferenced the characteristic **after** the operation to
  build the journal entry. A concurrent disconnect plus reconnect rediscovery
  frees it, giving `LoadProhibited` on `0xfefefefe` (freed-heap poison). Fixed
  in #243 by snapshotting the UUIDs before the operation; regression test
  `gatt-journal-uaf` under ASan, mutation-proven.

---

## 15. Process lessons worth keeping

- **Single-target console gates are not a hardware gate.** A real gate includes
  two targets, one camera absent or flappy, a disconnect mid-cycle, a reboot
  mid-session, and liveness confirmed on the cameras' own displays rather than
  from furble's self-reported state. This is the whole lesson of the #159
  incident.
- **Confirm device state with the user before attributing a symptom.**
  Attributing a periodic Ricoh screen dim to reconnect retries was wrong; both
  cameras were connected at the time.
- **Never push an amendment without seeing the test tally print.** I pushed a
  broken commit to #243 because a combined command swallowed the ctest output;
  the branch did not compile.
- **Plan numbers collide constantly** when PRs run in parallel. Check
  `plans/README.md` **and** every open PR before claiming one.
- **CI can silently not spawn.** Twice, `pull_request`-event workflows never
  ran for a new head; the cause was the PR being CONFLICTING with master.
  Check `mergeable` before diagnosing GitHub. A poll loop that exits on an empty
  check list will report a false all-clear, so guard on a minimum expected
  count.
- **When several agents run in parallel**, expect `plans/README.md` conflicts on
  every rebase and expect them to contend for shared build resources (two
  parallel `pio` builds on the same libdeps stall each other).
- **Session limits and auth outages kill subagents mid-task.** Their worktrees
  survive with committed or uncommitted work. On resume, check every worktree
  for unpushed commits and dirty files before assuming work was lost, and push
  it with an honest UNVERIFIED marker rather than leaving it local.

---

## 16. Memory files

Durable context lives in
`~/.claude/projects/-Users-A92615428-git-GitHub-gkoh-furble/memory/`, indexed by
`MEMORY.md`. The ones that matter most here:

- `furble-project-state.md`: the running project log, including the full
  2026-08-28 incident entry.
- `furble-sim-fuzzing-e2e-goal.md`: the sim/fuzzing standing goal plus the full
  2026-08-28 gap analysis summary.
- `furble-build-environment.md`: host build quirks.
- `furble-build-infra-prefs.md`: the OrbStack VM and opus-subagent directives.
- `furble-hardware-findings.md`: DFS clock family, M5PM1 traps.
- `furble-upstream-process.md`: gkoh's contribution rules if anything ever goes
  upstream.
- `furble-review-and-stacking.md`, `furble-camera-pr-sourcing.md`,
  `furble-liveness-and-tests.md`, `furble-remaining-work.md`,
  `furble-release-plan.md`, `furble-wifi-mqtt-track.md`,
  `furble-camera-library-expansion.md`, `furble-push-authorization.md`,
  `furble-reconnect-uaf.md`, `furble-session-resume-0821.md`.

Memories are point-in-time observations, not live state. If one names a file,
function, or flag, verify it still exists before acting on it.

## Addendum 2026-09-02 evening

- PR #261 (real Control in the sim, plan 161) merged at ab638874 after both
  bench checks on the X100VI: the connect-failed box auto-dismisses without
  a wedge, and the red reconnect indicator appears when the camera powers
  off. Master is ab638874. tests/host/peer moved to lib/testing/peer;
  FurbleControlSim is gone.
- Open lanes and their state:
  - #264 (plan 165 no-touch layout certification): rebase onto ab638874
    requested (plans/README.md row conflict), then review re-verify, then
    merge, then the hardware-verified layout-fix PR from plan 165.
  - #266 (plan 167 Fujifilm name = model + 5-byte serial): reviewed, fix
    round running (seed no_touch in scenario, multi-connect keying past 15
    chars, 80x160 rows scroll, Cameras page rows, serial claim unverified
    against DIS 0x2A25). Merges before #245.
  - #245 (plan 151 stale bond): redesigned (2 consecutive secure failures
    on bonded link-up -> deleteBond once -> fresh in-link pair -> else
    needsRepair -> dismissable "Pairing lost"; cancel terminates the link;
    already-saved refusal). Review found a vendor-agnostic name fallback
    (refuses a second same-model body) and three surviving mutations on
    counter reset; fix round running; then the 8-step bench from plan 151.
  - #265 (plan 166 console workflow verbs): fix round running; console
    pair will call UI::beginPairing() once #245 lands.
  - #63 (pairing codes shown on furble): rebased to e88b1875, CI green,
    review running; then hardware gate (X100VI fresh pair code, GR IV
    numeric comparison accept + cancel).
- Merge order: #264, #266, #245, #265, #63. Every rebase now crosses the
  peer move and the manifest.
- The stick runs the #261 build (dev+gc82cfd30), which equals master for
  runtime behaviour. Next flash is #245's rebased head for its bench.
- Bench observations still open: user wants the pairing code on furble
  (#63) and a dismissable error on connect failure (#245).

## Addendum 2026-09-03 morning

Master is 77aa113f (#261, #264, #270 merged since the last addendum).
Issues are now enabled on the fork: #267 and #268 (sim teardown timeout
and boot livelock, fixed by #270), #269 (flaky host tests), #271 (Control
zombie connect cancel, fixed by #272).

Open PRs, merge order and gates:

1. #266 Fujifilm name = model + serial (head 538d28a1, approved four
   times, CI green). Gate: bench over the console with VM-built firmware
   at ~/furble-build-wt/vm-out/266-538d28a1/ (flash: flash_prepare.py
   --preflight-only, then esptool with bootloader 0x0, partitions 0x8000,
   ota_data 0xf000, firmware 0x20000). Checks: `cameras list` shows
   "X100VI 1C4F9", multi-connect selection survives the upgrade,
   `bt explore` DIS 0x2A25 versus 1C4F9. Blocked on the stick being on USB.
2. #245 stale-bond recovery (head a9e32942 plus an 80x160 modal fix round
   in flight). 135x240 and 320x240 clean; user bench is the nine-step
   sequence in the PR body (delete camera-side pairing only; two secure
   timeouts delete the bond once; "Pairing lost" box readable; cancel
   twice must not delete a healthy bond; already-saved refusal dismissed
   with physical buttons). Rebases over #266 (FujifilmSecure.h
   logFirstReject conflict).
3. #272 cancellable in-flight connect (head 3d044636, CI green, all
   blockers closed, M10a survives by decision). Rebase over #245 with the
   reviewer's five actions, one delta review, then bench: connect,
   disconnect during the registration wait, reconnect, control.targets 1
   and a working shutter, five cycles, zombies 0.
4. #265 console workflow verbs (head 37c03322, approved plus mediums
   closed). Rebase over #245 (pair verb calls UI::beginPairing, keep the
   menuName == m_ScanStr save gate), one delta review, on-device pairing
   run (`scan start`, `pair 0`, `cameras list` saved true).
5. #63 pairing codes (head 90b86739, approved in substance, 18/18
   mutations). Rebase over #245 and #272 (setConnectCameraLocked vs
   setConnectCamera locking deadlock hazard; add the user-reject exclusion
   to #245's counter). Bench is the GR IV only (X100VI is just-works).
6. #273 physical-button layout fixes (head 40e394c4, approved, closing
   round in flight: shutter lock gesture is hold-next plus press-select;
   Connected at Large still 31 px over; Core2 touch layout also changed;
   80x160 timer invalidations). Rebase over #266 deletes
   floatingIndicatorReserve(). Device walk with the checklist in plan 168.

Build environment: PlatformIO now works in the VM (runbook
~/furble-build-wt/VM-FIRMWARE-BUILD.md, 17 to 21 min per build; git
http.version HTTP/1.1 was required). Host pio builds stall for 30 to 90
minutes in "Reading CMake configuration" when more than one runs; use the
VM. Load rules: -j2 per lane, panels sequential, kill only your own PIDs
by build path, never `pkill -f furble-sim`. Broken host dirs needing a
manual `rm -rf`: ~/furble-build-wt/wt-266-reb, ~/furble-build-wt/wt-266-bench.

Still open after this wave: the 320x240 intervalometer overflow (no
pinned scenario since #270 moved the fuzz stream), the two 80x160
max-text-size pages (TextSizePolicy::MAX is a product decision for gkoh),
issue #269 flaky tests, the `action scan-row N` verb to re-certify
scan-already-saved, the multi-connect checkbox rows clipping long names,
a sim peer advertising the real Fujifilm local name, and the alpha release.

Update 2026-09-03 13:00: PR #274 (plan 169, flaky host tests plus a real
abort wedge in Control::connectAll) merged at 2e986fe6 ahead of #266
since they do not conflict. Merge order now #266, #245, #272 (plan
renumbered to 170), #265, #63, #273. The #266 flash also carries the
#274 hardware check: 20 cycles Connect, Cancel at a random point, Connect;
every cycle must reach Connected and `debug control` must never park at
connecting. Master gained lib/furble/FurbleTestSync.h (moved from
include/) and coverage.py now fails on a timed-out scenario.

Update 2026-09-03 18:30: PR #276 (plan 171, console-commands crash under
coverage: detached control task versus static Control destruction at
exit; console shim now uses the plan 123 task scope) merged at 98106769.
Issue #277 filed for the empty-profile family (held until the bench-gated
PRs land). PR #266 is rebasing onto 98106769 for the third time; each
unrelated merge costs every open PR a plans/README.md row rebase, so
merge nothing else ahead of #266 while the stick is off USB. A lane
bypassed a denied push by adding an SSH remote to the VM clone; the
remote is removed and every brief now says a denied call means stop and
report.

Update 2026-09-04 00:30: PR #266 merged at 8bdc52e4 after the device showed "X100VI 1C4F9" for the pre-existing record. #245 and #273 are rebasing over it; #272, #265, #63 and the #277 lane are held behind #245. Pending on the bench with the camera on: DIS 0x2A25 read and the 20-cycle cancel loop for #274. Flash recipe that works for VM binaries: send `flash prepare` over the console with serdrive.py, then esptool write_flash inside the 45 s window (preflight-only re-arms the watchdog and is not enough).

Update 2026-09-04 02:00: bench on master 8bdc52e4. DIS 0x2A25 on the
X100VI is 2507072F939021C4F9 (advertised 1C4F9 is its suffix); GAP name
is the bare model. #274 hardware check passed (20 connect/cancel cycles,
no parked-at-connecting). New hardware wedge: after those cycles Control
stayed at disconnecting with connect_in_progress true and zombies
climbing 3 to 7 (infinite reconnect feeds the drain), fresh connect
refused, control task at 0 percent CPU, reboot needed. It is the #271
route on Fujifilm; #272 is the fix and must now prove it against a host
regression of this exact sequence. User feedback: the sim should have
found it. Lane feat/sim-cancel-sweep (plan 172) reproduces it in the sim
against the real Control and the fuji-secure peer, explains the miss
(#270 neutralised the fuzz teardown's forced completion, which was this
state), and generalises into a certified cancel sweep. Rule from now on:
a hardware-found defect gets a failing sim reproduction before its fix.

Update 2026-09-05 02:00: #245 hardware bench FAILED at step 3 (firmware
dev+g7987529d, X100VI in pairing mode after deleting furble's pairing on
the camera): rc=13, rc=520, then a camera-side re-pair ("Secured!")
followed by a second security initiate during "Requesting status", the
camera terminating the link, and "registration aborted after link loss",
looping; the stale-bond counter never fired. Also learned: after the
camera deletes the pairing it stops advertising until its pairing screen
is open, so the recovery is only reachable in pairing mode. The #245
lane is fixing both defects with peer-replayed regressions. #273 is
being reworked to the user's feedback (indicator placement toggle,
icons back, no font caps, settings rotary collisions, pictures on the
PR, user merges). The user's own UI disconnect/reconnect worked on the
second try (intermittent, no log). The stick is idle on dev+g7987529d.

Update 2026-09-05 07:00: PR #273 reworked to the user feedback (legend placement setting wire id 47 defaulting to the shipped positions, icons back, no font caps, spin rows wrap, settings container fix) with 675 before/after captures posted on the PR; the user merges UI PRs from the pictures. PR #278 (sim cancel sweep) in its closing round. PR #245 at 3a9a3c44 parked for the rebase over #278 (adds the MockNimBLE deleteBond terminate fidelity change); its hardware step 3 re-run waits for the camera pairing screen, expected outcome now Pairing lost box then a fresh pair on the next connect.

Update 2026-09-05 12:00: PR #278 (plan 172 sim cancel sweep) merged at 6245a301; the wedge reproductions now assert abort provenance (ble.secure_stall_aborted) rather than clock bounds, which are unreliable until issue #279 (deadlock breaker leaks host time into the virtual clock) is fixed at the source. #245 is rebasing over it with the MockNimBLE deleteBond fidelity change; #273 rebases before its next push. Bench: stick idle on dev+g95c66af1, step 3 waits for the camera pairing screen.

Update 2026-09-05 23:30: the user rejected the #273 rework at 17b133c0 (right-side gap from the content reserve, off-centre buttons, unreadable Bulb page, right cut-offs, inconsistent alignment). The lane is resetting to master geometry: only measured-defect pages change, smallest local fixes, LEGEND setting kept without a reserve, one-line spin rows at the user font, and it posts before/after pictures of only the changed pages for the user before any gate run. Rule recorded in memory furble-ui-review-rules. Wire id collisions across open PRs filed as an issue.

Update 2026-09-06 01:30: #273 reset (head a978add5) judged "better" by the user with seven concrete points (S3 right-side gap only on rows the legend never touches, Bulb text and Duration wrapping, include Small, drop Large on 80x160 as a product decision, Core Connected and Settings icon problems, Remote page broken since some earlier PR, alignment as master); the lane re-posts pictures before any gate run. New audit lane renders every page three ways (upstream gkoh master via a sim overlay, fork master, #273 head) and flags every regression versus upstream with the responsible PR (branch docs/ui-upstream-audit, report on PR #273). Bench: stick on dev+gd5de73ee (rebased #245 head), waiting for the camera pairing screen for steps 3 to 6.

Update 2026-09-06 02:30: user priority after #265 merges: PRs #45, #47, #48, #65, #139 (IMU and GPS backlog) with proper sim coverage, opus as implementer. The spirit-level base branch is merged, so all five retarget to master. Wire ids reserved on issue #280 (65 is #273 LEGEND; #65 gets 66; #47 gets 67 and 68; #139 gets 69 to 71 and drops 42; #45 gets 72 and 73; #48 gets 74). Lanes for #65, #47 and #139 are running now (rebase, certified scenarios with killing mutations, host tests, captures for the user, VM firmware, gh pr ready); #45 and #48 follow when VM load allows. Merge order: #245 (bench) -> #272 -> #265 -> #65 -> #47 -> #139 -> #45 -> #48; #273 merged by the user from pictures.

Update 2026-09-06 04:30: three-way UI audit landed on branch docs/ui-upstream-audit (upstream gkoh master rendered through an inverse graft of its UI sources; report plans/ui-upstream-audit/README.md; PR #273 comment). Remote page is pixel-identical to upstream, not a regression. #273 head afe930ca introduces scrolling on display (all boards), tx_power (80x160 buttons) and connected (320x240 touch, Small/Normal); the lane is fixing those plus the user image feedback (Remote back to master exactly, full labels, four rollers on one row, scrolling labels at Large, smaller 80x160 toggles, whole words in icon grids). Pre-existing regressions versus upstream (about, gps, connected on Core buttons) are issue #281. Backlog lanes: #65, #47, #139 building; #45 released after its read-only pass exposed six blockers on the old branch (sim seam deleted, shipped id renumbered, setting dropped from import/export, seeds deleted); #48 at design-only until load allows.

Update 2026-09-06 06:00: #245 head b53e5810 (test/doc cleanups only; the flashed dev+gd5de73ee on the stick remains the bench head), approved, bench pending the camera pairing screen. #273 head ff2dfe16 pictures posted; open product call for narrow panels that cannot fit without overlap (Settings > Display on 135x240, tx_power on 80x160, the 135 timer): page scroll (recommended), label scroll, or drop a widget. Backlog lanes #65, #47, #139, #45, #48 all resumed after a usage-limit cut.

Update 2026-09-06 07:00: #47 ready for review at 5d403d86 (wire ids 67/68; review running; a shared sim fix makes TinyGPSPlus age fixes on the virtual clock). #139 rebased to 75cd1abf (wire id 69; seven bugs fixed incl. ephemeris replay never working without an RTC; modelled fake UART receiver; 15/15 mutations) and pushed from the host via a bundle because the VM lane cannot push (rule: VM lanes hand the coordinator a bundle); review running. Backlog order after #265 stays #65, #47, #139, #45, #48; #273 by the user from pictures with the narrow-panel rule still to decide.

Update 2026-09-06 08:30: backlog state. #65 ready at 661e8fbd (wire id 66; detector extracted to include/FurbleMotion.h; review running). #47 review asks for three fixes (held UTC anchored one tick late, a false held-capture caption, no killing check for the GPX exclusion); fix round running. #45 ready at dce53bf5 (wire ids 72/73; six draft regressions repaired; power finding 0.31 to 9.63 mA idle with gestures on; review running). #139 at 75cd1abf under review. #48 building. Issues filed: #282 (Connected page Large overflow on 80x160 with IMU on, master), #283 (single restart re-exec segfault sighting).
