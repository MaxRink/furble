# 334 - Simulator and production alignment with verified hardware behaviour

Status: implemented on `fix/sim-hw-align`. No hardware was available for this
change, so every claim below rests on a vendor document or vendor source that is
already pinned in the tree. Nothing here is a bench measurement.

## Motivation

Three hardware rules were verified against vendor documents, and the tree
disagreed with each one in a different way: a production timing path that
cannot meet its datasheet minimum, a documentary value that the shipped
firmware overwrites at boot, and a simulator that was asked to model a panel
rule it has no bus for. Each item is a correctness fix, not a redesign.

## Item 1 - W5500 RESETn pulse

### Source

WIZnet W5500 datasheet version 1.1.0, dated 17DEC2022, live at
https://docs.wiznet.io/img/products/w5500/W5500_ds_v110e.pdf, 1,893,745 bytes,
sha256 `7b826b808084ccd986bcc22904c00a07a508ef42fb93d079fe7150a4c4f1a63d`.
Section 1.1 page 10, pin 37 `RSTn`: "RESET should be held low at least 500 us
for W5500 reset". Section 5.5.1 page 60 and figure 22, Reset Timing: `TRC`
minimum 500 us, `TPL` (RSTn released to internal PLOCK) maximum 1 ms. The corpus
URL for v1.0.6 returns HTTP 404, and the v1.1.0 document history on pages 66 to
67 changes only the RPU minimum and the package text, so both reset figures are
unchanged from v1.0.6.

### What the tree did

`src/FurbleEthernet.cpp` passed GPIO 9 to the ESP-IDF W5500 PHY through
`eth_phy_config_t::reset_gpio_num` and left the pulse to the framework. The
pinned framework is ESP-IDF 5.5.3 (`platformio.ini`,
`framework-espidf@3.50503.0`), whose
`components/esp_eth/src/spi/w5500/esp_eth_phy_w5500.c`:

- calls `phy->reset_hw(phy)` from `esp_eth_driver_install()`
  (`components/esp_eth/src/esp_eth.c:248`), immediately before `mac->init()`
  starts talking SPI to the chip, and
- hardcodes `esp_rom_delay_us(100); // insert min input assert time` in
  `w5500_reset_hw()`, with `reset_gpio_num` as its only input.

So the pulse is a fixed 100 us, one fifth of the datasheet minimum, and it
cannot be lengthened from the driver config.

### Change

- `src/FurbleEthernet.cpp` adds `W5500_RESET_LOW_US = 500` with the datasheet
  clause in the comment, and a private `resetW5500()` that configures GPIO 9 as
  an output, drives it low, waits `esp_rom_delay_us(500)`, and drives it high.
- `init()` calls `resetW5500()` before the SPI bus, the MAC/PHY objects and the
  driver install, so the pulse precedes `esp_eth_driver_install()` (which runs
  the PHY hardware reset and then the MAC init over SPI) and therefore also
  precedes `esp_eth_start()`.
- `phyConfig.reset_gpio_num = -1`, so `w5500_reset_hw()` skips the pin. Handing
  the pin over as well would append a second, 100 us assertion after the
  production one and leave the last reset under-spec.
- The line is left driven high and is re-pulsed on every `init()`, so the
  stop-then-restart path resets the chip too.

### Evidence

`tests/host/boundaries/ethernet_w5500_test.cpp` previously asserted only that
the PHY received GPIO 9. It now fails if the pulse is shorter than 500 us:

- `tests/host/boundaries/boundary_host.h` and `boundary_support.cpp` keep a
  virtual microsecond clock that only the `esp_rom_delay_us()` double advances,
  and record every GPIO level change with its timestamp.
- The test measures the low-to-high interval per pulse, asserts it is at least
  500 us, asserts the line is an output and left high, asserts the pulse is
  complete before `esp_eth_driver_install()` and before `esp_eth_start()`, and
  asserts the restarted transport drives a fresh pulse.
- Measured defect check: with `W5500_RESET_LOW_US = 100` the test fails with
  `the W5500 RESETn pulse is shorter than the datasheet 500 us`. With the
  original code (`reset_gpio_num = ETH_SPI_RST_GPIO`, no explicit pulse) it
  fails with 11 messages, including the pulse-did-not-complete and PHY-owns-the-pin
  ones. The passing run prints `eth.reset pulse_us=500 min=500 pin=9`.
- `sim/scripts/run-boundary-conformance.sh` gains an `eth.reset pulse_us=`
  token and `sim/coverage/hardware_quirks.json` lists `eth.reset` on
  `boundary.waveshare_eth`.

