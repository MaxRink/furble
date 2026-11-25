# Simulator coverage and flash gate

The simulator is the release gate for firmware that changes memory, radio
services, power or boot behavior. Hardware flashing waits for the gate.

## Coverage manifests

| File | Denominator |
| :--- | :--- |
| `sim/coverage/hardware_quirks.json` | Every supported board/profile and every hardware or memory quirk. |
| `sim/coverage/crashes.json` | Every known crash or boot loop, its reproduction scenario and assertions. |
| `sim/coverage/fuzz_targets.json` | Every fuzz target and its known failing seeds. |
| `sim/coverage/permutations.json` | Every memory-critical configuration combination. |
| `sim/coverage/memory_budget.json` | Golden memory floors and component budgets. |

Each hardware entry names a profile, category, feature, production source, model
source, proving scenario or runner, assertions and status. `modeled` means the
simulator executes the relevant production decision or a documented boundary
model. `approximate` means only a boundary or state model exists and the entry
names the missing evidence. `not-modeled` is allowed only with a reason and
missing evidence, and blocks the flash gate.

## Linked host boundaries

A boundary whose SDL model cannot reach the production driver is proved by a
host conformance target that compiles the same production source unchanged and
replaces only the lowest hardware API. The evidence runner is:

```sh
sh sim/scripts/run-boundary-conformance.sh
```

| Boundary | Production path | Host double |
| :--- | :--- | :--- |
| Headless UI | `src/main.cpp` with `FURBLE_NO_DISPLAY` | module doubles, no SDL/M5GFX |
| W5500 Ethernet | `src/FurbleEthernet.cpp` | SPI, GPIO, W5500 MAC/PHY, netif, event loop |
| IR RMT | `src/FurbleIR.cpp` | RMT TX channel, encoder and task API |
| SD writer | `src/FurbleSD.cpp`, `src/FurbleGPX.cpp` | SD/SPI/VFS mount over host POSIX I/O |
| Feedback | `src/FurbleFeedback.cpp` | M5 speaker/LED/vibration and LVGL timer |
| CompanionGatt | `src/FurbleCompanion.cpp` | MockNimBLE server/advertising and file-backed Preferences |
| TLS/socket | `src/FurbleWebUI.cpp` | precise allocator-instrumented TLS/socket layer |

The TLS/socket row models the exact allocation sizes and the start/stop path. It
does not claim a real mbedTLS handshake on the host. Physical SPI wiring, W5500
PHY behavior, DHCP on a physical LAN, analog audio, the SD card controller and
TLS on hardware remain hardware validation items even though the production
decisions are linked.

The W5500 row also times the RESETn pulse. `boundary_ethernet_test` records
every level change on the reset line against a virtual microsecond clock that
only the production `esp_rom_delay_us()` call advances, so it fails on a pulse
shorter than the 500 us W5500 datasheet minimum. It does not prove the device
waited, only that production asked the ROM for the delay.

## Not modelled

- **Panel command timing.** The simulator has no SPI or command layer for the
  panel, so `SLPIN` (0x10) and `SLPOUT` (0x11) are never issued and nothing
  timestamps the gap between them. M5GFX 0.2.27 renders through `Panel_sdl`,
  which derives from `Panel_FrameBufferBase`, and that class overrides
  `setSleep(bool)` with an empty body
  (`src/lgfx/v1/panel/Panel_FrameBufferBase.hpp:46`). `M5.Display.sleep()` and
  `M5.Display.wakeup()` therefore change no simulated hardware state beyond
  the backlight value. The production dwell in `src/FurbleUI.cpp` does run in
  the simulator on the virtual clock, but asserting it there would check the
  guard against itself, not against a panel. The one-way ST7796S 9.2.12 and
  9.2.13 rule (120 ms after SLPOUT before the next SLPIN, 5 ms after SLPOUT
  before any other command) stays a hardware item. See
  `plans/334-sim-hardware-alignment.md`.


## Checkers

```sh
python3 tools/check_sim_coverage.py
python3 tools/check_sim_coverage.py --require-ready
python3 tools/check_sim_memory.py .pio/sim-memory/*.json
python3 tools/check_flash_readiness.py
python3 tools/check_flash_readiness.py --ready
```

