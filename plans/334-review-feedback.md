# 334 - Review feedback for the simulator and production hardware alignment

## Scope

This is the review-feedback record for PR 482 (`fix/sim-hw-align`) and its plan
[334-sim-hardware-alignment.md](334-sim-hardware-alignment.md). An independent
review of head `3dff7409` returned CHANGES REQUIRED while CI was green. Three
findings blocked the merge and six were non-blocking. All nine are recorded here
as fixed, with the evidence that closed each. None was resolved without a
change, and no review thread claims a fix that is not in this branch.

The production pulse, its ordering and the 64 s IP5306 reading were checked
again and survived mutation, so `src/FurbleEthernet.cpp` behaviour is unchanged
except for the GPIO error handling the review asked for in finding 6.

## Findings

| # | Blocking | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Yes | `README.md:742-743` still said the M5Stack Core IP5306 boost cuts the board below 45 mA "after 32 seconds". The shipped firmware programs 64 s. | Fixed. The sentence now says 64 seconds. 32 s is the power-on default of `SYS_CTL2` bits 3:2; M5Unified 0.2.20 writes `0x02 = 0x6C`, bits 3:2 = `11`. | `README.md:743`, `docs/power/board-power.md` register block, plan 334 item 2 |
| 2 | Yes | The L01 rows said "Boost off 32 to 40 s after the load drops" while the S25 rows in the same files said 64 s, so an operator following L01 would score a correctly behaving Core as a failure. | Fixed in both files. The rows read "64 to 72 s", which keeps the 8 s observation slack the original row carried above the datasheet value instead of rescaling it. | `docs/power/measurement.md:655`, `docs/power/measurement-plan.md:525` |
| 3 | Yes | `docs/power/datasheet-register.md` was not updated for the W5500 reset figure: it recorded v1.0.6 as "not opened, the exact figure is not quoted", while this PR cites that datasheet for a production constant, and section 12 warned against copying a number from a secondary document. | Fixed from an opened copy. WIZnet W5500 datasheet version 1.1.0, dated 17DEC2022, live at `https://docs.wiznet.io/img/products/w5500/W5500_ds_v110e.pdf`, 1,893,745 bytes, sha256 `7b826b808084ccd986bcc22904c00a07a508ef42fb93d079fe7150a4c4f1a63d`. The row quotes section 1.1 page 10 pin 37, section 5.5.1 page 60 and figure 22 (`TRC` 500 us minimum, `TPL` 1 ms maximum), section 5.5.2, section 5.3 page 59, section 5.4 page 60 and the register pages. Section 12 reconciles v1.0.6 against v1.1.0. The open item is stated, not glossed: furble pulses 500 us and then talks SPI without waiting `TPL`, and `reset_gpio_num = -1` means ESP-IDF adds no wait either. | `docs/power/datasheet-register.md:61`, `:259-263`, `:286-293`, `:313`, `docs/power/board-power.md:1234-1240`, `:1400`, `:1570-1580`, `:1691-1692` |
| 4 | No | The test header claimed a production path that hands the pin back to the PHY "fails these checks instead of passing a GPIO-number assertion". The pin-ownership case is exactly a GPIO-number assertion. | Fixed. The header separates the measured checks from the `reset_gpio_num == -1` assertion and says why that number is the PHY's only input. | `tests/host/boundaries/ethernet_w5500_test.cpp:6-13` |
| 5 | No | `checkResetPrecedes()` accepted any completed pulse, so a pulse placed after the restart install passed on the first init's edges. | Fixed. `beginInit()` snapshots the edge count before each `Furble::Ethernet::init()`, and the check requires two fresh edges, a low then a high, plus a released line. Mutation results are in the table below. | `tests/host/boundaries/ethernet_w5500_test.cpp:64-67`, `:102-125`, `:344`, `:420` |
| 6 | No | `resetW5500()` discarded the `gpio_config()` and `gpio_set_level()` results while every other `init()` step logs and bails. | Fixed. It returns bool, logs each failure through the existing `logError()`, and `init()` returns false before the SPI bus. The success path issues the same `gpio_config`, low, 500 us, high sequence. | `src/FurbleEthernet.cpp:159-164`, `:356-391` |
| 7 | No | The next reviewer would propose `eth_phy_config_t::hw_reset_assert_time_us` as the fix. The review cited `esp_eth_phy.h:289-297` and `esp_eth_phy_w5500.c:369-373` for it. | Fixed with the pinned line numbers. `eth_phy_config_t` declares the field at `esp_eth_phy.h:276-283`; only the 802.3-derived PHYs read it (`esp_eth_phy_802_3.c:469`, `:682-685`, and the generic, IP101, RTL8201 and LAN87xx constructors), and `w5500_reset_hw()` never does (`esp_eth_phy_w5500.c:152-164`). The review's line numbers match a newer IDF than the pinned 5.5.3, so the pinned copy is what the comment cites. | `src/FurbleEthernet.cpp:139-146`, `~/.platformio/packages/framework-espidf/version.txt` (`5.5.3`) |
| 8 | No | `docs/sim-coverage.md` had a doubled blank line before `## Checkers`. | Fixed. One blank line remains. | `docs/sim-coverage.md:71-73` |
| 9 | No | `src/FurbleUI.cpp:1887` said the panel datasheets require 120 ms between Sleep In and Sleep Out, which is the backwards form of a per-panel rule that PR 473 corrects. | Fixed by aligning to PR 473's wording instead of restating the rule: ILI9342C V101 8.2.11 and 8.2.12 need 120 ms both ways, ST7789V 9.1.11 and 9.1.12 and ST7796S V1.0 9.2.12 and 9.2.13 bound only Sleep Out to the next Sleep In, plus 5 ms after either command. The `wakeDisplay()` comment and the `DISPLAY_SLEEP_DWELL_MS` header comment now match PR 473 verbatim, so a merge with that branch is a no-op. | `src/FurbleUI.cpp:1887-1893`, `include/FurbleUI.h:1135`, PR 473 diff for the same two hunks |