### Left alone

- No settling delay after the pulse is released, which is an open item rather
  than a compliance claim. Figure 22 bounds `TPL`, the release to internal
  PLOCK, at 1 ms maximum, so the MAC init over SPI can reach the chip before
  its PLL locks. ESP-IDF adds no wait either, because `reset_gpio_num = -1`
  skips `w5500_reset_hw()`. Adding a wait would change production timing and
  needs a board check, so it is recorded in
  `docs/power/datasheet-register.md` item 9 and in the remaining gates below.
- The SPI pins, clock, queue depth and interrupt pin are unchanged.
- No change to `esptool`/monitor behaviour, or to the Waveshare board profile.

## Item 2 - IP5306 light-load cutoff is 64 s, not 32 s

### Source

- Injoinic IP5306 datasheet V1.01: the boost shuts off after continuous load
  below 45 mA for 32 s. The public copy does not tabulate the register map.
- M5Unified 0.2.20 `src/utility/Power_Class.cpp` (pinned in `platformio.ini`),
  board `board_M5Stack` branch of `Power_Class::begin()`: writes
  `0x02 = 0b01101100` (`0x6C`) to `REG_SYS_CTL2`, and its own bitfield comment
  decodes bits 3:2 as `Light load shutdown time setting (00:8s ; 01:32s ;
  10:16s ; 11:64s)`. Bits 3:2 of `0x6C` are `11`, which is 64 s. 32 s is the
  power-on default (`01`), not the shipped value.

### What the tree did

`include/FurbleSettingsDefaults.h` and the whole `docs/power/` corpus quoted the
32 s datasheet default as the shipped Core behaviour. `docs/power/board-power.md`
even quoted the written register value correctly and then read the wrong field.

### Change

- `include/FurbleSettingsDefaults.h`: the Core Basic comment now says 64 s and
  names the register and field.
- Every live document that stated the cutoff is corrected to 64 s, with the
  register evidence where the document carries citations:
  `docs/power/board-power.md` (summary, board table, boot register block, sleep
  table, PM-06, PM-07, PM-09, PM-12, the run-length note, and both source
  rows, the M5Unified one now naming `Power_Class.cpp` for the register
  writes and `IP5306_Class.cpp` for the constants and the keep-on setter),
  `docs/power/datasheet-register.md`, `docs/power/ble.md`,
  `docs/power/measurement.md`, `docs/power/measurement-plan.md`,
  `docs/power/display.md`, `docs/power/opportunity-matrix.md`,
  `tools/power-model/README.md` and the `board-currents.yaml` threshold note.
  Every one of them states both numbers where the distinction matters: 32 s is
  the power-on default, 64 s is what the firmware selects.
- `plans/implemented/292-low-power-defaults.md` and
  `plans/98-power-optimization-audit.md` are archived records, so per
  `plans/CLAUDE.md` their text is unchanged and each gains a `## Follow-up:`
  section that corrects the number and points here.
- Confirmed that nothing rewrites the field later: no furble source writes
  IP5306 `0x02`, and `M5.Power.powerOff()` only clears `REG_SYS_CTL0` bit 1
  through `IP5306_Class::setPowerBoostKeepOn()`.

### Simulator

The simulator does not model the cutoff. `sim/hardware_model.cpp` models the
IP5306 as profile facts (`pmic ip5306`, four-level gauge, latched power,
unavailable watchdog) and `sim/scenarios/e2e/hw-pmic-ip5306-core.txt` asserts
those fields. There is no continuous-load accounting, no boost event and no
power-cut event to drive, so there is no threshold or hold time to correct.
The scenario comment is corrected and now says why the timer is absent. What
the power model does carry is the 45 mA threshold as a constant in
`tools/power-model/board-currents.yaml`, which is unchanged because it is a
datasheet fact.

### Left alone

- `docs/power/board-power.md` keeps the 32 s number where it describes the
  power-on default and the datasheet, because that is what the datasheet says.
- No firmware change. The cutoff is not something furble configures, and the
  PM-06 keep-on option stays unimplemented as before.

## Item 3 - Panel SLPIN and SLPOUT timing

### Source

The rule is per panel, and the corrected wording is the one PR 473 carries.
ILI9342C V101 sections 8.2.11 and 8.2.12 (pages 100 and 101) need 120 ms in
both directions. Sitronix ST7796S V1.0 sections 9.2.12 and 9.2.13 and ST7789V
sections 9.1.11 and 9.1.12 need 120 ms from Sleep Out to the next Sleep In
only. Both families want 5 ms after SLPIN and after SLPOUT before any other
command.

