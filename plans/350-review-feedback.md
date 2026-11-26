# 350 - Review feedback for the StickC family fixes

Implementation state: reviewed at `75ec29ad` on `fix/stickc-family`, with the
findings below fixed in the follow-up commit on the same branch. Hardware gates
stay where plan [350](350-stickc-family-fixes.md) puts them, because this review
had no board, scope or analyzer.

## Scope

An independent adversarial review of PR #505 against plan 350, the pinned
libraries in `.pio/libdeps` (M5Unified 0.2.20, M5GFX 0.2.27), the pinned ESP-IDF
5.5.3 driver sources, and the host and boundary doubles. The pull request had no
human or Copilot review threads at the time of the review
(`gh api graphql` returned an empty `reviewThreads` list), so every row below
comes from this review rather than an existing thread.

The review treated the eight defects in the plan as claims to falsify, then
looked for behaviour the description does not mention. Rows 1 to 8 are real
findings and all are fixed. Rows 9 to 19 are the specific questions this review
was asked to attack, plus the description gaps that needed a decision; each is
recorded with the evidence that closed it.

## Findings

| # | Area | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | PinShare build | `include/FurblePinShare.h` and `src/FurblePinShare.cpp` guarded the glue with `defined(FURBLE_PIN_SHARE) \|\| defined(FURBLE_SIM)`. The simulator never defines `FURBLE_PIN_SHARE`, never compiles `src/FurblePinShare.cpp`, and shims both `FurbleFeedback.h` and `FurbleIR.h`, so the clause declared three functions in a build that could not link them. The `platformio.ini` comment repeated the claim ("the sim sets"). | Fixed. The `FURBLE_SIM` clause is removed from both guards, the header records why, and the `platformio.ini` comment now says the simulator is not a consumer. | `sim/CMakeLists.txt:120-187` (`FURBLE_SOURCES` has no `FurbleFeedback.cpp`, `FurbleIR.cpp` or `FurblePinShare.cpp`), `sim/shim/FurbleFeedback.h`, `sim/shim/FurbleIR.h`, `grep -rn FURBLE_PIN_SHARE` finding only `platformio.ini` and the boundary targets |
| 2 | PinShare fallback | `PinShare::endIr()` released the pad as an input when Feedback had never recorded a level, and the comment claimed that was "the state the boot path leaves it in when light feedback is off". It is not: M5Unified's `Power_Class::begin()` puts GPIO19 on the output path for the Plus2 before furble runs. The release also left an NPN base floating rather than at the known inactive level, and a later `gpio_set_level()` cannot reach an input pad. | Fixed. The pad is parked at `ledLevel(false, board.ledActiveLow)` on the GPIO output path, so it is never floating and every later level write reaches it. The header, `src/CLAUDE.md`, the LK-03 section and plan 350 carry the new rule. New ctest case `boundary-shared-pin-shared-off` covers it. | M5Unified 0.2.20 `Power_Class.cpp:577-585`; `src/FurblePinShare.cpp:64-75`; `tests/host/boundaries/shared_pin_test.cpp` `runSharedOff()` |
| 3 | Rail evidence | Defect 8 named only the M5GFX write (`0x12 = 0x4D`). `M5.begin()` also calls `Power.setExtOutput(cfg.output_power)`, and `output_power` defaults to true, which reaches `Axp192.setEXTEN(true)` on the StickC and Plus. The comment therefore understated the boot-time write that matters and would read as stale if the M5GFX board driver changed. | Fixed in `src/FurbleBoard.cpp`, `src/FurbleGPS.cpp`, `src/CLAUDE.md`, `docs/power/board-power.md` and the plan, each naming both writes. The conclusion (the rail is already on, so `enable()` is not a power-on edge) is unchanged and now has two independent sources. | `M5Unified.hpp:133` (`output_power = true`), `M5Unified.cpp:2015`, `Power_Class.cpp:951-990` (`setEXTEN` for that board pair), `AXP192_Class.cpp:228-240` |
| 4 | Docs accuracy | The LK-03 rejected-option row in `docs/power/board-power.md` still said the Plus2 LED polarity "is unverified", contradicting the profile fact this PR adds. The same section's risk paragraph also described the old input release. | Fixed. The row says the driven-stage polarity is a profile fact per family, and the risk paragraph says an unrecorded level is parked at the inactive level. | `docs/power/board-power.md`, LK-03 section and rejected-options table |
| 5 | Plan evidence | The strongest source for `ledActiveLow` was missing: M5Unified's own LED setup uses `invert = true` on GPIO10 for the StickC and Plus and `invert = false` on GPIO19 for the Plus2. The plan also did not say that the StickC emitter row inherits the StickC Plus schematic page. | Fixed. The plan's evidence table cites `Power_Class.cpp:1225-1250`, `src/FurbleBoard.cpp` names it too, and the UNVERIFIED list now separates the LED fact (library-confirmed on all three rows) from the StickC emitter fact (inherited from the Plus page, because both boards use G9). | M5Unified 0.2.20 `Power_Class.cpp:1225-1250`; `plans/350-stickc-family-fixes.md` |
| 6 | Board header | The new `rtcBatteryBacked` comment said the calendar survives a power-off "by a cell or by a rail the board keeps alive", which does not describe the StickC or Plus rows: those have neither, and master already marks them true. | Fixed by rewording to the invariant the code actually relies on: the board is expected to keep its calendar, and the chip's voltage-low flag guards a calendar that did stop. The flag is read on a backed row only, so a stopped calendar is rejected on every `true` row. | `include/FurbleBoard.h`, `src/FurbleTimeKeeper.cpp:128-152`, M5Unified `PCF8563_Class.cpp:34-37` |
| 7 | Description gap | Marking the Plus2 backed also suppresses the NVS shutdown checkpoint (`needsNvsCheckpoint = !g_RtcBatteryBacked`), so the BM8563 becomes the only retention mechanism across a Plus2 power-off. The PR description did not say so. | Recorded, no behaviour change: it is the intended consequence of a backed row. Plan 350 now states the failure mode too. If the BM8563 does not survive HOLD, the next boot sees the voltage-low flag, rejects the calendar and falls back to the last NVS record, which is at most six hours old. A stale clock, never a fabricated one. | `src/FurbleTimeKeeper.cpp:394`, `plans/350-stickc-family-fixes.md` defect 2 |
| 8 | Description gap | The IR inversion also changes the StickC and Plus idle level: before this PR the pad stayed muxed to the RMT signal after a frame at the un-inverted idle level, which held the emitter conducting between frames. The PR table does not mention that, only plan 350 does. | Recorded, no code change. The fix is correct: `init_level` and `eot_level` are 0, so the inverted pad idles high on the cathode-on-pin rows, which is emitter off. | ESP-IDF 5.5.3 `rmt_tx.c:332` (`init_level`), `:759` (`eot_level`), `src/FurbleIR.cpp` (`tx = {}` leaves both 0) |
| 9 | PinShare | Can a blink interleave inside an RMT carrier? | False. `Arbiter::requestLed()` records the level and returns false while `owner == IR`, and `beginIr()`/`endIr()` bracket the whole frame. The boundary case blocks inside `rmt_transmit()` and asserts the pad level does not move while the frame owns it. | `include/FurblePinShare.h:73-83`, `tests/host/boundaries/shared_pin_test.cpp` `runShared()` |
| 10 | PinShare | Can the arbiter leave the pad owned by nobody, or deadlock? | False. `endIr()` is the only transition back and always sets `Owner::LED`, so the pad is never ownerless after a frame. Every path after `beginIr()` in `IR::transmitSymbols()` reaches `releasePin()`, which calls `endIr()`: the reattach failure, the carrier failure, the enable failure and the final path. There is one IR task and a queue depth of one, so no second frame can hold the pin. | `src/FurbleIR.cpp` `transmitSymbols()`, `include/FurblePinShare.h:85-97`, `src/FurbleIR.cpp` queue creation |
| 11 | PinShare | Is the mutex held across a transfer? | False. `beginIr()` and `endIr()` each take the lock and return; `rmt_transmit()` and `rmt_tx_wait_all_done()` run between them with no lock held. A blink during a frame returns from `requestLed()` without blocking. | `src/FurblePinShare.cpp:45-57`, `src/FurbleIR.cpp` `transmitSymbols()`, `tests/host/boundaries/shared_pin_test.cpp` |
| 12 | PinShare | Does an unrecorded level fall back to input safely? | It no longer falls back to input; row 2 is the fix. As shipped it did not key the emitter, but it floated an NPN base, did not match the boot state and would have made a later level write inert. | Rows 2 and 9 |
| 13 | IR polarity | Is the inversion applied exactly once? | Yes. `m_MarkLow` feeds `rmt_tx_channel_config_t.flags.invert_out` at channel creation and the same flag on `rmt_tx_switch_gpio()`, which re-runs the GPIO matrix connection with that inversion instead of adding a second one. The reattach only runs on the shared pin, which is the Plus2 row where the flag is 0. | ESP-IDF 5.5.3 `rmt_tx.c:1135-1175` (`esp_rom_gpio_connect_out_signal(..., invert_out, ...)`), `include/driver/rmt_tx.h:197` |
| 14 | IR polarity | Does the electrical duty still produce the optical duty the protocol needs? | Yes, and the flip is required. The carrier modulates the high level (`@param level Which level to modulate on`), so a mark is high for `duty_cycle` of the carrier period. On an inverted row the pad is low, and therefore lit, for `1 - duty_cycle`. Duty 2/3 on the StickC and Plus rows yields an optical 1/3, which is what the un-inverted Plus2 row produces with 1/3. | ESP-IDF 5.5.3 `hal/esp32/include/hal/rmt_ll.h:337-342`, `rmt_tx.c:897-905` (high and low tick counts), `src/FurbleIR.cpp` carrier block |
| 15 | IR polarity | Is the Plus2 NPN path right rather than inverted twice? | Yes. The Plus2 row is `irMarkLow = false`, so the matrix does not invert, and the NPN stage supplies the single inversion that turns a high pad into light. `ledActiveLow = false` on the same row is the same fact for the LED, and M5Unified's own LED driver agrees (`invert = false` on G19). | `src/FurbleBoard.cpp` `STICKC_PLUS2`, M5Unified `Power_Class.cpp:1225-1250` |
| 16 | RTC | Does the 2 s uncertainty hide a real clock loss? | No. A calendar that lost its supply raises the voltage-low flag, and a backed row rejects the read instead of seeding the clock. The flag is also self-healing: `setDateTime()` writes the seconds register with bit 7 clear, so the next trusted NTP or GPS write clears the latch. The 2 s window only covers the read and the one-second register resolution, which is the same policy the three existing backed rows use. | M5Unified `PCF8563_Class.cpp:34-37` (`getVoltLow`), `:96-119` (`setDateTime` writes register `0x02`), `src/FurbleTimeKeeper.cpp:128-152`, `include/FurbleTimeKeeper.h` |
| 17 | RTC | Does the timed wake work on a board whose hold pin is absent? | The code path does. `powerOffUntil()` arms the countdown, verifies it by register readback, rejects an early wake and then calls `powerOff()` instead of driving a hold pin, and `consumeTimedWake()` reads the timer flag on the next boot. `Platform::getInstance()` already skips `M5.Rtc.disableIRQ()` for `RTC_ALARM` rows, so the flag survives boot. The remaining risk is not code: the BM8563 rail during an AXP192 shutdown and the R401/C401 coupling into the power key. Both stay the hardware gate plan 350 records. | `src/FurblePlatform.cpp:724-838`, `:842-860`; M5Unified `AXP192_Class.cpp:212-215` (`powerOff` is `0x32` bit 7, a soft shutdown); `Power_Class.cpp:1728-1733` |
| 18 | GPIO35 | Is the three-source claim right? | Yes. M5Unified's AXP192 init writes `0x42 = 0x03` (IRQ 3, power key enable) and `getPekPress()` reads and clears `0x46` bits 0 and 1, which is the status the comment says would be lost by masking the source. | M5Unified `Power_Class.cpp:701`, `AXP192_Class.cpp:275-280` |
| 19 | AXP192 ADC | Is the corrected mask reason right? | Yes. In the pinned source only the `board_M5StackCore2`/`board_M5Tough` case reads `Axp192.getVBUSCurrent()`, and the StickC and Plus case only calls `setEXTEN()`. The mask value is unchanged, so Core2 and Tough behaviour is identical. | M5Unified `Power_Class.cpp:951-990` |

