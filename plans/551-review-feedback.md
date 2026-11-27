# 551 - Review feedback

An independent, adversarial review of the T-Beam Supreme port: the board row and
`Board::compiledProfile()`, the AXP2101 power path in `src/FurblePlatform.cpp`
through `lib/hal` and `RegisterBus`, the `FurbleDisplay` facade and M5GFX's
`Panel_SH110x`, the GPS pin seam, the environment and `sdkconfig`, the
simulator's 1-bit model, the two scenarios, the host test and plan 367.

Reviewed at head `8691165a`, base `baa4ac23`. Class names that the LilyGO-family merge renamed are noted in place. Line references were re-pointed
to the post-fix tree on 2026-11-27, where a fix moved the code they name; the
findings, evidence and verdicts are as delivered. Every hardware fact was checked
against the vendor documents rather than against plan 367, and the two places
the PR could not test, the panel probe and the panel geometry, both turn out to
be wrong in the same direction: the code cannot fail, so nothing on the branch
or in the simulator can see that it does not work. One power-path gate has the
opposite failure, a total loss of battery, rail and power-off control from a
single unretried identity read.

The hardware verification is the strongest part of the PR and is better than the
average in this tree. Everything below was confirmed against
`Xinyuan-LilyGO/LilyGo-LoRa-Series` at master (`examples/Factory/utilities.h`
lines 514-570 and `examples/Factory/LoRaBoards.cpp` lines 26, 172-243, 766) and
the LilyGO wiki page `docs/get_started/en/LoRa_GPS/T-Beam-SUPREME/T-Beam-SUPREME.md`
in `LILYGOpp/LILYGO_wiki`:

- GNSS UART: `GPS_RX_PIN (9)`, `GPS_TX_PIN (8)`, used as
  `SerialGPS.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN)`, which is
  ESP32 rx 9 and tx 8. `src/FurbleGPS.cpp:230` calls `uart_set_pin(port, tx, rx)`,
  so the row's RX 9 / TX 8 is the same pair. The row is correct.
- Rails: ALDO4 3300 is the GPS, ALDO3 the radio, the cold-boot cycle cuts
  ALDO1/ALDO2/BLDO1 and waits 250 ms, then ALDO1/ALDO2/BLDO1/BLDO2 go to 3300,
  DCDC3 3300, DCDC4 at maximum, DCDC5 3300, and DCDC2, DLDO1, DLDO2 and VBACKUP
  are off. The sequence in `axpBringUp()` matches, with ALDO3 and the M.2
  DCDC3/4/5 group as the two deviations the plan records.
- Channel assignment: ALDO1 is BME280 + Display + MAG, ALDO2 Sensor, ALDO3 Radio,
  ALDO4 GPS, BLDO1 SD Card, BLDO2 external header.
- Addresses and buses: OLED 0x3C/0x3D and the QMC6310 at 0x1C/0x3C both on bus 0
  (SDA 17, SCL 18); AXP2101 0x34 on bus 1 (SDA 42, SCL 41, IRQ 40); microSD on its
  own SPI at SCK 36, MISO 37, MOSI 35, CS 47; GNSS backup from the 18650; GPIO7
  the L76K-only wake input; BOOT on GPIO0.
- `lib/hal/AXP2101.cpp` register and bit assignments agree with the rows recorded
  in `docs/components/axp2101.md`, and `setRail()` refuses to cut DCDC1
  (`lib/hal/AXP2101.cpp:142-144`), which is the one write that would kill the
  board rather than blank it.
- `sdkconfig.lilygo-tbeam-supreme` differs from `sdkconfig.m5stick-s3` in exactly
  two things, `CONFIG_ESP32S3_RTC_CLK_SRC_EXT_CRYS` and quad instead of octal
  PSRAM. Nothing risky is toggled.