### What the tree does

`src/FurbleUI.cpp` already implements the rule in the display state machine:
`sleepDisplay()` refuses to enter SLPIN within `DISPLAY_SLEEP_DWELL_MS` (120) of
a wake, and `wakeDisplay()` waits out the remainder of a sleep before SLPOUT.
Both waits are furble's, because M5GFX 0.2.27 `Panel_LCD::setSleep(bool)` writes
the bare `SLPIN`/`SLPOUT` command with no delay
(`src/lgfx/v1/panel/Panel_LCD.cpp:101-107`). The two comment lines that stated
the rule backwards were aligned to PR 473's wording in this PR rather than
duplicated here; see the review round below.

### Why the simulator is not the place to assert it

The simulator does not model panel command timing, so there is nothing to
assert against:

- The simulator has no SPI or command layer for the panel. It renders through
  M5GFX 0.2.27 `Panel_sdl`, which derives from `Panel_FrameBufferBase`.
  `Panel_FrameBufferBase::setSleep(bool)` is an empty override
  (`src/lgfx/v1/panel/Panel_FrameBufferBase.hpp:46`), so `M5.Display.sleep()`
  and `M5.Display.wakeup()` store a backlight value and change nothing else.
- Consequently no SLPIN (`0x10`) or SLPOUT (`0x11`) is ever emitted, and there is
  no bus ordering or inter-command gap to timestamp.
- The production dwell does run inside the simulator, on `sim/clock.cpp`'s
  virtual clock. A scenario could read the two gaps back through new
  observability, but that would assert the guard against itself, not the
  panel. It would also be a simulator-only timing model with no hardware
  counterpart, which `sim/CLAUDE.md` forbids for policy.
- The ILI9342C is the panel where both directions matter, and the wake guard is
  what satisfies the Sleep In to Sleep Out half. On the ST panels that half is
  stricter than any datasheet requires, and no panel can object to it.

The reason is recorded in `docs/sim-coverage.md` under a new `## Not modelled`
section so a later reader does not assume the display scenarios cover it.

### Left alone

- No behaviour change to `src/FurbleUI.cpp` or `include/FurbleUI.h`. Only the
  two comments that stated the panel rule backwards changed, to PR 473's
  wording, so a merge with that branch is a no-op rather than a conflict.
- No new simulator scenario, query or assertion for panel timing, for the
  reasons above.

## Deliberately not changed

These need a bench measurement and are out of scope for a document-only
verification:

- MPU-6886 wake-on-motion threshold scale.
- LEDC backlight flicker.
- USB Serial/JTAG reset behaviour on the S3.

## Validation

No board is attached. Every result below is a host build.

| Command | Result |
| :--- | :--- |
| `cmake --build /tmp/furble-sim-align-host --target boundary_ethernet_test` | builds; run prints `eth.reset pulse_us=500 min=500 pin=9` and `ethernet_w5500_test: PASS` |
| same target with `W5500_RESET_LOW_US = 100` | fails: `the W5500 RESETn pulse is shorter than the datasheet 500 us` |
| same target with the original `reset_gpio_num = ETH_SPI_RST_GPIO` and no pulse | fails with 11 messages, including `the ESP-IDF W5500 PHY still owns the reset pin` and `the W5500 RESETn pulse did not complete before esp_eth_driver_install` |
| `ctest -R '^(boundary-|settings-default-policy)'` | 20 of 20 pass |
| `sh tools/run_sanitized_host_tests.sh '^(boundary-|settings-default-policy)'` | 20 of 20 pass under ASan and UBSan |
| `sh sim/scripts/run-boundary-conformance.sh` | 18 of 18 linked boundary cases pass, `eth.reset pulse_us=` token found |
| `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e waveshare-s3-eth` | succeeds against the pinned ESP-IDF 5.5.3, 46904 bytes RAM and 1529093 bytes flash |
| `furble-sim --script sim/scenarios/e2e/hw-pmic-ip5306-core.txt` | 25 assertions pass with the corrected comment block |
| `python3 tools/check_sim_scenarios.py`, `sh sim/scripts/check-doc-tokens.sh`, `python3 tools/check_sim_coverage.py` | pass |
| `python3 tools/pre_review.py --base origin/master` | all 10 gates pass |
| `clang-format -style=file` on every changed C or C++ file | clean |

The first `pio run` attempt failed to compile and caught a real defect that the
host shim could not: IDF types `gpio_num_t` as an enum, so `gpio_set_level()`
needs a `static_cast`. The shim declares it as `int`, which is why the host
build accepted the uncast version. The cast is in the committed code.

