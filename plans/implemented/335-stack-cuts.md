# 335 - The control stack cut is not supported, and the walk now sees why

Implementation state (2026-11-26): the `control` stack stays at 8,192 bytes on
every env. The cut to 5,120 that an earlier revision of this branch took is
withdrawn: the walk it rested on did not follow the task's main job, which is
connecting a camera through the pure virtual `Camera::_connect()`. Resolving
that dispatch in `tools/stack_budget.json` raises the control row from 3,184 to
4,720 bytes on the ESP32 release builds and from 4,128 to 6,080 on
`m5stick-c-debug`, which is 960 bytes over the 5,120 the cut created. The
manifest now resolves the dispatch and records the NimBLE pairing restart cycle
as a known limit, so the gate measures the path instead of dropping it.

The `main` cut from 16,384 to 12,288 remains not taken, for the reason the
branch already recorded: the only hardware reading leaves 24.58 percent.

## Goal

The memory audit (`docs/stack-budget.md`, `plans/314-memory-resilience.md`)
lists a resolved walk and one hardware reading per task. `control` carried the
largest unsupported margin: a 8,192-byte stack against a walk of 2,704 to 4,240
across the thirteen envs and about 820 bytes used in the one reading. The cut was supposed to turn most
of that margin into boot heap. It cannot, because the walk was not measuring the
camera connect path.

## Why the cut is withdrawn

`Control::connectAll()` calls `Camera::connect()`, a non-virtual wrapper
(`lib/furble/Camera.cpp:531`), which calls the pure virtual `_connect()`
(`lib/furble/Camera.cpp:608`). Every vendor camera implements it:
`FujifilmBasic`, `FujifilmSecure`, `Sony`, `Nikon`, `CanonEOSRemote`,
`CanonEOSSmart`, `DJIOsmo`, `Lumix`, `Ricoh` and `FauxNY`. GCC writes an
indirect call at that site, and `tools/stack_budget.json` had no rule for it,
so the walk stopped at the wrapper and followed none of the ten
implementations.

Resolving the dispatch to `::_connect()` adds the deepest vendor path. On
`m5stick-c` it is 4,432 frames plus the 288-byte ESP32 port overhead:

```
  32  control_task
  48  Control::task
 160  Control::connectAll
  80  Camera::connect
 544  FujifilmSecure::_connect
  48  Fujifilm::subscribe
  64  Camera::gattSubscribe
  48  NimBLERemoteCharacteristic::subscribe
  80  NimBLERemoteCharacteristic::setNotify
  64  NimBLERemoteValueAttribute::writeValue
  80  NimBLEClient::secureConnection
  32  NimBLEDevice::startSecurity
 176  ble_gap_security_initiate
  96  ble_sm_enc_initiate
 176  ble_sm_process_result
  96  ble_store_persist_peer_records
 208  get_nvs_db_attribute
 ...  NVS page compare, free, assert, abort
1216  panic handler
```

Every edge is a direct call in the image except the one the new manifest rule
resolves. The tail is the panic handler, which every abort path pays
(`multi_heap_free_impl` asserts the block it is freeing).

The rule resolves all four indirect sites inside `Camera::connect`, not only
the `_connect()` one: the other three are `NimBLEClient` virtuals
(`setSelfDelete`, `setSelfDelete`, and the failure teardown). That
over-approximates, which is the safe direction, and none of those three adds
depth beyond `_connect()`.

## What the resolved walk needs

`python3 tools/stack_budget.py --env <env>` on the thirteen builds of this
branch (seven release envs and six `-debug` envs), control row, port overhead
included. The column `as pulled` is the manifest this branch was opened with,
`resolved` is the manifest this branch now carries.

| Env | as pulled | resolved | Stack | Free after | Headroom |
| :--- | ---: | ---: | ---: | ---: | ---: |
| m5stick-c | 3,184 | 4,720 | 8,192 | 3,472 | 42.4% |
| m5stick-c-plus | 3,184 | 4,720 | 8,192 | 3,472 | 42.4% |
| m5stack-core | 3,184 | 4,720 | 8,192 | 3,472 | 42.4% |
| m5stack-core2 | 3,184 | 4,720 | 8,192 | 3,472 | 42.4% |
| m5stick-s3 | 2,704 | 4,336 | 8,192 | 3,856 | 47.1% |
| waveshare-s3-eth | 2,704 | 4,336 | 8,192 | 3,856 | 47.1% |
| esp32-s3-headless | 3,968 | 5,648 | 8,192 | 2,544 | 31.1% |
| m5stick-c-debug | 4,128 | 6,080 | 8,192 | 2,112 | 25.8% |
| m5stick-c-plus-debug | 4,128 | 6,080 | 8,192 | 2,112 | 25.8% |
| m5stack-core-debug | 3,568 | 5,248 | 8,192 | 2,944 | 35.9% |
| m5stack-core2-debug | 4,128 | 6,080 | 8,192 | 2,112 | 25.8% |
| m5stick-s3-debug | 3,824 | 5,520 | 8,192 | 2,672 | 32.6% |
| waveshare-s3-eth-debug | 4,240 | 5,936 | 8,192 | 2,256 | 27.5% |