| # | Severity | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Blocking | The address probe cannot fail, so neither `begin()` nor `ready()` means anything and 0x3D is never reached. `src/FurbleDisplay.cpp:130` treats `beginTransaction(...).has_value()` as the acknowledgement, but M5GFX defers the address NACK: `beginTransaction` returns `restart(...)` (`common.cpp:1966-1969`), which returns success as soon as the command is queued (`common.cpp:1844-1846` checks only a *previous* transaction's state), and the ACK error is raised in `i2c_wait()` (`common.cpp:1514-1560`), reached through `endTransaction` (`common.cpp:2046-2051`). That result is discarded at `src/FurbleDisplay.cpp:131`. Consequences: 0x3C is always taken, so on a band variant whose panel is at 0x3D nothing is drawn and the init commands go to whatever answers 0x3C, which the vendor wiki puts a QMC6310 at; with no panel at all `begin()` still returns true and `ready()` still reports safe to draw. This contradicts `include/FurbleDisplay.h:35-40`, the commit message ("refuses to draw when neither answers"), `plans/367-tbeam-supreme-port.md:105-107` and the PR body. | Open. Take `endTransaction`'s result as the acknowledgement, for example `const bool ack = beginTransaction(...).has_value() && endTransaction(I2C_PORT).has_value();`. A NACK does not poison the next probe: `i2c_wait` force-stops and sets `state_disconnect` (`common.cpp:1603`), and `restart` returns early success for that state, so 0x3D is still reachable after a failed 0x3C. | `sed -n '1966,1969p;2046,2051p' .pio/libdeps/lilygo-tbeam-supreme/M5GFX/src/lgfx/v1/platforms/esp32/common.cpp`; `sed -n '125,142p' src/FurbleDisplay.cpp`. |
| 2 | Blocking | The device panel is configured 128x128 while the sim models 128x64, and the column offset has no code path at all. the device panel class (`src/FurbleDisplay.cpp:373-431`, `Sh1106Panel` after the LilyGO seam merged; `I2cPanel` in the pre-549 file) never calls `m_Panel.config()`, so `Panel_SH110x`'s constructor defaults stand, `memory_height = panel_height = 128` (`Panel_SSD1306.hpp:141-146`), and its init list programs multiplex 0x7F (`Panel_SSD1306.hpp:181`) and COM pins 0x12 (`:190`) for a panel that has 64 commons and expects 0x3F, which is what M5GFX's own 128x64 SSD1306 class writes (`:117`). The simulator sets 128x64 (`src/FurbleDisplay.cpp:79-91`), so the two do not describe the same hardware and a scenario cannot catch a geometry error. Separately `offset_x` stays 0, while `Panel_SH110x::display()` adds `_cfg.offset_x` to the column (`Panel_SSD1306.cpp:353-354`); there is no `setWindow` in `Panel_SSD1306.*`, so `plans/367-tbeam-supreme-port.md:107-108` and the comment at `src/FurbleDisplay.cpp:51-53` are both wrong about who handles the SH1106's 2-column RAM offset. On a 64-row panel the device also fills 128 rows: `Panel_SSD1306.cpp:359` then writes pages 8 to 15 as 0xB8 to 0xBF, which the SH1106 decodes as pages 0 to 7. | Open. Configure the panel the way the sim already does, `panel_width/panel_height/memory_width/memory_height = 128/64`, and add `offset_x` with the value the bench check settles (0 today, 2 if the module is the usual centred 132-column part). Then re-measure the 1-bit render on both sides. | `awk '/struct Panel_SH110x/,/^  };/' .pio/libdeps/lilygo-tbeam-supreme/M5GFX/src/lgfx/v1/panel/Panel_SSD1306.hpp`; `grep -n setWindow .pio/libdeps/lilygo-tbeam-supreme/M5GFX/src/lgfx/v1/panel/Panel_SSD1306.*` returns nothing. |
| 3 | Blocking | One unretried identity read disables the whole power backend, while the row still advertises power off. `axpBringUp()` returns before any rail when `g_Axp.probe()` fails (`src/FurblePlatform.cpp:255-258`), `g_AxpReady` stays false, and every consumer then refuses: `axpSwitch` (`:237-245`), `readRailState` (`:1672-1678`), `readBattery` (`:1700+`), `powerOff` (`:885-892`). The GPS rail is therefore never enabled and the GPS reports absent, the battery reads zero, and Auto off refuses to act, while `src/FurbleBoard.cpp:206-208` keeps `canPowerOff` and `powerOffPath` true so the UI offers it. The identity byte is UNVERIFIED in this tree: `docs/components/axp2101.md:17` records 0x47/0x57 from the revision 0.1 datasheet, while `lib/hal/AXP2101.cpp:27` requires 0x4A, which M5Unified 0.2.20 also requires (`AXP2101_Class.cpp:31-34`) on shipped AXP2101 boards. 0x4A is the better evidence, but the failure is total and there is no retry, unlike the M5PM1 path whose own comment notes that the first access after an idle period only wakes the part (`src/FurblePlatform.cpp:324-326`). | Open. Retry the probe (two or three attempts, as `axpSwitch` already does) before giving up, and consider accepting a status-register read as a fallback identity so a mismatch degrades to fuel gauge and rails rather than to nothing. Also read back ALDO1 once after the sequence: the read-modify-write on REG 0x90 cannot tell a corrupt-but-accepted read from a good one, and a panel rail that ended up off would be silent. | `sed -n '250,300p' src/FurblePlatform.cpp`; `sed -n '28,40p' .pio/libdeps/lilygo-tbeam-supreme/M5Unified/src/utility/power/AXP2101_Class.cpp`. |
| 4 | Non-blocking | The power-path comment states an invariant the code breaks. `src/FurblePlatform.cpp:178-180` says ALDO1 is never switched off because it feeds the panel, the BME280 and the I2C pull-ups, and `:267-272` switches it off in the cold-boot cycle. It is harmless today only because `axpBringUp()` (`:460`) runs before `Display::begin()` (`:467`); a reorder blanks the panel and takes the bus pull-ups with it. The vendor code does the same cut, so the cut is the right behaviour and the comment is wrong. | Open. Say what the code does: the vendor cold-boot cycle cuts ALDO1 for 250 ms before the panel exists, and the rail is never cut after bring-up. | `sed -n '176,184p;264,275p;455,470p' src/FurblePlatform.cpp`. |
| 5 | Non-blocking | The boot splash is invisible on this board. `Panel_SH110x`'s init list leaves the panel at contrast 0x00 (`Panel_SSD1306.hpp:189`), `Platform::init()` runs `Display::begin()` (`src/FurblePlatform.cpp:467`), `BootScreen::begin()` draws at `src/main.cpp:449`, and the first `setBrightness` is in the UI constructor (`src/FurbleUI.cpp:539`). Every M5 board leaves `M5.begin()` with a usable backlight, so this is new behaviour rather than a port of it. | Open. Apply the stored brightness at the end of `Display::begin()` on the 128x64 path, or before the first boot-screen draw. | `grep -n "setBrightness" src/FurbleUI.cpp src/FurblePlatform.cpp src/FurbleDisplay.cpp src/main.cpp`. |
| 6 | Non-blocking | The `-debug` figures are pre-rebase and no CI lane builds that environment. `plans/367-tbeam-supreme-port.md:242` quotes flash 2,211,952, DRAM 52,404 and IRAM 129,792 as recorded on this branch, but the follow-up commit re-recorded only the release numbers and the host count, so the debug triple comes from the tree before master's five commits. `.github/workflows/main.yml:243-250` hardcodes the six release environments and appends `<env>-debug` for those only, and `.github/workflows/reproducible.yml:76` adds the release environment alone, so `lilygo-tbeam-supreme-debug` is built by no workflow even though `tools/size_budget.json:100-107` pins its budget. The release margins are 820 bytes of DRAM and 768 bytes of IRAM, so a stale figure is not a rounding question. | Open. Either re-measure the debug environment on the rebased tree, or label the numbers pre-rebase in the plan. Adding the environment to `main.yml`'s discovery list is the only thing that keeps the pinned budget honest, since `check_size_budget.py` only fails for an environment something builds. | `grep -rn lilygo-tbeam .github/workflows/` returns one release line; `sed -n '236,280p' .github/workflows/main.yml`. |
| 7 | Non-blocking | The AXP2101 component page still says nothing uses the driver. `docs/components/axp2101.md:8-10`: "`lib/hal/AXP2101.cpp` is a pure driver and no shipped board instantiates it: the only caller in the tree is `tests/host/hal_drivers_test.cpp`". This PR is the first caller in firmware, and the page is not in the diff. | Open. Update the Status row and the role paragraph to name the T-Beam Supreme and the `-debug` bench image. | `git diff --name-only origin/master...HEAD \| grep axp2101` is empty; `sed -n '8,18p' docs/components/axp2101.md`. |
| 8 | Non-blocking | The two new scenarios are allowed but not required. `tools/check_sim_scenarios.py:17` adds the board to `SUITE_BOARDS["e2e"]`, but `REQUIRED_SELECTIONS` at `:18-23` still lists only the three M5 boards, so both T-Beam scenarios can be deleted and the gate still passes. They are the only evidence in the tree that either receiver trim is modelled. | Open. Add `lilygo-tbeam-supreme` to `REQUIRED_SELECTIONS["e2e"]`. | `sed -n '8,25p' tools/check_sim_scenarios.py`; `python3 tools/check_sim_scenarios.py` passes with the board absent from the required set. |
| 9 | Nit | Three documented promises have no implementation behind them. `include/FurbleDisplay.h:38-42` says width and height are 0 before `begin()`, while `src/FurbleDisplay.cpp:173-183` returns 128 and 64 unconditionally, so `BootScreen` treats a dark board as shown. `ready()` and `mono()` have no caller, and `I2cPanel::address()` (`:144`, the class is `Sh1106Panel` now) has none either. | Open. Either make width and height depend on `g_Ready` as documented, or change the header; drop or use the three dead accessors. | `grep -rn "Display::ready\|Display::mono" src sim include` returns only the definitions. |
| 10 | Nit | The citation names a branch that does not exist. `plans/367-tbeam-supreme-port.md:54` and `:75`, and `src/FurblePlatform.cpp:167-168`, cite `Xinyuan-LilyGO/LilyGo-LoRa-Series` "at master, branch `T_BEAM_S3_SUPREME`"; the repository has one branch, master, and the file is the `T_BEAM_S3_SUPREME` *section* of `utilities.h`. The facts are right, the pointer is not. | Open. Cite `master`, section `T_BEAM_S3_SUPREME`. | `gh api repos/Xinyuan-LilyGO/LilyGo-LoRa-Series/branches --jq '.[].name'` returns `master`. |
| 11 | Nit | The display still takes the backlight's clock lock. `src/FurbleUI.cpp:523` acquires `APB_FREQ_MAX` with the comment about backlight PWM, while `include/FurbleBoardCaps.h:100-107` says this panel has no backlight and needs no lock for one. On the board whose stated purpose is a light-sleep floor that pins the CPU for the whole display-on window. | Open. Gate the acquire on the panel class, or record that the lock is deliberately kept. | `sed -n '519,524p' src/FurbleUI.cpp`; `grep -rn APB_FREQ_MAX src/*.cpp`. |

