# 346 - plans directory cleanup

Status: docs-only. This change archives eleven implemented plans, corrects four
status lines against the code, adds the archive status line to the others it
moves, repoints every tracked reference and records what it deliberately left
alone. No decision or evidence inside a moved plan is rewritten. Written against
master `4af87727` (#461), rebased onto `9a617355` (#502) and landed on
`85b89931` (#494). Every plan master added since is covered: the ones whose work
merged are archived below, and the rest carry a row under Left alone, including
the ones that arrived after `9a617355` and were not swept.

## Problem

`plans/` held 68 numbered documents at top level at `4af87727`. #471, #473, #476,
#477, #478, #479, #482, #483, #485 and #487 added fifteen more, so `9a617355`
carries 91. Master kept moving: `85b89931` (#494), the commit this lands on,
carries 117. Several describe work
that is already merged, so a contributor reading the directory cannot tell an
open plan from a finished one without opening each document.

`plans/CLAUDE.md` already states the rule: move a plan to `implemented/` once its
implementation is complete on master and no implementation work remains, and let
hardware measurement, soak and device-look gates stay in the archived text.
`plans/implemented/README.md` keeps research, design-only, branch-only, deferred
and partially implemented plans in the parent directory.

The convention this change follows is the one the directory already uses: the
file keeps its number and name, `git mv` keeps the history, and the plan text is
not rewritten. PR #441 set the related practice that a review-feedback record
moves with the plan it belongs to.

Every move below was checked against a merge commit in `git log` or against the
code the plan claims, on master. Branch-only and unmerged work was not moved.

## Moves

| Plan | Evidence |
| --- | --- |
| [321-ublox-m10-driver.md](implemented/321-ublox-m10-driver.md) | Merged as #458 (`cd04c38a`, codec) and #459 (`511c11d3`, backend and features). On master: `lib/furble/protocol/GpsUbx.h` and `.cpp`, `src/FurbleGPSUbx.cpp`, `GPS::backend_t` and `Ubx::Detect` in `include/FurbleGPS.h`, `tests/host/gps_ubx_test.cpp`, and 20 `sim/scenarios/e2e/gps-ubx-*` scenarios. |
| [321-review-feedback.md](implemented/321-review-feedback.md) | Review record for the same branches, which merged as #458 and #459. Moves with its plan. |
| [322-hardware-abstraction.md](implemented/322-hardware-abstraction.md) | Merged as #462 (`fd51e96c`). On master: `lib/hal/` with the pure drivers, `include/FurbleBoard.h` with the board profile and power contract, and the `hal-drivers` and `board-profile` host tests in `tests/host/`. |
| [322-review-feedback.md](implemented/322-review-feedback.md) | Review record for the same branch, which merged as #462. The last section is the PR #462 review and every finding is fixed. Moves with its plan. |
| [323-plans-refresh-review-feedback.md](implemented/323-plans-refresh-review-feedback.md) | The record for PR #441, merged as `a26e73f8`. It reviews the plans refresh rather than a separate plan document, so it archives on its own. |
| [324-hw-research-review-feedback.md](implemented/324-hw-research-review-feedback.md) | The record for `docs/hw-research-and-fixes`, merged as #443 (`663d106f`). That branch adds plans 318 to 320, which stay at top level as research; the record itself is closed. |
| [325-peer-subscription-race.md](implemented/325-peer-subscription-race.md) | Merged as #453 (`139d884a`). The commit message carries `Fixes #451`, so the issue closes with the merge. `lib/testing/peer/SubscriptionTable.h` is on master. |
| [325-review-feedback.md](implemented/325-review-feedback.md) | Review record for #453. Moves with its plan. |
| [326-docs-accuracy.md](implemented/326-docs-accuracy.md) | Merged as #471 (`92618bff`). The corrections are in the tree: `docs/wiki/UI-Walkthrough.md` embeds the three captured pages, and the fuzz and readiness figures name `sim/coverage/fuzz_results.json` and the manifests. Added by #471 after this plan was written, so it is moved here too. |
| [326-review-feedback.md](implemented/326-review-feedback.md) | All nineteen findings in the two review rounds are recorded fixed in the same merge. Moves with its plan. |
| [331-merge-gate-base.md](implemented/331-merge-gate-base.md) | Merged as #478 (`b57f0118`), whose title names #464. `tools/check_review_threads.py:201` reads `baseRefOid` and fails closed when it is absent, and the run with no pull request keeps the checkout read. Added by #478 after this plan was written, so it is moved here too. |

Plan 167 was considered and left at top level. It is implemented as written, but
it carries a `## Follow-up, not in this change` section with four open UI gaps,
and the multi-connect list still does not wrap: `addMenuItem()` returns on the
checkbox branch at `src/FurbleUI.cpp:2816`, before the `wrapText` branch that
opens at `:2843` and sets the long mode at `:2858`. Archiving it would claim work
is complete that is not.

Plans 323 and 324 archive without a parent plan staying open. Their parents are
PR #441 and branch `docs/hw-research-and-fixes` (#443), not plan documents. The
plans that branch added, 318 to 320, stay at top level as research.

## Status corrections

Only status lines changed. Bodies are untouched.

| Plan | Was | Now | Evidence |
| --- | --- | --- | --- |
| [322-hardware-abstraction.md](implemented/322-hardware-abstraction.md) | "hal-1 and hal-2 implemented, not merged" | "hal-1 and hal-2 merged in #462 (`fd51e96c`), on the capability macros and coupling ratchet from #460 (`f4cf0d32`) ... Firmware includes none of the drivers yet", the #471 text kept verbatim, plus "Archived to `plans/implemented/` on 2026-11-25." | #471 had already corrected this line, and its second review round removed the false claim that both hal branches were gone. The rebase keeps that wording and adds only the archive note, so the correction is not duplicated or regressed. |
| [325-peer-subscription-race.md](implemented/325-peer-subscription-race.md) | "Done on `fix/451-fuji-double-subscriptions`" | "Merged as #453 (`139d884a`). The merge commit carries `Fixes #451`, so the issue closes with it." | `git log -1 139d884a` message body. |
| [304-review-feedback.md](304-review-feedback.md) | "every finding below is fixed on `fix/381-390-review`" | the same, plus "merged as #456 (`592ec019`)" | `592ec019` is "fix: recover the USB monitor (#381) and gate WebUI memory on camera connect (#390)". The hardware gate is unrun, so the document stays at top level. |
| [33-wifi-hub.md](33-wifi-hub.md) | "Open: the headless LVGL exclusion and moving the intervalometer off the UI task." | "Open: moving the intervalometer off the UI task", with the landed headless gate cited | `src/CMakeLists.txt:49` gates the UI sources that need a display, `src/CMakeLists.txt:78-84` registers the component without `REQUIRES icons` under `FURBLE_NO_DISPLAY`, and `platformio.ini:74` passes `-DFURBLE_NO_DISPLAY=1 -DEXCLUDE_COMPONENTS=icons`. Line 49 alone gates sources only, so the first draft misattributed the component drop to it. `FurbleUIIntervalometer.cpp` is still inside that UI source list, so the second half of the item is still open. The `platformio.ini` line read `:69` on `9a617355` and moved four lines down when #500 and #505 added env rows above it. |

Eight of the moved documents had no status line at the top at all, so the move
adds one, `Status: implemented on master (archived 2026-11-25).`, to
`321-review-feedback.md`, `321-ublox-m10-driver.md`, `322-review-feedback.md`,
`323-plans-refresh-review-feedback.md`, `324-hw-research-review-feedback.md`,
`325-peer-subscription-race.md`, `325-review-feedback.md` and
`326-review-feedback.md`. The four rows above are corrections to text that was
already there; these eight are new lines, which is why they are listed
separately rather than in the table.

Three index changes in `plans/README.md` came with them:

- The `#451` ownership row is retired into the closed-issue sentence, because
  #453 merged with `Fixes #451` and plan 325 is archived.
- The `#464` ownership row is retired the same way, because #478 merged with
  #464 in its title and plan 331 is archived.
- The sim-only paragraph is dated 2026-11-13 and describes master at
  `e66e8807` (#439). A note records that master has moved on to `b57f0118`
  (#478) and that the gate results were not re-run. The dated paragraph and
  `313-sim-only-status.md` keep their text and their numbers.

## References repointed

38 tracked files named a moved path on the tree this lands on. Each now points
at `plans/implemented/`:

| File | Paths it named |
| --- | --- |
| `CLAUDE.md` | `plans/implemented/331-merge-gate-base.md` |
| `CONTRIBUTING.md` | `plans/implemented/331-merge-gate-base.md` |
| `docs/components/README.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/axp2101.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/bq25896.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/bq27220.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/co5300.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/cst816.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/cst9217.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/qmi8658.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/sh1106.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/st7789.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/sx1262.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/tca8418.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/components/xl9555.md` | `plans/implemented/322-hardware-abstraction.md` |
| `docs/protocols/gnss-ubx.md` | `plans/implemented/321-ublox-m10-driver.md` |
| `docs/supported-hardware.md` | `plans/implemented/321-ublox-m10-driver.md` |
| `include/CLAUDE.md` | `plans/implemented/322-hardware-abstraction.md` |
| `lib/furble/protocol/GpsUbx.h` | `plans/implemented/321-ublox-m10-driver.md` |
| `lib/hal/CLAUDE.md` | `plans/implemented/322-hardware-abstraction.md` |
| `plans/313-sim-only-status.md` | `plans/implemented/326-docs-accuracy.md` |
| `plans/314-review-feedback.md` | `plans/implemented/331-merge-gate-base.md` |
| `plans/318-next-board-battery-gps.md` | `plans/implemented/322-hardware-abstraction.md` |
| `plans/332-imu-motion-scale-audit.md` | `plans/implemented/322-hardware-abstraction.md` |
| `plans/378-vendor-document-verification.md` | `plans/implemented/322-hardware-abstraction.md` |
| `plans/333-datasheet-corpus.md` | `plans/implemented/321-ublox-m10-driver.md`, `plans/implemented/322-hardware-abstraction.md` |
| `plans/381-display-touch-datasheet-promotions.md` | `plans/implemented/322-hardware-abstraction.md` |
| `plans/350-stickc-family-fixes.md` | `plans/implemented/322-hardware-abstraction.md`, `plans/implemented/326-docs-accuracy.md` |
| `plans/352-m5gfx-board-pin.md` | `plans/implemented/322-hardware-abstraction.md` |
| `plans/353-hal-verification.md` | `plans/implemented/322-hardware-abstraction.md`, `plans/implemented/322-review-feedback.md` |
| `plans/353-review-feedback.md` | `plans/implemented/322-hardware-abstraction.md` |
| `plans/382-gnss-datasheet-verification.md` | `plans/implemented/321-ublox-m10-driver.md` |
| `plans/473-review-feedback.md` | `plans/implemented/322-hardware-abstraction.md` |
| `plans/95-engineering-lessons.md` | `plans/implemented/322-hardware-abstraction.md` |
| `plans/README.md` | ten rows repointed, and the #451 and #464 ownership rows retired rather than repointed |
| `plans/implemented/README.md` | eleven archive rows added |
| `sim/ubx_m10.h` | `plans/implemented/321-ublox-m10-driver.md` |
| `src/FurbleGPSUbx.cpp` | `plans/implemented/321-ublox-m10-driver.md` |

The first draft was written against `9a617355` and counted 29. Three arrived
late on that base: `plans/95-engineering-lessons.md` and
`plans/473-review-feedback.md` with #473 and #486, and the thirteen
`docs/components/` pages with #475. Ten more arrived with #500, #504, #505 and
#507 after it: `docs/components/bq27220.md`, `docs/components/sx1262.md`,
`docs/protocols/gnss-ubx.md`, `plans/313-sim-only-status.md`,
`plans/332-imu-motion-scale-audit.md`, `plans/378-vendor-document-verification.md`,
`plans/381-display-touch-datasheet-promotions.md`, `plans/350-stickc-family-fixes.md`,
`plans/352-m5gfx-board-pin.md` and `plans/382-gnss-datasheet-verification.md`.
`docs/components/ublox-max-m10s.md` and `docs/components/ublox-mia-m10q.md` named
plan 321 on `9a617355` and stopped doing so when #504 rewrote those pages, so
they are no longer in the list.

Two kinds of reference needed more than a path swap. A relative link inside a
moved plan resolves against `plans/implemented/`, so
`plans/implemented/322-hardware-abstraction.md` now writes
`[352](../352-m5gfx-board-pin.md)`: the link to plan 352 arrived with #499 after
the move was written. And `plans/implemented/326-review-feedback.md` cited plan
322 phase 4 as `:203-210`; the phase 4 census sits at `:254-259` on this tree,
so the row cites that.

## Left alone

Eleven of the 117 documents `85b89931` carries move, so 106 stay at top level,
and this change adds two, its own plan and this review record, for 108.
Each one still owes implementation work, still owns an issue, is a research or
design record, is a reference document rather than a plan, or landed after this
plan was written. The last group is the twenty-six that arrived between
`9a617355` and `85b89931`: they are listed so the rows equal the files, with the
reason that this change did not review them, rather than with a claim about
their state. Neither this plan nor its review record is listed against itself.

| Document | Reason |
| --- | --- |
| [00-hardware-experiments.md](00-hardware-experiments.md) | Measured hardware record cited by plans 07 and 15 and by the root CLAUDE.md. Not an implementation plan. |
| [106-flash-size-optimization.md](106-flash-size-optimization.md) | Items 3, 7-13 and 14 are still open. Spot-checked item 3: `# CONFIG_NEWLIB_NANO_FORMAT is not set` in `sdkconfig.m5stick-s3:3287`, `sdkconfig.m5stack-core:3011` and `sdkconfig.waveshare-s3-eth:3283`. |
| [106-review-feedback.md](106-review-feedback.md) | Its reviewed branch merged as #461, but it stays with plan 106, which is still open. |
| [108-upstream-issue-mapping.md](108-upstream-issue-mapping.md) | Dated snapshot from 2026-08-22 whose own rows include OPEN upstream issues. |
| [111-power-recommendations.md](111-power-recommendations.md) | Three maintainer policy questions at the top are unanswered, and N5 is skipped rather than done. |
| [111-review-feedback.md](111-review-feedback.md) | Its reviewed branch merged as #454 (`4ca92c52`), but it stays with plan 111, which is still open. |
| [114-flasher-provisioning.md](implemented/114-flasher-provisioning.md) | Design only. Only the settings parser landed, as `plans/implemented/114-provision-parser.md` in #165; the browser transport and the WiFi application step were still designs at this cleanup; the browser step landed afterwards. |
| [115-ota-update-mqtt.md](115-ota-update-mqtt.md) | Design only. Only the OTA engine landed, as `plans/implemented/115-ota-engine.md` in #168; the HTTPS download and the Home Assistant end to end flow are unrun. |
| [125-esp-optimization-program.md](125-esp-optimization-program.md) | Verified open: `evaluateBootHealth()` has no production caller, only `tests/host/ota_state_machine_test.cpp`, and there is no `esp_ota_mark_app_*` call in `src/`, `include/` or `lib/`. |
| [126-esp-optimization-audit.md](126-esp-optimization-audit.md) | Historical research snapshot. Research stays in the parent directory. |
| [127-nordic-port-baseline.md](127-nordic-port-baseline.md) | Port stack steps 1 to 4 are merged, but the "Follow-up slices" list is not implemented: Clock/Random, BLE interfaces, SettingsStore, companion framing and auth, UART/GPS and storage interfaces, a headless Nordic skeleton, and a Nordic DK in CI. |
| [127-review-feedback.md](127-review-feedback.md) | Its reviewed branch merged as #460 (`f4cf0d32`), but it stays with plan 127, which still owns the follow-up slices. |
| [149-ricoh-sleep-shutter-gate.md](149-ricoh-sleep-shutter-gate.md) | Phase 1 is on master, but Phase 2 (wake) is design only and the bench gate is unrun. |
| [158-sim-scheduler-parity.md](158-sim-scheduler-parity.md) | "Step 5 (a differential trace against hardware) is not started." |
| [159-camera-peer-certification.md](159-camera-peer-certification.md) | "The implementation and acceptance steps below remain pending where their gates are unrun." |
| [161-sim-real-control.md](161-sim-real-control.md) | Open: gap 7, a mid-session restart scenario, and gap 1, a `ble-kill` liveness probe. |
| [165-sim-no-touch-layout.md](165-sim-no-touch-layout.md) | "Deriving the simulator default from the compiled board is still open." |
| [167-fujifilm-device-name.md](167-fujifilm-device-name.md) | Implemented as written, but it carries a `## Follow-up, not in this change` section with four open UI gaps. The multi-connect list still does not wrap: `addMenuItem()` returns on the checkbox branch at `src/FurbleUI.cpp:2816`, before the `wrapText` branch at `:2843`, whose long mode is set at `:2858`. Deciding what "complete" means here is not this cleanup's call, so the plan stays where its follow-ups are visible. |
| [168-notouch-layout-overflows.md](168-notouch-layout-overflows.md) | Carries a `## Still open` section and unclosed 320x240 and Display page items. |
| [17-imu-gestures.md](17-imu-gestures.md) | Merged as #45, but it names the StickS3 deep wake path as "for a later PR. Not implemented here", and no other plan owns that work. |
| [173-review-feedback.md](173-review-feedback.md) | Stays with plan 173. |
| [173-sim-scheduler-visibility.md](173-sim-scheduler-visibility.md) | The README ownership table still assigns it issues #283 and #289, and the document records CI and hardware validation as pending. |
| [18-gps-motion.md](18-gps-motion.md) | "Phase 2 receiver policy is still open: nothing consumes `isStationary()`." Confirmed by `src/FurbleGPS.cpp:2416`. |
| [18-review-feedback.md](18-review-feedback.md) | Stays with plan 18. |
| [25-multiconnect-ui.md](25-multiconnect-ui.md) | The plan 10 reconciliation is unfinished: `Control::updateConnStats()` exists at `src/FurbleControl.cpp:210` while the Cameras rows still carry "No live RSSI here" in `src/FurbleUI.cpp`, so the rows never read the cached snapshot. |
| [28-emulator.md](28-emulator.md) | Carries a `## Remaining work` section; the screenshot command is still open. |
| [287-mcu-portability-and-bundle-hardware.md](287-mcu-portability-and-bundle-hardware.md) | Research and hardware survey. No implementation is claimed. |
| [29-virtual-test-rig.md](29-virtual-test-rig.md) | "Phase 4, phase 5, and phase 6 are intentionally not implemented here." |
| [297-gps-improvement-roadmap.md](297-gps-improvement-roadmap.md) | Carries a `## Verified findings still open` section and ranked remaining GPS work. |
| [30-m5stack-framework.md](30-m5stack-framework.md) | "The G1, G2, G4 and G5 upstream PRs are not filed; G6 is now due." |
| [302-companion-restart-reconnect.md](302-companion-restart-reconnect.md) | Owns open issue #381. The UART0 panic capture and the hardware retest are unrun. |
| [304-review-feedback.md](304-review-feedback.md) | Stays with plans 302 and 304. Its status line now names the merge. |
| [304-webui-stick-memory.md](304-webui-stick-memory.md) | Owns open issue #390. The hardware memory gate, a TLS handshake, a settings write and a soak remain. |
| [313-sim-only-status.md](313-sim-only-status.md) | The live closeout record, linked from the README and `plans/CLAUDE.md`. Not an implementation plan. |
| [314-memory-resilience.md](314-memory-resilience.md) | Two software items remain: PR 2c font and DPI parity, and the row 18 cycle memoization limit. |
| [314-review-feedback.md](314-review-feedback.md) | Stays with plan 314 and records row 18 as open. |
| [317-notouch-regressions.md](317-notouch-regressions.md) | Open: `bughunt/timer-overflow-no-timed-wake.txt` still fails on 80x160 and 320x240. |
| [318-next-board-battery-gps.md](318-next-board-battery-gps.md) | Research and decision. Adds no board, driver or support claim. |
| [319-meshtastic-dual-boot.md](319-meshtastic-dual-boot.md) | Research and design only. Nothing is implemented, built or flashed. |
| [320-rtc-use.md](320-rtc-use.md) | Ranked ideas. Nothing is implemented. |
| [327-control-gps-race.md](327-control-gps-race.md) | Added by #487 (`bbe4d22c`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [328-reference-docs-audit.md](328-reference-docs-audit.md) | Added by #477 (`dc5be70f`) after this plan was written. It names no open gate and its implementation is complete on master; it is left here so this cleanup stays the set of moves it reviewed. |
| [33-wifi-hub.md](33-wifi-hub.md) | The intervalometer is still on the UI task. Status line corrected above. |
| [332-imu-motion-scale-audit.md](332-imu-motion-scale-audit.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [332-imu-motion-scale-review-feedback.md](332-imu-motion-scale-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [333-datasheet-corpus.md](333-datasheet-corpus.md) | Added by #475 (`e26fad1a`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [334-review-feedback.md](334-review-feedback.md) | Review record for PR 482, added with it. Stays with the plan it reviews, which stays at top level. |
| [334-sim-hardware-alignment.md](334-sim-hardware-alignment.md) | Added by #482 (`30cc9c79`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [335-review-feedback.md](335-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [335-stack-cuts.md](335-stack-cuts.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [337-sim-mem-shims.md](337-sim-mem-shims.md) | Added by #506 after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [338-e2e-page-coverage.md](338-e2e-page-coverage.md) | Added by #476 (`b711a44a`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [339-mqtt-retry-backoff.md](339-mqtt-retry-backoff.md) | Added by #498 (`b3d08489`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [34-ota-partitions.md](34-ota-partitions.md) | Open: 34a-2 HTTPS OTA with boot health and mark-valid, and 34b BLE OTA. Confirmed by the missing `esp_ota_mark_app_valid` and `esp_ota_mark_app_invalid` calls. |
| [341-sd-shutdown-ack.md](341-sd-shutdown-ack.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [342-review-feedback.md](342-review-feedback.md) | Review record for the lost-lease fix, added with it. Stays with the plan it reviews, which stays at top level. |
| [342-wifi-lost-ip.md](342-wifi-lost-ip.md) | Added by #485 after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [343-apple-ui-snapshot.md](343-apple-ui-snapshot.md) | Added by #480 (`c7af92ea`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [343-review-feedback.md](343-review-feedback.md) | Review record for PR 480, added with it. Stays with the plan it reviews, which stays at top level. |
| [344-android-ui-snapshot.md](344-android-ui-snapshot.md) | Added by #483 (`a5cb2e14`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [344-review-feedback.md](344-review-feedback.md) | Review record for PR 483, added with it. Stays with the plan it reviews, which stays at top level. |
| [345-doc-citation-gate.md](345-doc-citation-gate.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [347-core-debug-flash-headroom.md](347-core-debug-flash-headroom.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [348-review-feedback.md](348-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [348-sticks3-sim-align.md](348-sticks3-sim-align.md) | Added by #502 (`9a617355`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [350-review-feedback.md](350-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [350-stickc-family-fixes.md](350-stickc-family-fixes.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [352-m5gfx-board-pin.md](352-m5gfx-board-pin.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [352-review-feedback.md](352-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [353-hal-verification.md](353-hal-verification.md) | Added by #479 (`bebf7caf`) after this plan was written; its own state records the paced gauge bus, the bounded radio BUSY wait and the PMIC key mask. Left here so this cleanup stays the set of moves it reviewed. |
| [353-review-feedback.md](353-review-feedback.md) | Review record for PR 479, added with it. Stays with the plan it reviews, which stays at top level. |
| [355-review-feedback.md](355-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [355-sim-cmake-entry-point.md](355-sim-cmake-entry-point.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [356-sim-memory-certify.md](356-sim-memory-certify.md) | Added by #492 (`9b404837`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [357-review-feedback.md](357-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [359-review-feedback.md](359-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [359-sony-legacy-generation.md](359-sony-legacy-generation.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [377-gnss-vendor-audit.md](377-gnss-vendor-audit.md) | Added by #501 (`f1fa36a0`) after this plan was written. It is left here so this cleanup stays the set of moves it reviewed. |
| [378-vendor-document-verification.md](378-vendor-document-verification.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [379-docs-gap-closure.md](379-docs-gap-closure.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [380-review-feedback.md](380-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [380-sim-rtc.md](380-sim-rtc.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [381-display-touch-datasheet-promotions.md](381-display-touch-datasheet-promotions.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [382-gnss-datasheet-verification.md](382-gnss-datasheet-verification.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [40-thinknode-port.md](40-thinknode-port.md) | Feasibility study. No code. |
| [41-alternative-hardware.md](41-alternative-hardware.md) | Hardware survey. No code. |
| [473-review-feedback.md](473-review-feedback.md) | Review record for the #473 hardware-trap corrections, added by #473 after this plan was written. Stays with the plans it reviews, which stay at top level. |
| [474-companion-coverage.md](474-companion-coverage.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [475-review-feedback.md](475-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [50-companion-app-design.md](50-companion-app-design.md) | "End to end BLE against the firmware service is still pending." |
| [51-app-feature-parity.md](51-app-feature-parity.md) | "The rig scenarios of phase 5 remain outstanding." |
| [511-review-feedback.md](511-review-feedback.md) | Landed on master after this plan's rebase base (`9a617355`); not reviewed by this change. |
| [61-camera-compatibility.md](61-camera-compatibility.md) | Survey with unimplemented rows: upstream PR 282 is still open and the G9II-generation XOR login is not implemented. |
| [63-sim-power-analysis.md](63-sim-power-analysis.md) | The README row records that the sticky PR report is not built, and the document keeps an "Out of scope" section. |
| [64-debug-tooling.md](64-debug-tooling.md) | Pillars 1 to 3 merged, but the document records pillar 2 follow-up work as remaining. |
| [65-bt-coexistence.md](65-bt-coexistence.md) | Research with no implementation. |
| [67-fujifilm-dial-detection.md](67-fujifilm-dial-detection.md) | Capture-first design. Nothing is implemented. |
| [74-dji-osmo.md](74-dji-osmo.md) | v1 merged as #81; GPS push and the remaining R SDK commands stay deferred. `DJIOsmo::updateGeoData()` is still an empty body, which the status line states correctly. |
| [86-sim-bug-hunt.md](86-sim-bug-hunt.md) | "The reported #32 symptom stays open as a hardware investigation." |
| [88-alternate-hardware.md](88-alternate-hardware.md) | Hardware survey. No code. |
| [90-scheduled-shooting.md](90-scheduled-shooting.md) | Deferred sketch, not scheduled for any phase. |
| [91-mic-trigger.md](91-mic-trigger.md) | Deferred sketch, not scheduled for any phase. |
| [95-engineering-lessons.md](95-engineering-lessons.md) | Reference document, not a plan. Its claim holds: `esp32-s3-headless` is built by `.github/workflows/reproducible.yml:71` and is absent from the PR matrix in `main.yml`. |
| [96-wifi-bt-ble-hardening.md](96-wifi-bt-ble-hardening.md) | Analysis complete, remediation open, and the document says per-item state is not re-audited. |
| [97-followups.md](97-followups.md) | The consolidated open backlog. |
| [98-power-optimization-audit.md](98-power-optimization-audit.md) | Audit with explicit "Not implemented" entries and no code of its own. |

## Verification

- `python3 tools/pre_review.py --base origin/master`: all thirteen gates pass,
  including `diff hygiene`, which runs `tools/check_pr_hygiene.py` and with it
  the `plan-links` check over `plans/README.md`.
- `python3 tools/check_doc_citations.py`: 0 errors of 2,899 citations in 162
  files, 92 of them symbol-checked. The gate checks a path and a line range, so
  it does not see `plans/33-wifi-hub.md`'s `platformio.ini:69`, which is why that
  one is recorded as a correction above rather than left to the gate.
- `sh sim/scripts/check-doc-tokens.sh`: simulator tokens are present.
- `git grep -n '<old name>.md'` finds no reference to a moved path outside
  `plans/implemented/`. The one remaining `plans/167-` reference is correct
  because that plan did not move.
- Every Markdown link in the tree resolves, except the pre-existing
  `docs/wiki/` page-name links, the vendored `components/esp-nimble-cpp/`
  README and three `plans/` links that are broken on master too.