## Behaviour the description does not mention

Rows 7 and 8 are the two behaviour consequences found. Both are intended and are
now recorded in plan 350. Nothing else changed outside the eight defects:

- The simulator model and scenario assertions the PR body maps out belong to
  `fix/stickc-sim-align`. The four files it names
  (`sim/scenarios/e2e/hw-stickc-plus2-capabilities.txt`,
  `hw-stickc-plus-capabilities.txt`, `hw-plus2-shared-led-ir.txt` and the
  `STICKC_PLUS2_EXTRA` row) do not exist at this head, so `sim-e2e` cannot
  contradict this PR. That claim was checked and is not a finding for this
  branch.
- `FurbleGPS`, `FurbleUIIntervalometer` and `FurblePlatform` changes are comment
  or log-string only, except the `powerHoldPin < 0` branch, which is defect 7.
- No setting, default, wire id or NVS key changed.

## Verification

Every command below ran on the reviewed tree after the fixes, on macOS with the
pinned M5Unified 0.2.20 and M5GFX 0.2.27 trees.

| Check | Command | Result |
| :--- | :--- | :--- |
| Host suite | `cmake -S tests/host -B /tmp/furble-505-host -DCMAKE_BUILD_TYPE=Debug && cmake --build /tmp/furble-505-host -j8 && ctest --test-dir /tmp/furble-505-host -j4 --output-on-failure` | `100% tests passed out of 207`, 155.01 s |
| Pin share | `ctest --test-dir /tmp/furble-505-host -R 'pin-share-policy\|boundary-shared-pin' -V` | `100% tests passed out of 4`. `pin-share-policy: PASS`, `pin.shared frame=blocked blink=suppressed level=0 invert_out=0`, `pin.shared handover=returned restored_level=1`, `pin.shared frames=2 switch_gpio=1 restored=1`, `pin.shared-off handover=returned direction=output parked_level=0` |
| IR polarity | `ctest --test-dir /tmp/furble-505-host -R 'boundary-ir-polarity\|boundary-ir-' -V` | `100% tests passed out of 3`. `ir.polarity family=stickc invert_out=1 mark_level=1 emitter_mark=0 duty_permille=667`; `family=stickc-plus` the same; `family=stickc-plus2 invert_out=0 mark_level=1 emitter_mark=1 duty_permille=333` |
| Board profile | `ctest --test-dir /tmp/furble-505-host -R 'board-profile' -V` | `board-profile` passed. The Plus2 row is backed, the Plus row is `RTC_ALARM`, and the polarity columns match the expected table. |
| Time keeper | `ctest --test-dir /tmp/furble-505-host -R 'time-keeper' -V` | `time-keeper-policy` and `time-keeper-persistence` passed, with `pm1-poll-policy` in the same run, 4 of 4. |
| Pre-review | `python3 tools/pre_review.py --base origin/master` | `PASS: 11 gates` |
| Doc tokens | `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present` |
| Merge gate | `python3 tools/check_review_threads.py --repo MaxRink/furble --pr 505` | RESULT_GATE |

### Size and CI notes

`build (m5stack-core-debug)` is the lane that constrains this PR. This branch
adds the arbiter glue, so `FURBLE_PIN_SHARE` keeps it out of every image that has
no LED pin. The head this review started from measured 1,739,961 bytes of the
1,740,800-byte slot (839 spare), and PR #518, already on master, widened the
same lane by compiling it at `ESP_LOG_INFO`; the merge therefore does not trim
this PR.

RESULT_MERGE