Verified, no change needed:

| Check | Result | Evidence |
| :--- | :--- | :--- |
| GPS UART pins and direction | Hold, against the vendor code rather than the wiki's TX/RX labels. | `utilities.h:518-520`; `LoRaBoards.cpp:26`, `:766`; `src/FurbleGPS.cpp:230` puts tx before rx. |
| No GPS enable pin, GPIO7 never driven | Holds. The row has no enable column and nothing in the image configures GPIO7. | `grep -rn "GPIO_NUM_7\b\|gpio_set_level.*7" src/` finds nothing; `src/FurbleBoard.cpp:187-196`. |
| Rail order and values | Hold, except the two documented deviations. | `LoRaBoards.cpp:172-243` against `src/FurblePlatform.cpp:249-300`. |
| DCDC1 protection and no write to it | Holds; this is the anti-brick guard and it is in place. | `src/FurblePlatform.cpp:217`; `lib/hal/AXP2101.cpp:142-144`. |
| `Rail::EXT_5V` mapped to ALDO4 with a readback and one retry | Holds, and a refused cut restarts a stopped receiver instead of leaving it stopped. | `src/FurblePlatform.cpp:1525-1538`; `src/FurbleGPS.cpp:1078-1120`. |
| Neither receiver detected | Holds. The ladder fails, `onProbeFailed` cuts the rail and reports absent, and the retry re-enables it through the rail account. | `src/FurbleGPS.cpp:596-610`, `:555-568`. |
| Backend reselection after a reboot | Holds. `m_Backend` starts UNKNOWN each boot and a new autobaud lock resets it, so nothing persists a stale choice. | `src/FurbleGPS.cpp:575`, `:585-590`. |
| The wrong receiver answering | Holds for the two trims in scope: the MON-VER poll only sets UBX when a u-blox answers, and everything else keeps the NMEA plus `$PCAS` path. | `src/FurbleGPS.cpp:1739-1749`. |
| M5 side stays in the adapter | Holds and improved. No `M5.Display` remains outside `src/FurblePlatform.cpp`, and the ratchet records the reduction: `src/FurbleUI.cpp` 82 to 50, `src/FurbleSD.cpp` 7 to 2, `src/FurbleBootScreen.cpp` 2 to 0. | `python3 tools/check_coupling_ratchet.py --base origin/master`: clean, 93 m5 calls in 10 files. |
| The M5GFX pin actually takes the no-probe return | Holds on the source. `patches/m5gfx_board_noprobe.patch` returns the StampS3 family before the first probe, and the pin is in the env's flags. No CI step runs `check_m5gfx_pinning.py` for this environment, and `REQUIRED_PINS` does not list it, so nothing would notice if the pin were dropped. | `patches/m5gfx_board_noprobe.patch`; `tools/check_m5gfx_pinning.py:48-53`; `platformio.ini:100`. |
| Port A is ignored by the row | Holds, and the host test pins it. M5Unified's StampS3 Port A is 15/13, which the row never reads. | `tests/host/board_profile_test.cpp:302-308`; `M5Unified.cpp:93`. |
| The 1-bit conversion agrees between device and sim | Holds. M5GFX converts through `swap565_t` (`Panel_SSD1306.cpp:157-171`), which cancels LVGL's `push` swap exactly as `toMono()` does. | `src/FurbleDisplay.cpp:66-77`; `Panel_SSD1306.cpp:151-178`. |
| The sdkconfig carries only the two board facts | Holds. | `diff <(grep ^CONFIG_ sdkconfig.m5stick-s3) <(grep ^CONFIG_ sdkconfig.lilygo-tbeam-supreme)`. |
| The scenario draw-buffer assertions are real | Hold: `drawBufferBytes(128, 64, 2)` is 512 and `singleDrawBuffer(128)` is true, which is what the scenarios assert. | `include/FurbleUIMemory.h:15-63`; `sim/scenarios/e2e/tbeam-supreme-m10s.txt:18-19`. |
| The heap model matches the plan | Holds: 47,312 static, 284,208 at app start, 86,944 attributable, 197,264 derived, tagged `derived`. | `sim/coverage/board_heap.json`, `lilygo-tbeam-supreme` entry. |