`check_sim_coverage.py` validates the manifest structure and evidence. A
scenario must contain a real `assert` step: `xassert`, comments and log strings
never count. A shell scenario must use the structured
`assert_token "token"` form, or a grep that explicitly reads the captured
runner output (`$LOG`/`$log`/`$output` or a result file). A modeled `runner`
entry must point at an executable script with `assert_token` markers.
`--require-ready` fails on any approximate
or not-modeled entry, any pending fuzz target, any known failing seed, and any
incomplete or failed permutation result.

`check_flash_readiness.py` prints the report and the exact blockers. `--ready`
returns non-zero while any blocker remains. It runs every manifest `runner` and
fails on a nonzero exit, so the linked boundaries are executed evidence. It
also refuses a dirty worktree.

The committed artifact in `sim/coverage/fuzz_results.json` passes: 11 targets,
145 seed runs, 124,148 iterations and zero failures. The committed manifests
list 6,912 permutations and 13 memory profile scenarios, and
`sh sim/scripts/run-boundary-conformance.sh` runs the 18 linked boundary ctest
cases. The
memory dumps are ignored generated evidence, so run
`python3 sim/scripts/run-memory-profile.py` before the final readiness check.
No fuzz seed is open. Hardware validation is deferred; #407 has not been
hardware-retested.

## Permutations

`sim/coverage/permutations.json` expands eight memory boards against the
memory-critical axes: `web_ui`, `mqtt`, `mqtt_ha`, `companion`, `wifi`, `camera`,
`psram` and `mqtt_tls`. The current count is 6,912 combinations.

```sh
# deterministic pull-request smoke prefix
python3 sim/scripts/run-permutations.py --limit 64 \
  --results .pio/permutation-results.json
# full release run (compact artifact, no per-result output tail)
python3 sim/scripts/run-permutations.py --compact \
  --results sim/coverage/permutation_results.json
```

The release gate requires the full result file with one passing entry per
combination. A partial file is smoke evidence only.

## Combined-tree merge order

`sim/coverage/merge_order.json` records the exact order the open production
branches must merge: `#394`, `#395`, `#396`, `#397`, `#398`, `#393`. Each row
maps the PR to its coverage entries. The readiness gate also checks the
combined-tree scenarios, so passing one PR alone is not evidence.

## Fuzz contract

`sim/scripts/run-fuzz-target.py` runs the targets named by
`sim/coverage/fuzz_targets.json` and writes `sim/coverage/fuzz_results.json`.
Use the full corpus before a hardware build:

```sh
python3 sim/scripts/run-fuzz-target.py --all
```

Each target record names its seeds, total iterations, failures, commands and
known failing seeds. The document's top-level `status` is `pass` only when every
target passed. A known failing seed must carry `fixed: true` and a regression
scenario before the gate can pass; none are currently open. Fixed regressions
that came from a production PR are listed in `fixed_regressions` with the file
and the regression command.

The target list covers touch and physical-button layouts, WebUI HTTP,
companion BLE, MQTT, OTA, allocation failure, fragmentation, PSRAM, NVS and GPS
UBX.
The #397 cover targets run real settings migration and WiFi fast-connect paths
plus the PSRAM memtest debug/release split. The #398 cover targets run the
MQTT start and HA discovery gates at below/at/above values, 1,536-byte
discovery fragmentation, single-buffer display behavior, and the companion
budget, 3 KiB when #398 added it and 3,840 bytes since plan 314 PR 4c. The
#407 cover targets run the 12 KiB main stack with a separate WebUI identity
task, the 9,392-byte MQTT event queue in PSRAM, the 5,120-byte camera snapshot
and the pre-task owner gate.

## Required commands for the flash report

```sh
python3 tools/check_sim_scenarios.py
python3 tools/check_sim_coverage.py --require-ready
sh sim/scripts/run-boundary-conformance.sh
sh sim/scripts/check-doc-tokens.sh
sh sim/scripts/check-hardware-quirks.sh
python3 sim/scripts/run-memory-profile.py
python3 sim/scripts/run-permutations.py --compact \
  --results sim/coverage/permutation_results.json
sh sim/scripts/run-fuzz.sh
python3 tools/check_flash_readiness.py --ready \
  --write-report plans/evidence/flash-readiness.md
python3 tools/pre_review.py --base origin/master
```

Do not treat a passing subset as completion. The readiness report names every
blocker, including unmodeled quirks, pending fuzz results and unrun
permutations.