Every env passes on 8,192 with the resolved manifest, and the smallest headroom
is 2,112 bytes, 25.8 percent. The tightest rows are the debug builds, not the
release ones: the protected build adds frames to the camera, NVS and log paths
the release build compiles down.

One `as pulled` cell differs from the number the branch was opened with:
`m5stack-core-debug` walks 3,568, not 3,984. #518 changed that lane to
`LOG_LOCAL_LEVEL=ESP_LOG_INFO`, which compiles most of the verbose log frames
out of the protected build. Every other release and `-debug` row reproduces
the published figure.

## The 25 percent line

`main` stays at 16,384 because the only hardware reading, the StickS3 in #398,
leaves 3,020 bytes free of 12,288, 24.58 percent, under the 25 percent a cut in
this audit needs. The earlier revision of this branch waived the same line for
`control` (22.5 percent on headless, five of six `-debug` rows between 17.2 and
22.2 percent) on the argument that the line was set for a walk that is a lower
bound, while `control` was exact. That argument does not hold: `control`'s walk
is a lower bound too, by 1,536 bytes on the ESP32 release builds and by 1,952
on `m5stick-c-debug`, and the resolved rows clear the line on 8,192 without an
exception. There is no exception left to record.

## The NimBLE pairing restart cycle

Following the vendor connect path reaches
`ble_sm_pair_initiate -> ble_sm_process_result -> ble_sm_pair_initiate`
(`~/.platformio/packages/framework-espidf/components/bt/host/nimble/nimble/nimble/host/src/ble_sm.c:1023-1093`
and `:2869-2911`). The re-entry is the `MYNEWT_VAL(BLE_RESTART_PAIR)` branch,
which is 1 in the ESP-IDF port
(`~/.platformio/packages/framework-espidf/components/bt/host/nimble/port/include/esp_nimble_cfg.h:2338`);
it
re-initiates the procedure when `ble_sm_process_result` returns app status 518.
Each pass frees the procedure it just allocated, so the walk charges one pass
and the manifest records the retry as a known limit. `control` therefore
reports a lower bound on that cycle, the same way `main` records LVGL's widget
walk.

## What the branch now changes

| File | Change |
| :--- | :--- |
| `tools/stack_budget.json` | `control` stays 8,192. Adds the `Camera::connect` to `::_connect()` indirect rule and the `ble_sm_pair_initiate` known-limit recursion entry. |
| `docs/stack-budget.md` | The control row's resolved walk, the Waveshare row correction from #461, and the two known limits. |
| `plans/314-memory-resilience.md` | Follow-up section: the cut was withdrawn, the `control` bullet returns to the hardware-gated list. |
| `plans/implemented/335-review-feedback.md` | The independent review, its findings and their evidence. |
| `plans/README.md` | The two plan rows. |

No task stack changes. No static RAM or heap change, so
`sim/coverage/board_heap.json` and the `profile-*` floors are untouched and
keep the values master derives.

## Commands and results

Every build is `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e <env>`, thirteen of
them, on this branch rebased onto `origin/master` (4bf0b97d).

| Command | Result |
| :--- | :--- |
| `python3 tools/stack_budget.py --env <env>` on the seven release envs, patched manifest | all PASS, table above |
| `python3 tools/stack_budget.py --env <env>-debug` on the six `-debug` envs, patched manifest | all PASS, table above |
| `python3 tools/stack_budget.py --env <env>` on the same builds, manifest as pulled | all PASS; reproduces every published row except `m5stack-core-debug` 3,568 against 3,984, see above |
| `python3 -m unittest tests.test_stack_budget` | OK, 29 tests |
| `python3 tools/pre_review.py --base origin/master` | PASS, 12 gates |

## Not verified

- No `perf tasks` reading on a cut stack, because there is no cut. The about
  820 bytes used on 8,192 is unchanged and remains the hardware evidence.
- The resolved `control` row is still a lower bound: 201 unresolved indirect
  sites remain reachable with the rule in place, so the walk is not a proof of
  fit. The hardware high-water mark stays the evidence for this task, as it is
  for `main`.
- The NimBLE pairing restart cycle is charged one pass. A pathological retry
  loop would cost more; the vendor code frees and re-allocates per pass, and no
  reading covers it.