After the review round the gates were re-run at the amended head:
`ctest -R '^(boundary-|settings-default-policy)'` 20 of 20,
`tools/run_sanitized_host_tests.sh '^(boundary-|settings-default-policy)'` 20 of
20 under ASan and UBSan, `sim/scripts/run-boundary-conformance.sh` 18 of 18 with
every token found, `tools/check_sim_coverage.py` complete at 198 of 198 modeled
hardware entries, `sh sim/scripts/check-doc-tokens.sh` reports the tokens
present, `tools/pre_review.py --base origin/master` passes all 10 gates, and
`clang-format` is clean on the changed C and C++ files.

## Review round 1: documentation completeness and test hardening

An independent review of PR 482 returned CHANGES REQUIRED while CI was green.
The production pulse, its ordering and the 64 s IP5306 reading were verified and
survived mutation, so that behaviour is unchanged. This round closes the
document gaps, hardens the boundary test and handles the discarded GPIO
results.

### Blocking items

- **B1, README cutoff.** `README.md` still said the Core's IP5306 boost cuts the
  board below 45 mA "after 32 seconds". It now says 64 seconds, which is what
  M5Unified 0.2.20 writes at boot (`0x02 = 0x6C`, bits 3:2 = `11`); 32 s is only
  the power-on default of that field.
- **B2, L01 rows.** `docs/power/measurement.md` and
  `docs/power/measurement-plan.md` still said "Boost off 32 to 40 s after the
  load drops" in the L01 rows while the S25 rows in the same files said 64 s. An
  operator following L01 would have scored a correctly behaving Core as a
  failure. Both rows now read "64 to 72 s", which keeps the 8 s observation
  slack the original row carried above the datasheet value instead of
  rescaling it.
- **B3, W5500 register row.** `docs/power/datasheet-register.md` recorded the
  W5500 document as v1.0.6 with "the exact figure is not quoted because the copy
  was not opened", and section 12 warned against copying a number from a
  secondary document. The row now cites the opened copy: WIZnet W5500 datasheet
  version 1.1.0, dated 17DEC2022,
  `https://docs.wiznet.io/img/products/w5500/W5500_ds_v110e.pdf`, 1,893,745
  bytes, sha256 `7b826b808084ccd986bcc22904c00a07a508ef42fb93d079fe7150a4c4f1a63d`.
  It quotes section 1.1 page 10 pin 37 ("RESET should be held low at least
  500 us"), section 5.5.1 page 60 and figure 22 (`TRC` minimum 500 us, `TPL`
  maximum 1 ms), section 5.5.2 (10 us regulator wake), section 5.3 page 59
  (`IDD1` 132 mA, `IDD2` 13 mA in PHY power-down), section 5.4 page 60 (128, 75
  and 65 mA link currents) and the register pages in 3.1, 4.1 and `SHAR`.
  Section 12 item 3 reconciles v1.0.6 against v1.1.0: the corpus URL for v1.0.6
  is dead (HTTP 404), and v1.1.0's own history on pages 66 to 67 changes only
  the RPU minimum and the package text, so the reset figures are unchanged.
  `docs/power/board-power.md` carried the same "not opened" wording and is
  corrected with it, together with its NB-02 citation and evidence tag, its
  source-table row, its revision note and its URL list. `docs/power/summary.md`
  no longer lists the W5500 current among the documents that were not opened.
  - The row states the open item instead of claiming compliance: `TPL`, the RSTn
    release to internal PLOCK, is up to 1 ms, `resetW5500()` releases the line
    and returns, and `reset_gpio_num = -1` means ESP-IDF adds no wait either, so
    the first SPI transaction can run before the PLL locks. Section 12 item 9
    records it as a gap and the remaining gates carry it.

### Non-blocking items

- **N1.** The test header claimed that a production path handing the pin back to
  the PHY "fails these checks instead of passing a GPIO-number assertion". It
  fails a GPIO-number assertion, the `reset_gpio_num == -1` check in the PHY
  constructor. The header now separates the measured checks from that one.
- **N2.** `checkResetPrecedes()` accepted any completed pulse, so a pulse moved
  after the install passed on the previous init's edges. `beginInit()` snapshots
  the edge count before each `Furble::Ethernet::init()` and the check requires
  two fresh edges, a low then a high, plus a released line. Evidence below.
- **N3.** `resetW5500()` discarded `gpio_config()` and both `gpio_set_level()`
  results while every other `init()` step logs and bails. It now returns bool,
  logs each failure through the existing `logError()`, and `init()` returns
  false before the SPI bus is touched. The success path is the same pulse.
