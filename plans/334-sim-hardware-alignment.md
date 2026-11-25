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

WIZnet W5500 datasheet v1.1.0 section 5.5.1, figure 22 and table 2: RESETn must
be held low for at least 500 us.

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

- No settling delay after the pulse is released. The W5500 datasheet timing
  table is the only source in hand, ESP-IDF adds none after its own pulse, and
  a value invented here would be unverified. The MAC init over SPI already runs
  after the pulse.
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
  rows), `docs/power/datasheet-register.md`, `docs/power/ble.md`,
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

Sitronix ST7796S V1.0 section 9.2.12 and 9.2.13, the same rule in the ST7789 and
ILI934x families: 120 ms after SLPOUT before the next SLPIN, and 5 ms after
SLPOUT before any other command.

### What the tree does

`src/FurbleUI.cpp` already implements the rule in the display state machine:
`sleepDisplay()` refuses to enter SLPIN within `DISPLAY_SLEEP_DWELL_MS` (120) of
a wake, and `wakeDisplay()` waits out the remainder of a sleep before SLPOUT.
The header comment for `DISPLAY_SLEEP_DWELL_MS` was corrected on a separate
branch, so this change does not touch it.

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
- The one-way rule is stricter than the firmware in one direction only: the
  guard also waits 120 ms after SLPIN before SLPOUT, which no datasheet
  requires and no panel can object to.

The reason is recorded in `docs/sim-coverage.md` under a new `## Not modelled`
section so a later reader does not assume the display scenarios cover it.

### Left alone

- No change to `src/FurbleUI.cpp` or `include/FurbleUI.h`. The implementation is
  correct and its comments belong to the other change.
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

## Remaining gates

- One scope capture of RESETn on a Waveshare ESP32-S3-ETH to confirm 500 us and
  the release edge, plus a link-up run afterwards.
- A timed Core discharge below 45 mA to confirm the 64 s cut, if anyone wants
  the number measured rather than read from the vendor sources.
- A logic-analyser capture of the SLPIN/SLPOUT gap to confirm the firmware
  dwell on the wire.