Verification run by the reviewer on this head, macOS host, no device attached:

| Check | Result |
| :--- | :--- |
| Host suite, `cmake -S tests/host` then `ctest` | 224 of 224 passed, 485.65 s, including `board-profile` and `board-profile-tbeam` |
| Simulator, `FURBLE_LILYGO_TBEAM_SUPREME` build, `run-e2e.sh` with `FURBLE_SIM_BOARD_ID=lilygo-tbeam-supreme` | `PASS tbeam-supreme-l76k`, `PASS tbeam-supreme-m10s`, all 2 pass |
| `python3 tools/pre_review.py --base origin/master` | 13 of 13 gates, with clang-format installed |
| `python3 tools/check_coupling_ratchet.py --base origin/master` | clean, 93 m5 calls in 10 files, 25 board guards in 12 files |
| `python3 tools/check_sim_scenarios.py` | manifest complete |
| `python3 tools/check_doc_citations.py --root .` | 0 errors of 3200 citations in 181 files |

What the review could not run: no ESP-IDF or xtensa toolchain is installed on the
reviewing host, so the firmware images were not rebuilt and the size and stack
numbers were checked for self-consistency against `tools/size_budget.json` and
the plan, not re-measured. The `-debug` environment is the one figure the branch
itself has not re-measured (finding 6). The 249-scenario M5StickS3 regression was
not re-run, so the "changed no M5 board" claim rests on the host suite and the
coupling counts above.

The verdict: findings 1 to 3 are the merge blockers. Finding 1 is four
characters of intent in the wrong place, finding 2 is a dozen lines modelled on
the simulator the PR already writes, and finding 3 is a retry loop. None of them
change the design, and all three are in the part of the port that no scenario
can currently reach.