## Mutation evidence for finding 5

Every run is `boundary_ethernet_test` against the pinned host doubles.

| Mutation | Test version | Result |
| :--- | :--- | :--- |
| `resetW5500()` moved after `esp_eth_driver_install()` | `3dff7409` test | 1 failure, first init only: `the W5500 RESETn pulse did not complete before esp_eth_driver_install`. The restarted transport passed on the previous init's two edges, which is the hole the review named. |
| same mutation | hardened test | 2 failures, one per init: `... before esp_eth_driver_install (edges 0 from a baseline of 0)` and `... before esp_eth_driver_install (edges 2 from a baseline of 2)` |
| `W5500_RESET_LOW_US = 100` | hardened test | `the W5500 RESETn pulse is shorter than the datasheet 500 us` and `the restarted transport did not pulse the W5500 RESETn for 500 us` |
| `gpio_config()` double returns `ESP_FAIL` | hardened test | logs `[E] ethernet: W5500 RESETn pin configuration failed: ESP_FAIL (0xffffffff)`, and `init()` returns false before the SPI bus, so the error path cannot pass silently |

## Validation

`ctest -R '^(boundary-|settings-default-policy)'` passes 20 of 20.
`tools/run_sanitized_host_tests.sh '^(boundary-|settings-default-policy)'`
passes the same 20 under ASan and UBSan. `sim/scripts/run-boundary-conformance.sh`
passes 18 of 18 linked boundary cases and finds every production token.
`python3 tools/check_sim_coverage.py` reports 198 of 198 modeled hardware
entries, `sh sim/scripts/check-doc-tokens.sh` reports the tokens present, and
`python3 tools/pre_review.py --base origin/master` passes all 10 gates. No board
is attached, so the pulse width and the 64 s cutoff remain datasheet and library
reads rather than measurements.
