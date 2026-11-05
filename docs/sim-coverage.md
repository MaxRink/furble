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

## Checkers

```sh
python3 tools/check_sim_coverage.py
python3 tools/check_sim_coverage.py --require-ready
python3 tools/check_sim_memory.py .pio/sim-memory/*.json
python3 tools/check_flash_readiness.py
python3 tools/check_flash_readiness.py --ready
```

`check_sim_coverage.py` validates the manifest structure and evidence. The
normal form passes when every modeled or approximate entry has a model,
scenario or runner, and at least one assertion that the scenario contains.
`--require-ready` fails on any approximate or not-modeled entry, any pending fuzz
target, any known failing seed, and any incomplete or failed permutation result.

`check_flash_readiness.py` prints the report and the exact blockers. `--ready`
returns non-zero while any blocker remains. It also refuses a dirty worktree.

## Permutations

`sim/coverage/permutations.json` expands eight memory boards against the
memory-critical axes: `web_ui`, `mqtt`, `mqtt_ha`, `companion`, `wifi`, `camera`,
`psram` and `mqtt_tls`. The current count is 6,912 combinations.

```sh
# deterministic pull-request smoke prefix
python3 sim/scripts/run-permutations.py --limit 64 \
  --results .pio/permutation-results.json
# full release run
python3 sim/scripts/run-permutations.py \
  --results sim/coverage/permutation_results.json
```

The release gate requires the full result file with one passing entry per
combination. A partial file is smoke evidence only.

## Fuzz contract

The fuzz harness is owned by the parallel `fuzz-suite` branch. This branch
consumes its `sim/coverage/fuzz_results.json` and does not duplicate the harness.
Every target record names its command, scenario, assertions and known failing
seeds. A failing seed must carry `fixed: true` and a regression scenario before
the gate can pass. The target list covers touch and physical-button layouts,
WebUI HTTP, companion BLE, MQTT, OTA, allocation failure, fragmentation, PSRAM
and NVS.

## Required commands for the flash report

```sh
python3 tools/check_sim_scenarios.py
python3 tools/check_sim_coverage.py --require-ready
sh sim/scripts/check-doc-tokens.sh
sh sim/scripts/check-hardware-quirks.sh
python3 sim/scripts/run-memory-profile.py
python3 sim/scripts/run-permutations.py \
  --results sim/coverage/permutation_results.json
sh sim/scripts/run-fuzz.sh
python3 tools/check_flash_readiness.py --ready \
  --write-report plans/evidence/flash-readiness.md
python3 tools/pre_review.py --base origin/master
```

Do not treat a passing subset as completion. The readiness report names every
blocker, including unmodeled quirks, pending fuzz results and unrun
permutations.
