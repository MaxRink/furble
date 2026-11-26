# 336 - Review feedback for the GPS UART pin seam

## Scope

This is the review-feedback record for #510 (`feat/gps-profile-pins`), which
moves the GPS UART pins behind `Board::gpsPins()` and closes the UART on the
M5Stack Core and the Waveshare ESP32-S3-ETH. Every finding is recorded as
fixed, already fixed or a false positive, with evidence. Line references are
positions in the tree this PR produces on master.

## PR #510

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | The Waveshare ESP32-S3-ETH row now names `GpsPinSource::NONE`, so furble installs no GPS UART on that board, but `docs/power/board-power.md` still described it as "GPS only through the Pico header". That is the one board-facing doc the change did not update, and it leaves a reader believing the receiver still has a port. | Fixed. The row now says the profile names no GPS UART, that GPIO13 is the W5500 SPI clock, and that a receiver on the Pico header therefore has no port. | `docs/power/board-power.md`, the `waveshare-s3-eth` rails row. |
| 2 | `docs/power/board-power.md` still attributed the Port A pin lookup to `src/FurbleGPS.cpp:168-169` and the UART setup to `:160-180`. This change deletes that lookup from `src/FurbleGPS.cpp` and inserts the pin seam above the setup block, so both citations were wrong on two axes: wrong file and wrong lines. | Fixed. The paragraph now names `Board::gpsPins()` and `src/FurbleBoard.cpp:180-181`, and the setup block is cited at `:188-215`. | `grep -c 'M5.getPin' src/FurbleGPS.cpp` is 0 after this change; the two calls are at `src/FurbleBoard.cpp:180-181`. |
| 3 | Eight documentation citations the rebase rotated: `docs/components/at6668.md`, `docs/components/board-map.md`, `plans/00-hardware-experiments.md`, `plans/50-companion-app-design.md`, `plans/implemented/14-gps-pcas.md` (two), `plans/implemented/15-gps-power.md`, `plans/implemented/21-imu-dead-reckoning.md`. The branch's own rotation commit was correct against its base; master's #505 and this rebase moved the symbols again. | Fixed, all eight re-pointed to where the symbol now sits. | `python3 tools/check_doc_citations.py` reports `0 errors of 2871 citations in 157 files`. |
| 4 | **#509 merged during the review and guards the same function.** It returns early when `uart_driver_install()` fails and deliberately leaves `m_Enabled` to the stored setting, and its scenario `gps-uart-install-failure.txt` asserts `gps.enabled 1` with `gps.receiver absent`. This branch returns early on the same `m_Task == NULL` for a board with no port and clears `m_Enabled`, and its scenario asserts `gps.enabled 0`. A naive merge would have made the two paths indistinguishable and broken one of the two scenarios. | Fixed. One guard on the missing task distinguishes the causes: `m_UartInstallFailed` keeps the stored setting and the receiver absent, and a `NONE` row clears `m_Enabled` as well. `getInstance()` keeps #509's failure flag in its condition, so neither path retries. | `src/FurbleGPS.cpp`, `GPS::enable()`. Both `waveshare-no-gps-uart.txt` (`gps.enabled 0`) and `e2e/gps-uart-install-failure.txt` (`gps.enabled 1`) pass on the rebased tree. |

## Checked and not findings

The four questions a reviewer has to answer for this change, plus the ones that
looked suspicious and are not.

- **Can a board that should have GPS lose it?** No. Six rows keep
  `BOARD_SUPPORT` (StickC, StickC Plus, StickC Plus2, StickS3, Core2, Tough)
  and the unknown row keeps it too, so every pin lookup resolves exactly as it
  did before. M5Unified 0.2.20 `_pin_table_i2c_ex_in` (`M5Unified.cpp`)
  gives StickS3 10/9 from its own row and 33/32 for the ESP32 boards that fall
  through to `board_unknown` (Core2, Tough, the StickC family), which is what
  the plan's table claims. `BOARD_SUPPORT` also returns true when both pins are
  -1, so an unknown board still reaches `uart_set_pin` with -1 as it did. A
  positive control on the rebased head: `gps-no-receiver.txt` on the StickS3
  panel exits 0 and asserts `ui.gps_source = uart`, so the UART is still
  installed where a GPS unit can be wired.
- **Is the absence sticky in a way that stops a later retry working?** On a
  `NONE` row it is permanent by design: `m_Task` is never created, and
  `enable()` and `disable()` return early with the state held at `ABSENT`. On
  every other row nothing changes: the early returns are keyed on `m_Task ==
  NULL`, which only a `NONE` row reaches. A stored `gps true` cannot make a
  receiver appear, which is the intended fail-closed behaviour.
