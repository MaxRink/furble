# 313 - Sim-only closeout status

Status: current on master, 2026-11-06, baseline `0883a31e` (#407). No
implementation pull request is open. No hardware was flashed or measured for
this record.

## Mode

The repository is in sim-only mode. All simulator-doable work is complete,
every implementation PR is merged, and master is the baseline. Hardware
validation is
deferred. A simulator result, including a `READY TO FLASH` report, is not a
board measurement and does not authorize a support claim.

The last hardware flash, of the candidate before #407, reproduced the MQTT
main-stack overflow. #407 is the fix and has not been flashed or retested. No
upstream issue, comment, pull request or push was made for this closeout.

## Completed simulator work

- #394 reduced steady internal RAM. #395 added the heap-pressure reproductions
  and the retained crash record. #396 fixed the gate log literals. #397 cut
  boot cost and added the boot timeline. #398 added the MQTT owner and Home
  Assistant discovery gates. #399 added the sim-first release gate and the
  hardware and crash manifests.
- #401 added the full input, API and memory fuzz corpus. #402 removed the
  duplicate crash-log capture stub. #404 linked the production boundary paths
  against peripheral doubles. #406 recorded the full memory-critical
  permutation run.
- #407 moved the 9,392-byte MQTT event queue to PSRAM with an internal
  fallback, removed the early camera snapshot, created the queue only after the
  start gate, gated owner creation at 18,432 free / 12,288 largest (TLS
  21,504 / 12,288), and restored the StickS3 main stack to 12 KiB.
- The current MQTT budget is a 10 KiB owner, a 3 KiB plain worker, a 6 KiB TLS
  worker, 1,536-byte network and outbox buffers, a 9,392-byte queue, a
  8,192 / 4,096 plain start gate, a 11,264 / 7,168 TLS start gate and a
  10,240 / 4,096 discovery gate.
- The HTTPD budget is two client sockets plus three ESP-IDF-reserved sockets,
  five total. The constant counts clients only.
- The companion identity is persisted under the 15-character `companion_bleid`
  NVS key, not `companion_own_addr`. Apply and persist are transactional, and
  `companion reidentify` stops advertising before it rotates.
- MQTT accepts `mqtt://` and `mqtts://` only. The `wss://` branch is removed
  because no firmware build enables the WebSocket transport.

## Gate results on master

These results were produced at `0883a31e` on 2026-11-06.

| Check | Result |
| --- | --- |
| `check_sim_coverage.py --require-ready` | Pass |
| Hardware entries | 188 of 188 modeled, 0 approximate, 0 not modeled |
| Crash entries | 20 of 20 modeled |
| Fuzz targets | 10 of 10 modeled |
| Permutations | 6,912 of 6,912 passed |
| Fuzz corpus | 10 targets, 136 seed runs, 89,146 iterations, 0 failures; artifact commit `0883a31e` |
| Memory profile | 13 of 13 dumps pass |
| Linked boundary conformance | 18 of 18 pass |
| Host CTest | 183 of 183 pass |
| Pre-review | 8 of 8 gates pass |
| `check_flash_readiness.py --ready` | `READY TO FLASH` |
| Open implementation PRs | none (this docs audit is the only open PR) |

The memory dumps are generated, ignored evidence in `.pio/sim-memory`. A clean
checkout reports `memory profile dumps missing` until
`python3 sim/scripts/run-memory-profile.py` has run. The readiness result above
is after that run, not from any edit to a result file. The permutation artifact
is committed at `sim/coverage/permutation_results.json` and must not be
rewritten to change a result.

## MQTT memory, measured versus inferred

The 8 KiB main-stack candidate is the last hardware measurement:

| State | Free / largest | Basis |
| --- | ---: | --- |
| MQTT off, 8 KiB main stack | 30,007 / 28,660 | Measured on the StickS3 debug image |
| MQTT on, pre-fix | 5,199 / 4,596 | Measured, then `A stack overflow in task main` |
| MQTT off, 12 KiB main stack | 25,911 free | Inferred, 30,007 minus 4,096 |
| MQTT on, after owner gate | 15,671 free | Inferred, 25,911 minus 10,240 |
| MQTT on, before camera snapshot | 9,527 free | Inferred, minus 3,072 worker and 3,072 buffers |
| MQTT on, after camera snapshot | 4,407 free | Inferred; the simulator model reports 4,407 / 3,060 |
| HA discovery | skipped | The 10,240 / 4,096 gate closes; the broker link stays up |

The largest block after the 12 KiB revert is not measured on hardware. The
simulator value of 3,060 is modeled, not a board result.

## Hardware-deferred items

1. Flash #407 and read `perf heap` with `mqtt=false` and `mqtt=true`. Confirm
   the free and largest pairs, the main high-water mark and the owner gate.
2. Confirm the PSRAM queue is used, the internal fallback is not, and the
   camera list loads only on connect.
3. Confirm HA discovery either publishes the retained records or logs the
   10,240 / 4,096 gate and skips them without a reset.
4. Confirm the 12 KiB main stack, the 3 KiB companion stack and the 6 KiB TLS
   worker with the display, WiFi, companion and WebUI enabled.
5. Confirm the StickS3 WebUI memory gate, a real TLS handshake, a settings
   write and a soak. The StickS3 is not a supported WebUI target before that.
6. Measure Core2, `esp32-s3-headless` and Waveshare WebUI budgets. They remain
   inferred.
7. Confirm MQTT OTA throughput with the 1,536-byte buffers.
8. Close the physical boundaries: display wiring, analog audio, the SD
   controller and a real mbedTLS handshake.
9. Retest the long USB console run in issue #381 and the firmware-side
   WebUI tracking in issue #390.
10. Non-Fujifilm camera vendors remain untested on hardware. Only Fujifilm
    bodies are available.

## Criteria for resuming hardware

Hardware work resumes only when all of these hold:

1. The candidate commit is clean and built from `origin/master`.
2. `python3 sim/scripts/run-memory-profile.py` has generated 13 dumps and every
   gate command in [307-sim-first-flash-gate.md](307-sim-first-flash-gate.md)
   passes, ending in `READY TO FLASH`.
3. The firmware artifact, its SHA-256 and the exact commit are recorded in a
   flash-candidate report.
4. The StickS3 flash helper passes its PMIC preflight and restores the watchdog
   after the prepare step.
5. The hardware test plan in plan 307 and the MQTT checks in plan 311 have run:
   MQTT off and on, owner and start gates, HA discovery, main and companion
   high-water marks, WebUI gate and soak, OTA, USB recovery and the crash
   record.
6. Plans 304, 305, 308, 311 and this status record are updated with the
   measured pairs and the pass or fail result. Support docs change only after
   those measurements.

Until then, treat every post-#407 value as inferred or modeled, and do not
claim that a board run has validated the fix.