- **N4.** The constant's comment now says `eth_phy_config_t` does declare
  `hw_reset_assert_time_us` (`esp_eth_phy.h:276-283` in the pinned 5.5.3 copy)
  but only the 802.3-derived PHYs read it, and the W5500 PHY never does
  (`esp_eth_phy_w5500.c:152-164`), so `reset_gpio_num` is the only usable knob.
  The review quoted `esp_eth_phy.h:289-297` and `esp_eth_phy_w5500.c:369-373`,
  which match a newer IDF than the pinned 5.5.3, so the pinned copy is cited.
- **N5.** The doubled blank line before `## Checkers` in `docs/sim-coverage.md`
  is gone.
- **N6.** `src/FurbleUI.cpp` claimed the panel datasheets require 120 ms between
  Sleep In and Sleep Out. PR 473's rule is per panel: ILI9342C V101 8.2.11 and
  8.2.12 need 120 ms both ways, ST7789V 9.1.11 and 9.1.12 and ST7796S V1.0
  9.2.12 and 9.2.13 bound only Sleep Out to the next Sleep In, plus 5 ms after
  either command. The `wakeDisplay()` comment and the `DISPLAY_SLEEP_DWELL_MS`
  header comment now carry PR 473's wording verbatim, so a merge with that
  branch is a no-op. The panel rule text in this plan is corrected the same way.

### Mutation evidence

Every run is `boundary_ethernet_test` against the pinned host doubles, with the
mutation applied to `src/FurbleEthernet.cpp` or the `gpio_config()` double and
reverted afterwards.

| Mutation | Test version | Result |
| :--- | :--- | :--- |
| `resetW5500()` moved after `esp_eth_driver_install()` | test at `3dff7409` | 1 failure, first init only: `the W5500 RESETn pulse did not complete before esp_eth_driver_install`. The restarted transport passed on the previous init's two edges, which is the hole N2 names |
| same mutation | hardened test | 2 failures, one per init: `... before esp_eth_driver_install (edges 0 from a baseline of 0)` and `... before esp_eth_driver_install (edges 2 from a baseline of 2)` |
| `W5500_RESET_LOW_US = 100` | hardened test | `the W5500 RESETn pulse is shorter than the datasheet 500 us` and `the restarted transport did not pulse the W5500 RESETn for 500 us` |
| `gpio_config()` double returns `ESP_FAIL` | hardened test | logs `[E] ethernet: W5500 RESETn pin configuration failed: ESP_FAIL (0xffffffff)` and `init()` returns false before the SPI bus, so the discarded result cannot pass silently |

The per-finding record with its own evidence column is
[334-review-feedback.md](334-review-feedback.md), as `AGENTS.md` requires for a
review thread.

### Rebase reconciliation after #473

`fix/sim-hw-align` was rebased onto master after PR 473 merged, because both
branches rewrote the W5500 row in `docs/power/datasheet-register.md`.

- The four conflict hunks keep master's #473 text for everything it owns: the
  ST7796S row, the ILI9342C V101 and ILI9341 dwell pairs, the `ILI9342C V101`
  panel cells and the section 12 revision note. `src/FurbleUI.cpp` and
  `include/FurbleUI.h` are byte-identical to master after the rebase, so N6 is
  closed by #473 itself and this branch carries no second wording.
- The W5500 row keeps #473's facts, the PHY's hard-coded 100 us
  (`esp_eth/src/spi/w5500/esp_eth_phy_w5500.c:160`) and its citation in
  `plans/implemented/42-waveshare-eth-node.md`, and records that this branch
  removes that defect by driving the pulse in `resetW5500()` and passing
  `reset_gpio_num = -1`. The v1.1.0 size and sha256, the `TPL` open item and
  the current figures are this branch's.
- One #473 citation is corrected rather than kept: it placed `RSTn` pin 37 on
  page 9. In the opened copy the pin row and the "at least 500 us" note are on
  page 10, whose footer reads `10 / 67`, so the row cites page 10. The v1.0.6
  URL #473 linked redirects to HTTP 404, which the row now states.

## Remaining gates

- One scope capture of RESETn on a Waveshare ESP32-S3-ETH to confirm 500 us and
  the release edge, plus a link-up run afterwards.
- A timed Core discharge below 45 mA to confirm the 64 s cut, if anyone wants
  the number measured rather than read from the vendor sources.
- A logic-analyser capture of the SLPIN/SLPOUT gap to confirm the firmware
  dwell on the wire.
