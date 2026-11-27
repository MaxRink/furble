# 391 - Plan closeout decisions

Status: docs-only. This record closes the plan documents that can never be
"implemented", archives the plans whose work is complete on master, and gives
every remaining plan one explicit state with the evidence for it. No firmware,
test, simulator, workflow or setting changes. Written against master `c8159ee9`
(#544) on 2026-11-27.

## 1. Why this record exists

`plans/` carried 134 documents outside `implemented/` at the base of this pass,
and master added twenty more while the branch was open, including 370, 372, 376,
384, 385, 386, 389 and 392. The inventory below covers 154 documents: the 153
that are still open, plus 114b, which master archived in #537 while this branch
was open. Most describe work that
shipped; a subset are research, surveys, design notes and deferred sketches that
cannot be implemented as code at all. All of them still read as open, because
the index has no state column and several status lines say "not merged" about
branches that merged months ago. A contributor cannot tell an open plan from a
finished one, and a research question that was answered reads the same as a task
nobody started.

The owner asked for every open plan to be analysed and either implemented or
explicitly decided. This is the decision record for that pass. Each document
gets one of these outcomes:

| Outcome | Meaning |
| --- | --- |
| Archived | The work is complete on master. The file moves to `implemented/` with its review record, and hardware, soak and device-look gates stay in the text, as `plans/CLAUDE.md` allows. |
| Closed, research answered | The document asked a question and the question has an answer. It stays in `plans/` as the record of that answer, is marked closed, and names the condition that would reopen it. |
| Closed, superseded | A later plan or commit owns the subject. The later document is named here. |
| Closed, not worth doing | The work was considered and rejected, with the reason. |
| Deferred | Real work, deliberately not scheduled, with a named condition that reopens it. |
| Open | Real work with a named item and an owner. |
| Reference | A living document (`00`, `95`, `313`) that is cited, not implemented. |
| Record | A review-feedback or retrospective file. It has no independent state; it closes with the plan or pull request it reviews. |

What "on master" means here: the merge commit named in the row is an ancestor of
`origin/master`, checked with `git merge-base --is-ancestor`, and at least one
artifact the plan claims is read in the tree. A plan whose own status line says
"not merged" is not taken at its word. Twelve status lines still describe their
work as branch-only or unmerged while the merge is on master, and the
corrections are listed in section 6.

No plan was renumbered by this pass. The 2026-11-26 cleanup, plan 384, moved
seven plans to 377 to 383 and is itself master's newest plan, so this record takes
391, above that run, and does not collide with it.

## 2. How the inventory was built

Every file under `plans/` outside `implemented/` was opened: the state section in
full, the decision or verdict section, and the passages that name code or a
merge. For each one the pass recorded: the state section the document itself carries, the merge
commit or branch it names, whether that commit is an ancestor of master, and
whether the files and symbols it claims are in the tree. The three checks
disagree often enough to be worth doing separately. Plan 337 says "Not merged"
while its own merge commit, `b549d36c` (#506), is on master. Plan 320 says
"Nothing here is implemented" while its first ranked idea is in
`src/FurbleTimeKeeper.cpp:52`.

The inventory below is the complete list, including 376 and 541, which master
added on `789dd78c` (#541) after the analysis base. A row that says `open` names the item
that keeps it open; the detail is in section 5.

## 3. The inventory

| Doc | Title | Implementation on master | State after this record |
| --- | --- | --- | --- |
| [00-hardware-experiments](00-hardware-experiments.md) | Hardware experiments | n/a | reference, cited measurements |
| [17-imu-gestures](17-imu-gestures.md) | PR17 - IMU gestures for wake and shutter | yes, #45 `107a6b96` | open: StickS3 deep wake, 25 Hz rate pending hardware step 8 |
| [18-gps-motion](18-gps-motion.md) | PR18 - Motion adaptive GPS | yes, #65 | open: phase 2 receiver policy |
| [18-review-feedback](18-review-feedback.md) | Plan 18 review feedback: GPS reader and standby settle fixes (#465) | yes, #465 | record, follows 18 |
| [25-multiconnect-ui](25-multiconnect-ui.md) | PR25: multi-connect selection and a Cameras status page | yes, #38 `1afc42e4` | open: Cameras rows do not read RSSI |
| [28-emulator](28-emulator.md) | PR28: host simulator for the furble UI | yes, #44, #67 | open: screenshot command and capture path |
| [29-virtual-test-rig](29-virtual-test-rig.md) | 29 - virtual test rig: SDL simulator plus Android emulator | yes, #71, #80 | open: phases 4 to 6 deferred |
| [30-m5stack-framework](30-m5stack-framework.md) | PR30 - Upstream M5Stack framework work and fork strategy | pins and vendoring on master | closed, D1 |
| [33-wifi-hub](33-wifi-hub.md) | 33 - WiFi hub: displayless build, provisioning, MQTT | yes, #53, #148 | open: PR33d WebUI and REST |
| [34-ota-partitions](implemented/34-ota-partitions.md) | 34 - OTA updates and the partition scheme change that enables them | yes, #32 `3bf6f2c7`, `310904c3` | archived, 7 |
| [40-thinknode-port](40-thinknode-port.md) | 40 - ThinkNode port feasibility | no, feasibility study | closed, D2 |
| [41-alternative-hardware](41-alternative-hardware.md) | 41 - Alternative sidecar hardware | no, survey | closed, D12 |
| [50-companion-app-design](50-companion-app-design.md) | 50 - Companion App Design | yes, #18, #21, #34, #55 | design reference, 4.5 |
| [51-app-feature-parity](51-app-feature-parity.md) | 51 - Companion app feature parity | yes, #55, #73, #295 | design reference, 4.5 |
| [61-camera-compatibility](61-camera-compatibility.md) | 61: Camera compatibility survey | yes for #78, #79, #81 | closed, D13 |
| [63-sim-power-analysis](63-sim-power-analysis.md) | 63 - simulator based power analysis | partly | open: not complete, figures advisory |
| [64-debug-tooling](64-debug-tooling.md) | 64: expanded debug tooling | yes, #83 `c5993f38` | open: pillar 2 queue depth and perf page |
| [65-bt-coexistence](65-bt-coexistence.md) | 65: Broader Bluetooth: Classic BT feasibility and WiFi+BLE coexistence | no, study | closed, D16 |
| [67-fujifilm-dial-detection](67-fujifilm-dial-detection.md) | 67: Fujifilm drive dial detection | no, experiment | deferred, D17 |
| [74-dji-osmo](74-dji-osmo.md) | DJI Osmo Action BLE remote support | yes, #81 `dab53fdc` | open: GPS push and R SDK commands |
| [86-sim-bug-hunt](86-sim-bug-hunt.md) | 86 - SDL simulator bug hunt | yes, #112 | open: reported #32 symptom |
| [88-alternate-hardware](88-alternate-hardware.md) | 88 - Alternate hardware for a furble remote, and Meshtastic coexistence | no, survey | closed, D14 |
| [90-scheduled-shooting](90-scheduled-shooting.md) | 90 - Scheduled Shooting (deferred) | no | deferred, D18 |
| [91-mic-trigger](91-mic-trigger.md) | 91 - Microphone Trigger (deferred) | no | deferred, D19 |
| [95-engineering-lessons](95-engineering-lessons.md) | 95 engineering lessons | n/a | reference |
| [96-wifi-bt-ble-hardening](96-wifi-bt-ble-hardening.md) | 96: WiFi, Bluetooth and BLE hardening | partly, #62, #109, #128, #307 | open: remediation continues |
| [97-followups](97-followups.md) | 97: Consolidated follow-up backlog | mostly, #530 | open: three named items |
| [97-review-feedback](97-review-feedback.md) | 97 - Review feedback | yes, #530 | record, follows 97 |
| [98-power-optimization-audit](98-power-optimization-audit.md) | 98 - power optimization audit | no, audit | closed, D3 |
| [106-flash-size-optimization](106-flash-size-optimization.md) | 106 - flash size optimization | partly, #142, #148 | open: item 3 and group C |
| [106-review-feedback](106-review-feedback.md) | 106 - Review feedback for the Waveshare alignment | yes, #457 | record, follows 106 |
| [108-upstream-issue-mapping](108-upstream-issue-mapping.md) | 108 - upstream issue mapping | no, mapping | closed, D4 |
| [111-power-recommendations](111-power-recommendations.md) | 111 - Power optimization recommendations | partly, #454 | deferred, D20 |
| [111-review-feedback](111-review-feedback.md) | 111 - Review feedback for the idle power fixes | yes, #454 | record, follows 111 |
| [114-flasher-provisioning](implemented/114-flasher-provisioning.md) | 114b - Web flasher WiFi and settings provisioning | parser landed, #165; transport #537 | archived on master, #537 |
| [115-ota-update-mqtt](115-ota-update-mqtt.md) | 115b - HTTPS OTA delivery and OTA-over-MQTT | engine landed, #168 | deferred, D25 |
| [125-esp-optimization-program](125-esp-optimization-program.md) | ESP-IDF optimization program | partly | open: OTA health integration |
| [126-esp-optimization-audit](126-esp-optimization-audit.md) | 126: ESP-IDF optimization and reproducibility audit | no, audit | closed, D5 |
| [127-nordic-port-baseline](127-nordic-port-baseline.md) | 127 - Nordic silicon port baseline | ratchet and inventory only | closed, D6 |
| [127-review-feedback](127-review-feedback.md) | 127 - Review feedback for the port stack | yes, #462 | record, follows 127 |
| [149-review-feedback](149-review-feedback.md) | 149 - Review feedback for the Ricoh GR IV standby wake | yes, #532 | record, follows 149 |
| [149-ricoh-sleep-shutter-gate](149-ricoh-sleep-shutter-gate.md) | 149: Ricoh GR IV sleep shutter gate | yes, #532 | open: hardware bench and UI surfacing |
| [158-sim-scheduler-parity](158-sim-scheduler-parity.md) | 158 - simulator scheduler and hardware parity | yes, #253 `37d9136a` | open: hardware parity |
| [159-camera-peer-certification](159-camera-peer-certification.md) | 159 - capture-backed camera peer certification | partly | open: certification gates |
| [161-sim-real-control](161-sim-real-control.md) | Plan 161: production connection stack in the simulator | yes, #261 | open: phase 3 |
| [165-sim-no-touch-layout](165-sim-no-touch-layout.md) | 165 - the simulator measured a layout no modeled board ships | yes, #264 | open: compiled-board default |
| [167-fujifilm-device-name](167-fujifilm-device-name.md) | 167 Fujifilm device name | yes, #266 `8bdc52e4` | open: four UI gaps |
| [168-notouch-layout-overflows](168-notouch-layout-overflows.md) | 168 - fix the layout the physical-button boards actually ship | yes, #270, #273 | open: see Still open |
| [173-review-feedback](173-review-feedback.md) | 173 - Review feedback for the #283 and #289 follow-up | yes, #452 | record, follows 173 |
| [173-sim-scheduler-visibility](173-sim-scheduler-visibility.md) | 173 - Scheduler-visible host mutex, one flash per simulated device | yes, #286, #452 | open: two production races |
| [287-mcu-portability-and-bundle-hardware](287-mcu-portability-and-bundle-hardware.md) | 287 - MCU portability, battery life, GPS quality, and input changes | research plus quick win | closed, D7 |
| [297-gps-improvement-roadmap](297-gps-improvement-roadmap.md) | 297 - GPS improvement roadmap after the deep review | partly, #323, #326, #328, #356 | open: verified findings |
| [302-companion-restart-reconnect](302-companion-restart-reconnect.md) | 302 - make companion stop/start and repeat connections survive | yes, #456 | open: #381 UART panic capture |
| [304-review-feedback](304-review-feedback.md) | 304 - Review feedback for the #381 and #390 closeout | yes, #456 | record, follows 304 |
| [304-webui-stick-memory](304-webui-stick-memory.md) | 304 - WebUI memory budget on the StickS3 | simulator complete | open: #390 hardware gate |
| [313-sim-only-status](313-sim-only-status.md) | 313 - Sim-only closeout status | n/a | reference, gate state |
| [314-memory-resilience](314-memory-resilience.md) | 314 - Memory resilience | yes, #411 to #439 | open: row 18 cycle memoization |
| [314-review-feedback](314-review-feedback.md) | 314 - Review feedback for memory resilience | yes, #411 to #439 | record, follows 314 |
| [317-notouch-regressions](317-notouch-regressions.md) | 317: physical-button layout suite regressions | yes, #420 | open: timer-overflow scenario |
| [318-next-board-battery-gps](318-next-board-battery-gps.md) | 318 - Next board: battery life and GPS | no, decision | open: T-Beam port in flight off master |
| [319-meshtastic-dual-boot](319-meshtastic-dual-boot.md) | 319 - furble and Meshtastic on one node (dual boot) | no, design only | deferred, D21 |
| [320-rtc-use](320-rtc-use.md) | 320 - Making better use of the ESP32 RTC domain | C1 and C4 landed | closed, D8 |
| [327-control-gps-race](327-control-gps-race.md) | 327 - Control target-set locking for the GPS fan-out | yes, #487 | open: target-side GPS handoff read |
| [328-reference-docs-audit](implemented/328-reference-docs-audit.md) | 328 - Reference documentation audit: settings and console | yes, #477 `dc5be70f` | archived, 7 |
| [332-imu-motion-scale-audit](implemented/332-imu-motion-scale-audit.md) | 332 - IMU motion wake scale audit | yes, #504 `0c585de9` | archived, 7 |
| [332-imu-motion-scale-review-feedback](implemented/332-imu-motion-scale-review-feedback.md) | 332 review feedback (IMU motion scale audit) | yes, #504 | archived, 7 |
| [333-datasheet-corpus](333-datasheet-corpus.md) | 333 - Datasheet citation corpus | partly, #475 | open: nine remaining items |
| [334-review-feedback](334-review-feedback.md) | 334 - Review feedback for the simulator and production hardware alignment | yes, #482 | record, follows 334 |
| [334-sim-hardware-alignment](334-sim-hardware-alignment.md) | 334 - Simulator and production alignment with verified hardware behaviour | yes, #482 | open: remaining gates |
| [335-review-feedback](implemented/335-review-feedback.md) | 335 - Review feedback | yes, #514 | archived, 7 |
| [335-stack-cuts](implemented/335-stack-cuts.md) | 335 - The control stack cut is not supported, and the walk now sees why | yes, #514 `9ca30588` | archived, 7 |
| [336-gps-pin-seam](implemented/336-gps-pin-seam.md) | 336 - GPS UART pin seam | yes, #510 `0f3e8905` | archived, 7 |
| [336-review-feedback](implemented/336-review-feedback.md) | 336 - Review feedback for the GPS UART pin seam | yes, #510 | archived, 7 |
| [337-sim-mem-shims](337-sim-mem-shims.md) | 337 - Simulator memory-management shims | yes, #506 `b549d36c` | open: heap_caps_zalloc gap |
| [338-e2e-page-coverage](338-e2e-page-coverage.md) | 338 - End-to-end coverage for the untested UI pages | yes, #526 | open: board matrix |
| [339-mqtt-retry-backoff](implemented/339-mqtt-retry-backoff.md) | 339 - MQTT retry backoff instead of a permanent block | yes, #498 `b3d08489` | archived, 7 |
| [340-gps-uart-install](implemented/340-gps-uart-install.md) | 340 - GPS UART install failure | yes, #509 `5021be84` | archived, 7 |
| [340-review-feedback](implemented/340-review-feedback.md) | 340 - Review feedback for the GPS UART install failure | yes, #509 | archived, 7 |
| [341-sd-shutdown-ack](341-sd-shutdown-ack.md) | 341 - SD shutdown acknowledgement | yes, #494 | open: timed shutdown and restart handshake |
| [342-review-feedback](342-review-feedback.md) | 342 - Review feedback | yes, #485 | record, follows 342 |
| [342-wifi-lost-ip](342-wifi-lost-ip.md) | 342 - WebUI network state after a DHCP lease loss | yes, #485 `7caf0468` | open: /api/status address race |
| [343-apple-ui-snapshot](implemented/343-apple-ui-snapshot.md) | 343 - Apple companion UI snapshot | yes, #480 `c7af92ea` | archived, 7 |
| [343-review-feedback](implemented/343-review-feedback.md) | 343 review feedback | yes, #480 | archived, 7 |
| [344-android-ui-snapshot](344-android-ui-snapshot.md) | 344 - Android UI snapshot test | yes, #483, #512 | open: see Still open |
| [344-review-feedback](344-review-feedback.md) | 344 review feedback | yes, #512 | record, follows 344 |
| [345-doc-citation-gate](implemented/345-doc-citation-gate.md) | 345 - A gate for 'file:line' citations in docs and plans | yes, #511 `81139818` | archived, 7 |
| [346-plans-cleanup](implemented/346-plans-cleanup.md) | 346 - plans directory cleanup | yes, #484 `a8082cc1` | archived, 7 |
| [346-review-feedback](implemented/346-review-feedback.md) | 346 - Review feedback for the plans cleanup | yes, #484 | archived, 7 |
| [347-core-debug-flash-headroom](347-core-debug-flash-headroom.md) | 347 - Core OTA debug flash headroom | yes, #524 `52b0900a` | open: part inventory check |
| [347-review-feedback](347-review-feedback.md) | 347 review feedback | yes, #524 | record, follows 347 |
| [348-review-feedback](implemented/348-review-feedback.md) | 348 - Review feedback for the StickS3 simulator alignment | yes, #502 | archived, 7 |
| [348-sticks3-sim-align](implemented/348-sticks3-sim-align.md) | 348: align the StickS3 simulator model with the verified board facts | yes, #502 `9a617355` | archived, 7 |
| [349-waveshare-board-row](349-waveshare-board-row.md) | 349 - The Waveshare ESP32-S3-ETH board row | yes, #500 `5c3b85bc` | open: console `eth status` |
| [350-review-feedback](implemented/350-review-feedback.md) | 350 - Review feedback for the StickC family fixes | yes, #505 | archived, 7 |
| [350-stickc-family-fixes](implemented/350-stickc-family-fixes.md) | 350 - StickC family fixes | yes, #505 `b56acc88` | archived, 7 |
| [352-m5gfx-board-pin](implemented/352-m5gfx-board-pin.md) | 352 - M5GFX board pinning | yes, #499 `e7cbea4d` | archived, 7 |
| [352-review-feedback](implemented/352-review-feedback.md) | 352 - Review feedback for the M5GFX board pinning | yes, #499 | archived, 7 |
| [353-hal-verification](353-hal-verification.md) | 353 - HAL verification pass 2 | yes, #479 | open: left-open list |
| [353-review-feedback](353-review-feedback.md) | 353 - Review feedback for the HAL verification pass 2 | yes, #479 | record, follows 353 |
| [354-core-family-fixes](354-core-family-fixes.md) | 354 - Core family hardware and power fixes | yes, #497 `535ad7d6` | open: item 10 and bench |
| [354-review-feedback](354-review-feedback.md) | 354 - Review feedback for the Core family hardware and power fixes | yes, #497 | record, follows 354 |
| [355-review-feedback](355-review-feedback.md) | 355 - Review feedback for the CMake entry point status | yes, #517 | record, follows 355 |
| [355-sim-cmake-entry-point](355-sim-cmake-entry-point.md) | 355: the documented CMake entry point aborted on macOS, and what fixed it | yes, #517 `3284b617` | open: smoke step |
| [356-sim-memory-certify](356-sim-memory-certify.md) | 356 - Simulator memory certification closeout | yes, #492 | open: optional-stack floor |
| [357-review-feedback](implemented/357-review-feedback.md) | 357 - Retrospective review of seven merged pull requests | yes, #520 `6591f323` | archived, 7 |
| [358-resilience-round3](358-resilience-round3.md) | 358 - Resiliency round three | yes, #516 | open: ranked findings |
| [358-review-feedback](358-review-feedback.md) | 358 - Review feedback for the resilience round three fixes | yes, #516 | record, follows 358 |
| [359-review-feedback](359-review-feedback.md) | 359 review feedback | yes, #508 | record, follows 359 |
| [359-sony-legacy-generation](359-sony-legacy-generation.md) | 359 - Sony 2019 generation: discovery, ATT MTU, location (upstream issue 316) | yes, #508 `91b29baa` | open: ranked follow-ups |
| [360-waveshare-amoled-1-8-port](360-waveshare-amoled-1-8-port.md) | 360 - Port plan for the slgray AMOLED-1.8 board | no, design | deferred, D23 |
| [362-nrf-ble-only-port](362-nrf-ble-only-port.md) | 362 - nRF port for a BLE-only furble variant | no, decision | closed, D10 |
| [363-pmic-irq-speaker](implemented/363-pmic-irq-speaker.md) | 363 - PMIC IRQ latches, the RTC IRQ write and StickS3 speaker timing | yes, #488 `4043fba0` | archived, 7 |
| [363-review-feedback](implemented/363-review-feedback.md) | 363 - Review feedback | yes, #488 | archived, 7 |
| [364-ext5v-rail](implemented/364-ext5v-rail.md) | 364 - EXT_5V rail ownership on the M5StickS3 | yes, #493 `79df13a7` | archived, 7 |
| [364-review-feedback](implemented/364-review-feedback.md) | 364 - Review feedback for the EXT_5V rail ownership branch | yes, #493 | archived, 7 |
| [365-core-sim-align](implemented/365-core-sim-align.md) | 365 - Align the simulator with the verified Core family facts | yes, #513 `b509486b` | archived, 7 |
| [365-review-feedback](implemented/365-review-feedback.md) | 365 - Review feedback for the Core family simulator alignment | yes, #513 | archived, 7 |
| [366-gps-receiver-selection](366-gps-receiver-selection.md) | 366 - GNSS receiver selection for a geotagging remote | no, decision | closed, D11 |
| [370-sim-memory-coverage](implemented/370-sim-memory-coverage.md) | Plan 370: simulator memory-management coverage audit | yes, #547 `62953874` | archived, 7 |
| [371-fork-network-sweep](371-fork-network-sweep.md) | 371 Fork network sweep | yes, #539 | open: item 4 |
| [372-web-installer-chip-family](implemented/372-web-installer-chip-family.md) | 372 - Web installer chip family | yes, #542 `cbc3a73d` | archived, 7 |
| [373-interval-shutter-hold](implemented/373-interval-shutter-hold.md) | 373: intervalometer shutter hold decision record | yes, #544 `c8159ee9` | archived, 7 |
| [374-control-release-durability](implemented/374-control-release-durability.md) | 374 Control release durability | yes, #546 `e3ddfb3d` | archived, 7 |
| [375-ci-apt-resilience](implemented/375-ci-apt-resilience.md) | Plan 375: CI apt installs survive a broken third-party repository | yes, #545 `9533920e` | archived, 7 |
| [376-board-level-documents](376-board-level-documents.md) | 376 - Board-level documents for the supported and planned boards | no, research register | closed, D26 |
| [377-gnss-vendor-audit](377-gnss-vendor-audit.md) | 377 - GNSS configuration audit against the vendor documents | yes, #501 | open: boundary GPS test |
| [378-vendor-document-verification](378-vendor-document-verification.md) | 378: Vendor document verification for the SoC, radio and board docs | partly, #475, #489 | closed, D15 |
| [379-docs-gap-closure](379-docs-gap-closure.md) | 379: component and protocol documentation gap closure | partly | open: 48 open rows |
| [380-review-feedback](implemented/380-review-feedback.md) | 380 - Review feedback | yes, #507 | archived, 7 |
| [380-sim-rtc](implemented/380-sim-rtc.md) | 380 - Battery-backed RTC in the simulator | yes, #507 `c644f39e` | archived, 7 |
| [381-display-touch-datasheet-promotions](381-display-touch-datasheet-promotions.md) | 381 Display and touch datasheet promotions | no, research | closed, D9 |
| [382-gnss-datasheet-verification](382-gnss-datasheet-verification.md) | 382 - GNSS datasheet verification | partly, comment-only corrections | deferred, D22 |
| [383-hardware-candidates](383-hardware-candidates.md) | 383 - Off-the-shelf port candidates, ranked | no, ranking | open: port in flight |
| [384-plan-number-deduplication](implemented/384-plan-number-deduplication.md) | 384 - duplicate plan numbers | yes, #538 `55987a32` | archived, 7 |
| [385-hal-register-verification](implemented/385-hal-register-verification.md) | 385 - HAL register verification pass 3 | yes, #540 `d26e9135` | archived, 7 |
| [386-board-verification](implemented/386-board-verification.md) | 386 - Board implementation verification against the vendor documents | yes, #543 `08709037` | archived, 7 |
| [389-shutter-zero-hold](implemented/389-shutter-zero-hold.md) | 389: a configured Shutter of 0 | yes, #548 `6b2da833` | archived, 7 |
| [392-ci-concurrency](implemented/392-ci-concurrency.md) | 392 - pull request concurrency cancellation | yes, #553 `1c7cdbb9` | archived, 7 |
| [473-review-feedback](473-review-feedback.md) | 473 - Review feedback for the hardware-trap corrections | yes, #473 | record, parent merged |
| [474-companion-coverage](474-companion-coverage.md) | 474 - Companion coverage audit, screenshot gallery and the golden corpus | yes, #512 | open: see Still open |
| [475-review-feedback](475-review-feedback.md) | 475 - Review feedback for the SD shutdown acknowledgement | yes, #494 | record, follows 341 |
| [511-review-feedback](implemented/511-review-feedback.md) | 511 - Review feedback for the documentation citation gate | yes, #511 | archived, 7 |
| [536-review-feedback](536-review-feedback.md) | 536 - Review feedback for the enclosure and battery branch | yes, #536 | record, parent branch |
| [537-review-feedback](implemented/537-review-feedback.md) | 537 - Review feedback | yes, #537 | archived, 7 |
| [539-review-feedback](539-review-feedback.md) | 539 - Review feedback for the fork network sweep | yes, #539 | record, follows 371 |
| [540-review-feedback](implemented/540-review-feedback.md) | 540 - Review feedback for the HAL register verification pass | yes, #540 | archived, 7 |
| [541-review-feedback](541-review-feedback.md) | 541 - Review feedback for the board-level document branch | yes, #541 | record, follows 376 |
| [542-review-feedback](implemented/542-review-feedback.md) | 542 - Review feedback | yes, #542 | archived, 7 |
| [543-review-feedback](implemented/543-review-feedback.md) | 543 - Board verification pass, review feedback | yes, #543 | archived, 7 |
| [544-review-feedback](implemented/544-review-feedback.md) | 544 - Interval shutter hold decision record, review feedback | yes, #544 | archived, 7 |
| [545-review-feedback](implemented/545-review-feedback.md) | 545 - CI apt resilience, review feedback | yes, #545 | archived, 7 |
| [546-review-feedback](implemented/546-review-feedback.md) | 546 - Review feedback | yes, #546 | archived, 7 |
| [547-review-feedback](implemented/547-review-feedback.md) | 547 - Simulator memory coverage, review feedback | yes, #547 | archived, 7 |
| [553-review-feedback](implemented/553-review-feedback.md) | PR 553 review feedback: pull request concurrency cancellation | yes, #553 | archived, 7 |


## 4. Decisions on the research and design documents

Each decision below names the evidence and the condition that would reopen it.
A closed document stays in `plans/`, because `implemented/README.md` keeps
research, design-only, branch-only and deferred documents in the parent
directory. Closing it means no one is waiting for code from it.

### 4.1 Closed, the question is answered

**D1. 30-m5stack-framework.md. Closed.**
Decision: the gap list is answered by pinning and vendoring, not by upstream
pull requests. Evidence: the document's own implementation state records master
on `M5GFX@0.2.27`, `M5Unified@0.2.20`, the standalone `M5PM1@1.0.6`, TinyGPSPlus
pinned to a full SHA, and esp-nimble-cpp vendored under `components/` through
`override_path`. The fork's dependency policy is plan 286 and its upstream audit
is plan 303, both archived. The G1, G2, G4 and G5 upstream pull requests were
never filed and filing them is not on any roadmap.
Reopen if: upstream ships a release that removes the need for the G2 patch, or a
maintainer decides to upstream a specific gap.

**D2. 40-thinknode-port.md. Closed.**
Decision: no port for the nRF ThinkNode boards; the S3 boards are board
profiles. Evidence: the verdict table at line 176 and the per-device verdicts
from line 111: M1, M3 and M4 are nRF52840 and need the platform and BLE layers
rewritten, M2 and M9 are S3 board profiles, and M5 is an S3 with GNSS. The
viable device became the recommendation in plan 41 and is ranked in plan 383.
Reopen if: a ThinkNode M5 or M9 is bought.

**D3. 98-power-optimization-audit.md. Closed as a snapshot.**
Decision: it answered its own two questions with "no" and produced the ranked
list that later plans executed. Evidence: audit base `fefbd05` on 2026-08-18;
the missing pieces it named landed as `implemented/13-auto-off-low-batt.md`, and
the budget work as plans 314 and 347. Its numbers came from the plan 63 profiler
and are advisory, as plan 63 now says.
Reopen if: a new board needs a power budget. New measurements belong to plan 63
or plan 285, not to this audit.

**D4. 108-upstream-issue-mapping.md. Closed as a historical snapshot.**
Decision: the mapping was accurate when written and is not maintained. Evidence:
the document states "No code changed. This is a mapping document only"; plan 303
replaced it with a read-only upstream audit and full-SHA pins and is archived.
Reopen if: never. A new upstream audit is a new plan.

**D5. 126-esp-optimization-audit.md. Closed.**
Decision: historical audit, superseded by the toolchain work it argued for.
Evidence: the document was prepared against `06e9333f` on 2026-08-24 and says so;
master now pins espressif32 6.13.0 with ESP-IDF 5.5.3 through plans 129 and 130,
both archived. Its P0 to P2 findings were not re-audited and are not claimed.
Reopen if: a dependency major moves again.

**D6. 127-nordic-port-baseline.md. Closed.**
Decision: what it produced is an inventory guard and a portability boundary, and
both are on master; the Nordic target itself is decided elsewhere. Evidence: the
document says line 226 that it is "documentation and an inventory guard only";
`tools/check_coupling_ratchet.py` and `tools/coupling_ratchet.json` run in
pre-review; the abstraction it argued for landed as plan 322 (#462) and the nRF
target is the documented no in plan 362.
Reopen if: the five seam extractions named in plan 362 land.

**D7. 287-mcu-portability-and-bundle-hardware.md. Closed.**
Decision: research, and its verdict is that no new MCU is needed. Evidence: line
546, "Neither goal needs a new MCU. Both goals need a different board"; the
implementation state at line 927 records research plus one companion quick win
and no firmware change, and that quick win (iOS background location) is on
master. The portability seam continues in plans 127 and 322.
Reopen if: a requirement appears that the ESP32-S3 cannot meet at all, rather
than one that a different board meets better.

**D8. 320-rtc-use.md. Closed.**
Decision: its rank 1 idea is implemented and its rank 4 idea landed with plan
350; the rest need a part this fork does not have. Evidence: C1 at line 80 is the
retained wall clock, and `src/FurbleTimeKeeper.cpp:52` defines
`FURBLE_TIME_RETAINED` as `RTC_NOINIT_ATTR`, documented at `src/CLAUDE.md:76`.
C4's timed wake landed through plan 350. The remaining ideas need a 32.768 kHz
source, which `plans/00-hardware-experiments.md` records the StickS3 lacks.
Reopen if: a board with a 32.768 kHz crystal is owned.

**D9. 381-display-touch-datasheet-promotions.md. Closed.**
Decision: it is a record of what was re-read on 2026-11-26 and which items stayed
unverified, and that is a finished research product. Evidence: the promotion
tables and the unverified list it carries; the display and touch pages it cites
are in `docs/components/`, and the citation gate from plan 345 enforces the page
references.
Reopen if: a display or touch document revision is re-read.

**D10. 362-nrf-ble-only-port.md. Closed with the answer no.**
Decision: a documented no until the seam extraction lands. Evidence: the decision
section at line 485 and the implementation state at line 754, which says the plan
adds no firmware environment, board or profile. The five seam extractions it
names are the reopen condition.
Reopen if: all five seam extractions land.

**D11. 366-gps-receiver-selection.md. Closed as a decision record.**
Decision: the receiver choice is made at the level of levers and candidates, and
the bench work that would settle it belongs to plan 318. Evidence: the
implementation state at line 472 says "Research and documentation only. No code,
board or support claim is owed by this plan"; the ranking in plan 318 and plan
364 uses it.
Reopen if: bench data exists for one of the seven receivers.

**D26. 376-board-level-documents.md. Closed as a research register.**
Decision: the register is the product, and the five rows that stay open are parked
where the next scan can act on them. Evidence: the status line says research and
document register, with no firmware, board, driver or support claim, and points at
`docs/power/board-document-gaps.md` for the open rows. The section at line 126
names V_IO on the M10S trim, the GPIO7 question, the M5StickC part values, the
Pager and Ultra drawing dates and the Heltec revision differences, and none of
them is code: each needs a continuity check, an FCC exhibit or a vendor change
log. This document landed after the inventory base, on `789dd78c` (#541), and is
covered here rather than left unswept.
Reopen if: the T-Beam MAX-M10S trim reaches a bench, which is the only way to
settle V_IO and the GPIO7 question.

### 4.2 Closed, superseded by a later document

**D12. 41-alternative-hardware.md. Superseded by 383 and 318.**
Decision: the survey's job is done and its recommendation was overtaken. Evidence:
line 52 recommends the Elecrow ThinkNode M5 at 64,90 EUR. Plan 383 ranks the
LilyGO T-Beam Supreme first, on the first non-M5 board and the first AXP2101 rail
model outside `M5.Power`, and plan 318 records the battery argument behind it.
The survey's hardware facts survive in 383's tables.
Reopen if: the T-Beam port is abandoned and a pocket-form-factor board is wanted
again; the M5 would then be the first candidate.

**D13. 61-camera-compatibility.md. Superseded by the shipped vendor plans and 159.**
Decision: close the survey, not a camera. Evidence: line 3 records that Lumix
(#79), Pentax K (#78) and DJI Osmo (#81) shipped from this survey, and line 39
records the Lumix port. Feature-level certification is plan 159's fail-closed
contract. Every remaining vendor row is a candidate with no capture, not a
commitment.
Reopen if: a protocol capture exists for one of the ranked vendors, for example
Insta360, which needs the peripheral BLE role.

**D14. 88-alternate-hardware.md. Superseded by 319.**
Decision: its coexistence verdict is the design plan 319 carries. Evidence: line
304 concludes that the two firmwares stay separate, with a partition switch and a
boot selector on a 16 MB ESP32-S3, and that "the only furble-specific work
coexistence adds is a partition table and a boot-time selector". The partition
layout is plan 34 and the design is plan 319.
Reopen if: the dual boot in 319 is scheduled; 319 is the document to edit.

**D15. 378-vendor-document-verification.md. Superseded by 333.**
Decision: the facts moved into the citation corpus. Evidence: plan 333 records a
page per part with revision, page and hash, and `docs/components/` carries the
rows; the citation gate from plan 345 checks them. Keeping a second copy of the
same facts invites disagreement, which `plans/382-gnss-datasheet-verification.md`
already records happening once.
Reopen if: a document revision changes a fact a page depends on; the page and the
corpus are the place to record it, not this file.

### 4.3 Closed, not worth doing

**D16. 65-bt-coexistence.md. Closed, not worth doing.**
Decision: do not start a Classic Bluetooth effort. Evidence: the verdict at line
270 rejects the full Bluedroid migration ("Do not attempt"), rates the
Nikon-pairing-only variant L because it adds a build-matrix dimension and a
bonding hack, and rejects BTstack as XL. Its own conclusion is "Verdict for this
fork: not worth it for Nikon alone. Do not start a Classic effort." Only the four
plain ESP32 boards would benefit, the reference camera is Fujifilm, and plan 61
ranked Lumix, Pentax K and DJI ahead of Nikon smart on effort to value.
Reopen if: upstream ships the Nikon-specific Bluedroid build as a variant. Adopt
it as a build variant; do not port a Classic stack here.

### 4.4 Deferred, with the condition that reopens it

**D17. 67-fujifilm-dial-detection.md. Deferred.**
Decision: keep the experiment and the two open questions, do not schedule them.
Evidence: line 33 names them, mapping notifications to dial positions and
finding which 0x45xx characteristic is the drive mode, and both need a session
with the camera awake and the dial turning. No firmware consumer exists: no page,
setting or shutter path branches on drive mode today.
Reopen if: a Fujifilm behaviour bug needs the dial state. Smallest slice: one
body, one session, log the 0x45xx notifications while turning the dial, and stop
there.

**D18. 90-scheduled-shooting.md. Deferred.**
Decision: keep deferred on its own revisit trigger. Evidence: the trigger is PR19
shipped and verified by the overnight timelapse run where the device wakes,
reconnects, shoots and sleeps again. Plan 19 is implemented and archived, but
hardware validation is deferred in sim-only mode, so that run has not happened
([313](313-sim-only-status.md)).
Reopen if: the resume criteria in 313 are met and that overnight run passes. The
camera time sync half stays closed until a vendor time-only protocol is confirmed
or the companion app can supply the timestamp.

**D19. 91-mic-trigger.md. Deferred.**
Decision: keep deferred. Evidence: its trigger requires the power work shipped
and the battery behaviour measurable. Plans 07, 12, 13 and 19 are all archived,
but the measurement is deferred with 313, and the plan's own power argument is
that continuous sampling holds the L3B rail that the power work exists to drop.
Reopen if: hardware power measurement resumes and a user asks for the feature.
Arming a microphone on an uncalibrated model is the one outcome to avoid.

**D20. 111-power-recommendations.md. Deferred by design.**
Decision: no setting default changes on this evidence. Evidence: the document
says it is a recommendation only and that every default change is the
maintainer's call, and its own simulator review note says the connection,
scheduler and electrical models are incomplete until plan 158 lands. The three
questions are policy, not code, and the 12x figure is a model estimate.
Reopen if: the power model is calibrated and hardware measurement resumes, or a
maintainer answers the three questions. Then this becomes a plan with a default
change and a validator.

**D21. 319-meshtastic-dual-boot.md. Deferred design.**
Decision: the design is complete and the prerequisites do not exist yet.
Evidence: the implementation state at line 343 says research and design only, and
names the prerequisite mark-valid wiring as belonging to plans 125 and 115. Plan
125's own state says `evaluateBootHealth` still has no caller and no
`esp_ota_mark_app_*` call exists.
Reopen if: a 16 MB ESP32-S3 board is owned and the OTA mark-valid wiring lands.
Until both hold, the 8 MB verdict in this plan (furble loses self-OTA) is the
answer and it is not attractive.

**D22. 382-gnss-datasheet-verification.md. Deferred.**
Decision: the facts left open need documents that are not in this tree. Evidence:
the section beginning at line 140 names each fact and the document that would
settle it. Nothing in the firmware depends on the open ones; the AT6668 and
CASIC facts this pass did settle are in `docs/components/`.
Reopen if: a CASIC or AT6668 document that settles one of those rows is obtained.

**D23. 360-waveshare-amoled-1-8-port.md. Deferred.**
Decision: a complete port plan for a board nobody owns. Evidence: the open items
at line 468 and no `amoled` or slgray environment on master, in `platformio.ini`
or in the board profile.
Reopen if: the slgray AMOLED-1.8 board is bought. The profile row, the pin
collisions and the CO5300 question are already written.

**D24. 114-flasher-provisioning.md. Closed: implemented in #537.**
Decision: the browser transport landed while this branch was open, so the
deferral recorded here is over. Evidence: #537 (`70efa7c7`) adds the WebSerial
config step, its encoder and its response parser to `web-installer/`, and master
moved the plan to `implemented/114-flasher-provisioning.md`. The parser from
#165 is still recorded in `implemented/114-provision-parser.md`.
Reopen if: never. A new provisioning surface is a new plan.

**D25. 115-ota-update-mqtt.md. Deferred.**
Decision: the engine shipped, the delivery paths did not. Evidence: the status
line says design only, `implemented/115-ota-engine.md` records #168, and the
contract it would use is archived as plans 130, 131 and 132.
Reopen if: a signed update path is wanted. Smallest slice: the HTTPS transport
against the existing engine, with one board and one image.

### 4.5 Design documents that stay by nature

Plans [50](50-companion-app-design.md) and [51](51-app-feature-parity.md) are
design references, not tasks. Both carry their own implementation state: the
firmware GATT service and the Android app for 50 (#18, #21, #34, #55), and the
firmware camera management plus the Android settings editors for 51 (#55, #73,
#295). Each keeps its own open questions section. They are not closed here
because a design document is current until its last open question is answered,
and both still have live ones. The inventory marks them `design reference`.

### 4.6 Reference documents

[00-hardware-experiments.md](00-hardware-experiments.md),
[95-engineering-lessons.md](95-engineering-lessons.md) and
[313-sim-only-status.md](313-sim-only-status.md) are cited, not implemented.
00 holds measured hardware facts and is cited by plans and docs instead of
re-measuring. 95 holds durable lessons. 313 holds the sim-only gate results and
the resume criteria. None of them is an open plan, and none moves to
`implemented/`, which is for completed implementations.

## 5. What stays open, and why

These are real implementation plans with named work left. They stay in `plans/`,
their row in the index keeps pointing here, and the item below is what a
contributor would pick up. Nothing in this list is ambiguous: each item is
concrete and each has an owner document.

| Plan | What keeps it open |
| --- | --- |
| [17](17-imu-gestures.md) | The StickS3 deep wake path is documented but not implemented (line 18), and the 25 Hz display-off rate is deliberately undecided pending step 8 of the hardware gate (line 369). The shipped position is 50 Hz, off by default. |
| [18](18-gps-motion.md) | Phase 2 receiver policy: nothing consumes the receiver duty state (line 4). |
| [25](25-multiconnect-ui.md) | The Cameras rows do not read cached RSSI (line 4), so the page shows state without signal strength. |
| [28](28-emulator.md) | The screenshot command and `tools/furble-screenshot.py` capture path (line 7). |
| [29](29-virtual-test-rig.md) | Phases 4 to 6 were intentionally not implemented (line 717); the rig is fork-only until upstream lands its transport. |
| [33](33-wifi-hub.md) | PR33d WebUI and REST are deferred (lines 1300 to 1301), and the intervalometer is unavailable in the displayless profile (line 385). |
| [50](50-companion-app-design.md), [51](51-app-feature-parity.md) | Design references with live open questions; both carry implemented halves. |
| [63](63-sim-power-analysis.md) | The plan is not complete and its figures are advisory (line 15); the sticky pull-request report is not built. |
| [64](64-debug-tooling.md) | Pillar 2's queue-depth and Performance diagnostics page items (line 49). |
| [74](74-dji-osmo.md) | The GPS push path and the remaining DJI commands are deferred (line 53). |
| [86](86-sim-bug-hunt.md) | The reported #32 symptom stays open (line 4). |
| [96](96-wifi-bt-ble-hardening.md) | Remediation continues; the inventory's per-item state is not re-audited (line 3). |
| [97](97-followups.md) | Three items, recorded in the review record: the PM1 watchdog feeder, the A3b Basic registration gate and the scan-end success drain. |
| [106](106-flash-size-optimization.md) | Item 3 (newlib nano), item 6 (icon source hygiene) and group C feature gating (line 401). |
| [125](125-esp-optimization-program.md) | The OTA health integration: `evaluateBootHealth` has no caller and no `esp_ota_mark_app_*` call (line 3). |
| [149](149-ricoh-sleep-shutter-gate.md) | Phase 2 landed as #532 (`21692490`) and both phases are still hardware bench pending, so the awake capture, the asleep refusal and the reconnect wake are unproven on a camera. The refusal also does not surface through the UI yet. |
| [158](158-sim-scheduler-parity.md) | Hardware parity is not started; phase 1 is #253 and phase 2 is plan 161 (lines 15 and 24). |
| [159](159-camera-peer-certification.md) | Certification gates: family aliases stay uncertified without an exact model and firmware corpus (line 216). |
| [161](161-sim-real-control.md) | Phase 3, scheduler fairness and calibrated peripheral models (line 457). |
| [165](165-sim-no-touch-layout.md) | Deriving the simulator default from the compiled board (line 260). |
| [167](167-fujifilm-device-name.md) | Four UI gaps recorded in the plan's follow-up section, as plan 346 also concluded. |
| [168](168-notouch-layout-overflows.md) | The Still open section (line 433), including the unresolved input-focus and timer boundary. |
| [173](173-sim-scheduler-visibility.md) | Two production races: Control flag synchronization (line 550) and reconnect state fields (line 591). |
| [297](297-gps-improvement-roadmap.md) | The verified findings still open (line 30). |
| [302](302-companion-restart-reconnect.md) | A UART0 panic capture and the hardware retest of the USB recovery; issue #381 stays open. |
| [304](304-webui-stick-memory.md) | The #390 hardware memory gate, TLS handshake, settings write and soak (line 265). |
| [314](314-memory-resilience.md) | Row 18, cycle memoization, as the review record states. |
| [317](317-notouch-regressions.md) | `bughunt/timer-overflow-no-timed-wake.txt` still fails (line 117). |
| [318](318-next-board-battery-gps.md) | The next-board decision is being executed off master: the T-Beam Supreme profile is `494a3410` on `feat/tbeam-supreme-port`, not on master. |
| [327](327-control-gps-race.md) | The target-side GPS handoff read in the `CMD_GPS_UPDATE` case is unlocked (line 131). |
| [379](379-docs-gap-closure.md) | 48 open documentation rows, each naming the document and section it needs (line 178). |
| [377](377-gnss-vendor-audit.md) | A boundary GPS test that compiles `src/FurbleGPS.cpp` against doubles, plus the hardware gate for `$PCAS03` (line 394). |
| [333](333-datasheet-corpus.md) | Nine remaining items, including two missing documents and three live hazards (line 263). |
| [334](334-sim-hardware-alignment.md) | The remaining gates (line 365). |
| [337](337-sim-mem-shims.md) | The `heap_caps_zalloc` gap, which has no firmware caller today (lines 355 and 376). |
| [338](338-e2e-page-coverage.md) | The board matrix is not closed: every scenario lists `m5stick-s3` (line 76). |
| [341](341-sd-shutdown-ack.md) | `Platform::powerOffUntil()` and `restart()` do not run the SD handshake (line 121). |
| [342](342-wifi-lost-ip.md) | `/api/status` can answer `connected:true` with an empty `ip` between the event and the cache fill (line 196). |
| [344](344-android-ui-snapshot.md) | The Still open section (line 170), including the platform jar download and the golden corpus. |
| [347](347-core-debug-flash-headroom.md) | Settle the StickC, Plus2 and Core2 part inventory before those lanes opt out of the BMI270 blob (line 206). |
| [349](349-waveshare-board-row.md) | The console `eth status` command is not implemented (line 290). |
| [353](353-hal-verification.md) | The left-open list: BQ27220 pacing, SX1262 sleep on a part, the AXP2101 IRQ latch contract, and `lib/hal` outside the format gate (line 191). |
| [354](354-core-family-fixes.md) | Item 10, gating the debug console display mode, plus the bench items (line 362). |
| [355](355-sim-cmake-entry-point.md) | A smoke step that builds the CMake target and runs one scenario (line 140). |
| [356](356-sim-memory-certify.md) | An optional-stack concept in the floor document and ratchet, so the new stack can carry a floor (line 174). |
| [358](358-resilience-round3.md) | The ranked findings that were not implemented (line 214). |
| [359](359-sony-legacy-generation.md) | The ranked follow-ups, including the MTU floor for the other families and the ATT status plumbed through `gattWrite` (line 263). |
| [383](383-hardware-candidates.md) | The ranking is live while the T-Beam port is in flight; no candidate is bought or measured (line 290). |
| [371](371-fork-network-sweep.md) | Item 4, the camera photo and movie mode design note (line 82). Item 5, the fourth intervalometer roller, is closed here as not worth doing: one clean cherry-pick with no user-visible value, and the roller set is already full on the smallest panel. |
| [474](474-companion-coverage.md) | The Still open section (line 180). |

The records that stay with an open plan are
[18-review-feedback](18-review-feedback.md), [97-review-feedback](97-review-feedback.md),
[106-review-feedback](106-review-feedback.md), [111-review-feedback](111-review-feedback.md),
[127-review-feedback](127-review-feedback.md), [173-review-feedback](173-review-feedback.md),
[304-review-feedback](304-review-feedback.md), [314-review-feedback](314-review-feedback.md),
[334-review-feedback](334-review-feedback.md), [342-review-feedback](342-review-feedback.md),
[344-review-feedback](344-review-feedback.md), [347-review-feedback](347-review-feedback.md),
[353-review-feedback](353-review-feedback.md), [354-review-feedback](354-review-feedback.md),
[355-review-feedback](355-review-feedback.md), [358-review-feedback](358-review-feedback.md),
[359-review-feedback](359-review-feedback.md), [475-review-feedback](475-review-feedback.md),
[536-review-feedback](536-review-feedback.md) and [539-review-feedback](539-review-feedback.md).
Two of them have no plan to pair with and are not moved here:
[473-review-feedback](473-review-feedback.md) reviews the hardware-trap corrections
in #473 and reads as a closed record, and
[536-review-feedback](536-review-feedback.md) reviews the enclosure and battery
branch. Moving a review record away from the work it reviews loses that
juxtaposition, which is the reason the convention pairs them.

## 6. Status lines this record corrects

Twelve documents say a branch was not merged when its merge is on master. The
document keeps its text; the correction is recorded here so the stale line is
not read as the current state.

| Document | Stale line | Merge on master |
| --- | --- | --- |
| [337](337-sim-mem-shims.md) | "Not merged. Branch `fix/sim-mem-shims`" | #506 `b549d36c` |
| [350](implemented/350-stickc-family-fixes.md) | "Status: implemented on `fix/stickc-family`, not merged" | #505 `b56acc88` |
| [354](354-core-family-fixes.md) | "Status: implemented on `fix/core-family`, not merged" | #497 `535ad7d6` |
| [352](implemented/352-m5gfx-board-pin.md) | "Status: implemented on `fix/m5gfx-board-pin`" | #499 `e7cbea4d` |
| [363](implemented/363-pmic-irq-speaker.md) | "Status: implemented on `fix/pmic-irq-and-speaker`" | #488 `4043fba0` |
| [340](implemented/340-gps-uart-install.md) | "Status: implemented on branch `fix/gps-uart-install`" | #509 `5021be84` |
| [342](342-wifi-lost-ip.md) | "Status: implemented on `fix/wifi-lost-ip`" | #485 `7caf0468` |
| [339](implemented/339-mqtt-retry-backoff.md) | "Done on `fix/mqtt-retry-backoff`" | #498 `b3d08489` |
| [336](implemented/336-gps-pin-seam.md) | "Status: implemented on `feat/gps-profile-pins`" | #510 `0f3e8905` |
| [34](implemented/34-ota-partitions.md) | "The fork-specific web installer is implemented on branch `feat/web-installer-fork`" | `310904c3` |
| [115](115-ota-update-mqtt.md) | "`plans/33-wifi-hub.md` PR33b (WiFi): not landed" | #53 `790500d8` |
| [386](implemented/386-board-verification.md) | "Status: implemented on `board-verification-pass`, not merged" | #543 `08709037` |

Two more status facts worth recording, because they change what the documents
mean. Plan 320's line 3 says "Nothing here is implemented", while its first
ranked idea is `src/FurbleTimeKeeper.cpp:52`. Plan 337's follow-up section says
the info watermark ignored the pool, which is a later fix folded into the same
file, not an open item.

## 7. Plans archived in this PR

Twenty-seven plans and twenty-four records move to `implemented/`. Each merge commit
below is an ancestor of `origin/master`, and each row names an artifact on
master. Hardware, soak and device-look gates stay in the moved text, which
`plans/CLAUDE.md` allows.

| Plan | Merge | Artifact on master |
| --- | --- | --- |
| [34](implemented/34-ota-partitions.md) | #32 `3bf6f2c7`, `310904c3` | `partitions/` with the two-slot layout, and `.github/workflows/pages.yml` with `web-installer/` |
| [328](implemented/328-reference-docs-audit.md) | #477 `dc5be70f` | the corrected `docs/settings-and-controls.md`, `docs/console-commands.md` and their wiki mirrors |
| [332](implemented/332-imu-motion-scale-audit.md) | #504 `0c585de9` | the shared motion scale and its `static_assert` in the IMU backends |
| [335](implemented/335-stack-cuts.md) | #514 `9ca30588` | the withdrawn control cut in `tools/stack_budget.json` and the resolved dispatch |
| [336](implemented/336-gps-pin-seam.md) | #510 `0f3e8905` | `Board::gpsPins()` and the no-GPS profile rows |
| [339](implemented/339-mqtt-retry-backoff.md) | #498 `b3d08489` | the capped MQTT retry backoff and its host double |
| [340](implemented/340-gps-uart-install.md) | #509 `5021be84` | the absent-receiver path and the install-failure scenario |
| [343](implemented/343-apple-ui-snapshot.md) | #480 `c7af92ea` | the committed Apple status baseline and its comparison test |
| [345](implemented/345-doc-citation-gate.md) | #511 `81139818` | `tools/check_doc_citations.py` and its unit tests |
| [346](implemented/346-plans-cleanup.md) | #484 `a8082cc1` | the archived plans and the corrected index it produced |
| [348](implemented/348-sticks3-sim-align.md) | #502 `9a617355` | the StickS3 model, `PWR_CFG_RESET` and `PWR_CFG_BOOT` |
| [350](implemented/350-stickc-family-fixes.md) | #505 `b56acc88` | the single LED and IR pin owner, board polarity and the rail facts |
| [352](implemented/352-m5gfx-board-pin.md) | #499 `e7cbea4d` | the pinned M5GFX board return before the first probe |
| [363](implemented/363-pmic-irq-speaker.md) | #488 `4043fba0` | the IRQ Status 2 clear, the profile-gated RTC write and the speaker session |
| [364](implemented/364-ext5v-rail.md) | #493 `79df13a7` | `RailHolder` and the single `PWR_CFG` writer |
| [365](implemented/365-core-sim-align.md) | #513 `b509486b` | the Core family model and the IP5306 window |
| [370](implemented/370-sim-memory-coverage.md) | #547 `62953874` | the simulator memory-management coverage audit and its scenarios |
| [372](implemented/372-web-installer-chip-family.md) | #542 `cbc3a73d` | `tools/web_installer_layout.py`, the one-family manifest and its regression test |
| [373](implemented/373-interval-shutter-hold.md) | #544 `c8159ee9` | the decision record itself, with the 30 ms default unchanged |
| [374](implemented/374-control-release-durability.md) | #546 `e3ddfb3d` | the durable control release path and its host double |
| [375](implemented/375-ci-apt-resilience.md) | #545 `9533920e` | the CI apt install guard |
| [380](implemented/380-sim-rtc.md) | #507 `c644f39e` | the modelled RTC in `sim/` and the four deep-sleep scenarios |
| [384](implemented/384-plan-number-deduplication.md) | #538 `55987a32` | the seven renamed plan files and the renumbering map in this index |
| [385](implemented/385-hal-register-verification.md) | #540 `d26e9135` | the HAL driver fixes and their host cases |
| [386](implemented/386-board-verification.md) | #543 `08709037` | `docs/components/board-verification.md` and the corrected board rows |
| [389](implemented/389-shutter-zero-hold.md) | #548 `6b2da833` | the zero-hold shutter release fix |
| [392](implemented/392-ci-concurrency.md) | #553 `1c7cdbb9` | the pull request concurrency cancellation |
The records that move with them are
[380-review-feedback](implemented/380-review-feedback.md),
[332-imu-motion-scale-review-feedback](implemented/332-imu-motion-scale-review-feedback.md),
[335-review-feedback](implemented/335-review-feedback.md),
[336-review-feedback](implemented/336-review-feedback.md),
[340-review-feedback](implemented/340-review-feedback.md),
[343-review-feedback](implemented/343-review-feedback.md),
[346-review-feedback](implemented/346-review-feedback.md),
[348-review-feedback](implemented/348-review-feedback.md),
[350-review-feedback](implemented/350-review-feedback.md),
[352-review-feedback](implemented/352-review-feedback.md),
[363-review-feedback](implemented/363-review-feedback.md),
[364-review-feedback](implemented/364-review-feedback.md),
[365-review-feedback](implemented/365-review-feedback.md),
[511-review-feedback](implemented/511-review-feedback.md) and
[537-review-feedback](implemented/537-review-feedback.md),
[540-review-feedback](implemented/540-review-feedback.md),
[542-review-feedback](implemented/542-review-feedback.md),
[543-review-feedback](implemented/543-review-feedback.md),
[545-review-feedback](implemented/545-review-feedback.md),
[546-review-feedback](implemented/546-review-feedback.md),
[547-review-feedback](implemented/547-review-feedback.md),
[544-review-feedback](implemented/544-review-feedback.md) and
[553-review-feedback](implemented/553-review-feedback.md), which follow
their plans, plus [357-review-feedback](implemented/357-review-feedback.md), the
retrospective of seven merged pulls (#520 `6591f323`) that has no parent plan.

## 8. What this record does not do

- No code, test, simulator, workflow, setting or sitemap change. The diff is
  Markdown only.
- No plan is renumbered here. The 377 to 383 run belongs to plan 384 (#538) and is
  not touched.
- No issue is closed. The ownership table in `plans/README.md` still maps each
  open issue to its plan; archiving a plan does not close an issue, and the one
  open row that names a plan archived here, #455, now points at the archived
  path.
- No hardware claim is made. Every bench, measurement and soak gate that was open
  before this pass is open after it, and stays in the text of its plan.
- The closed research documents are not moved to `implemented/`. That directory
  is for completed implementations, and its README says so.
- Two plans whose work is complete but whose follow-ups are owned elsewhere are
  left in place on purpose: [347](347-core-debug-flash-headroom.md) (the part
  inventory check) and [341](341-sd-shutdown-ack.md) (the timed shutdown and
  restart handshake). Archiving them would drop a named software item, and the
  convention is to archive only when no implementation work remains.

## 9. Verification

The pass used the repository's own gates:

- `python3 tools/check_pr_hygiene.py origin/master...HEAD`, which also checks
  every local link in `plans/README.md`.
- `python3 tools/check_doc_citations.py --root .`, which checks every `file:line`
  citation in the Markdown this change touches and in the repository.
- `python3 tools/pre_review.py --base origin/master`, the same scanner set CI
  runs for a pull request.

A plans-only change does not need a firmware build, and PlatformIO CI agrees:
`.github/workflows/main.yml` ignores `**/*.md` on `pull_request` and its change
filter skips `plans/*`, so it does not queue for this diff. Three other workflows
still matched on Markdown, because this change repoints plan links inside
`include/CLAUDE.md` and `tests/host/CLAUDE.md`: `reproducible.yml` and
`static-analysis.yml` on `include/**`, and `host-sanitized.yml` on `include/**`
and `tests/host/**`. `coverage.yml`, `power-gate.yml` and `sim-e2e.yml` have no
path filter at all and run on every pull request. Six firmware build jobs
therefore queue for a Markdown-only change. Narrowing those filters is its own
change and is deliberately not part of this one; no workflow is edited here.