- **Does any caller read `isEnabled()` in a way that changes UI or console
  behaviour?** Three, and the change only touched the surfaces:
  `gpsStatus()` now prints `enabled: false` next to `receiver: absent`, which
  is the truth on a board with no port; `FurbleTimezoneGPS` declines a GPS time
  source that cannot produce a fix instead of waiting for one; `updateGpsIcon()`
  keeps the header icon hidden because no receiver runs. The three *surfaces*
  the change moved to `gpsEnabledForUI()` (the settings rows, the Connected
  page GPS Data entry and the sub-pages) are the ones that must stay reachable
  so the receiver state can be read at all.
- **Does the Waveshare really avoid the GPIO13 collision with the W5500?**
  Yes. `ETH_SPI_SCLK_GPIO = 13` (`src/FurbleEthernet.cpp:132`, applied at
  `:169`) and M5Unified's `board_M5StampS3` row gives `EX SCL = 15`,
  `EX SDA = 13` (`M5Unified.cpp:93`), which is `port_a_pin1`/`port_a_pin2`.
  The old code therefore put UART TX on GPIO13. With `NONE` no pin is claimed
  at all, so nothing rewrites the W5500 clock.
- **The Core's Port A really is its internal I2C bus.** M5Unified's
  `board_M5Stack` row gives 22/21 for both the internal and the external pair
  (`M5Unified.cpp:139`), and `_setup_pinmap` collapses them onto one port
  when the pairs are equal (`M5Unified.cpp:1868-1871`). The old lookup put
  UART RX on G22 and TX on G21, the bus the IP5306 (0x75) and MPU6886 (0x68)
  share.
- **The negative scenario proves the seed does the work.** Same scenario with
  the `seed board stamps3` line removed:
  `ASSERT FAILED: gps.enabled expected '0' got '1'`, exit 1. With the seed:
  `furble: No GPS UART on this board.`, `assert ok: gps.enabled = 0`,
  `assert ok: gps.receiver = absent`, exit 0. The suppression comes from the
  selected row, not from the run.
- **The `board` seed is applied before anything reads the row.**
  `Sim::applyScenarioSettings()` runs before `Platform::init()` in
  `sim/main.cpp:77-83`, which is what the plan claims and what the platform and
  the GPS task need.
- **`m_NoUartLogged` is a plain `bool`.** Two concurrent first calls to
  `getInstance()` on a `NONE` row could both log. It is not reachable in
  practice: the first call comes from `GPS::init()`, which runs on the boot task
  before the console, UI and MQTT tasks exist. Left as is rather than widening
  the change with an atomic for a log guard.
- **`GPS::reset()` would touch a NULL queue on a `NONE` row.** It has no caller
  in the tree, so the early returns above are sufficient. Noted so a future
  caller does not reach it through the console.
- **The u-blox write paths are reachable with no driver.** `sendCommand()`,
  `sendRaw()` and `sendBinary()` can be reached from the console. ESP-IDF
  guards every `uart_*` entry point with a `p_uart_obj[uart_num]` check, and the
  simulator's fake UART has no driver state to dereference, so the call fails
  instead of crashing.
- **The row shape reconciles with #505 and with #500, which both merged.** #505
  added `ledActiveLow` and `irMarkLow` after `gpsRail`; #500 added the
  `powerOffPath` column and its own `WAVESHARE` row and
  `case board_M5StampS3`. The rebase keeps every one of those and appends the
  three GPS columns at the end, so there is exactly one row per board, one
  `case` label, and #500's `WAVESHARE` row now carries
  `GpsPinSource::NONE`. No `STAMPS3` row of this branch survives.
- **No em-dashes were added.** `CONTRIBUTING.md` forbids them; `tools/
  pre_review.py` passes.

## Gates

| Gate | Command | Result |
| :--- | :--- | :--- |
| Host suite | `ctest --test-dir /tmp/rv510-build --output-on-failure -j 6` | `100% tests passed out of 211` |
| Pre-review | `python3 tools/pre_review.py --base origin/master` | 13 gates |
| Doc tokens | `sh sim/scripts/check-doc-tokens.sh` | tokens present |
| Scenario manifest | `python3 tools/check_sim_scenarios.py` | complete |
| Doc citations | `python3 tools/check_doc_citations.py` | 0 errors |
| Format | `python3 tools/check_format.py --base origin/master` | 12 changed files formatted |
| Simulator | `sh sim/build.sh` on the branch tree, `waveshare-no-gps-uart` and `gps-no-receiver` by `--script` | both pass; the seed-removed copy fails as recorded above |
| Firmware | `build (m5stack-core)`, `build (waveshare-s3-eth)` and the matrix in CI | CI is the authority |
