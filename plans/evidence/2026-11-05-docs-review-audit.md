# Documentation review audit, 2026-11-05

> Historical record. Later merges changed both the memory values and the gate
> state: #406 recorded 6,912 of 6,912 permutations and #407 moved the MQTT
> queue to PSRAM and restored the 12 KiB StickS3 main stack. The 34,631-byte
> inference in this record is superseded by the 25,911-byte inference in plan
> 311. Current status: [313-sim-only-status.md](../313-sim-only-status.md).

This note records each documentation, plan and contract finding from the merged
PR review and where the remediation branch fixed it. Every row was checked
against the current tree, not against an older plan summary.

| Finding | Fix |
| :--- | :--- |
| PR 391: BLE debug-status bench workflow was only in README and `docs/companion.md`. | Added `docs/wiki/BLE-Debug.md`; linked it from `docs/wiki/Home.md` and `docs/wiki/README.md`; linked it from `docs/wiki/Console-Commands.md`. |
| PR 391: plan 304 still said no board was measured while the debug service was validated. | Reconciled the plan 304 baseline paragraph with the later #391 validation and #392 fail-closed measurement. |
| PR 391: `companion reidentify` was missing from the README serial-console list and the wiki console page. | Added it to `README.md` and synced `docs/wiki/Console-Commands.md` with `docs/console-commands.md`. |
| PR 392: changed WebUI and Companion ownership contracts were not recorded. | Added the WebUI memory gate, identity ordering, cached reload, companion auth callback order, reidentify and `CompanionDebug` contracts to `src/CLAUDE.md`, `include/CLAUDE.md` and `tests/host/CLAUDE.md`. Also corrected the stale Service Changed comment in `include/FurbleCompanion.h`. |
| PR 396: no plan note for the gate-log literal fix. | Added the #396 note to `plans/306-sim-memory-crash-repro.md`. |
| PR 397: boot timing docs lacked the full mark list and splash mark semantics. | Added the mark table and splash notes to `docs/boot-timing.md`; the notes match the code branch's `BOOT_SPLASH_SHOWN` and direct cycle-counter fixes. Synced `docs/wiki/Console-Commands.md` with the in-repo console reference and added the `boot.splash_shown` simulator query to `docs/sim.md`. |
| PR 398: README did not describe cached `mqtt_ha`, the MQTT/discovery floors or secure MQTT. | Added the MQTT settings and fail-closed paragraph to `README.md`, including the plain 8,192/4,096 and TLS 11,264/7,168 start floors. WebSocket MQTT is not documented because #405 removed the dead branch and the contract is `mqtt://`/`mqtts://` only. |
| PR 398: README and supported-hardware pages still used the stale 10.6 KiB WebUI number. | Updated `README.md`, `docs/supported-hardware.md`, `docs/webui.md` and `docs/wiki/WebUI.md` to the 27,351/25,588 measured pair and the inferred 34,631 free result, with StickS3 still unsupported until hardware confirms the gate, handshake, settings write and soak. |
| PR 399: contributor docs lacked the hardware coverage, memory profile, permutation and readiness commands. | Added the command sequence and gate semantics to `CONTRIBUTING.md`, `CLAUDE.md` and `AGENTS.md`. |
| PR 399: crash-record size was 434 in the memory profiler doc. | Corrected `docs/sim-memory-profiler.md` to 436 bytes. |
| PR 399: sim, plan 307 and coverage docs disagreed on the gate state. | Updated `docs/sim.md`, `plans/307-sim-first-flash-gate.md` and `docs/sim-coverage.md` to the passing fuzz corpus and the remaining approximate, memory-dump and permutation blockers. |
| PR 400: retained crash lines were not documented as ANSI-normalized. | Updated `docs/crash-diagnostics.md`, `docs/console-commands.md` and `docs/wiki/Console-Commands.md`; added the #400 note to `plans/306-sim-memory-crash-repro.md`. |
| PR 401: contributor docs still said the MQTT owner stack and HA discovery gate were red. | Replaced the stale text in `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `docs/sim.md`, `tests/host/README.md` and `tests/fuzz/README.md` with the passing corpus and fixed-regression state. |
| PR 402: no plan update for the duplicate crash-log capture stub. | Added the #402 follow-up to `plans/99-boot-time.md`, which records the original host link change. |
| Plan numbering: plans 308 and 309 had headings for the previous number, and plans 305/docs/webui referenced 307 for the 8 KiB MQTT memory pass. | Corrected the headings and the follow-up references in `plans/305-general-internal-ram.md`, `plans/308-mqtt-ha-memory.md`, `plans/309-input-api-memory-fuzzing.md` and `docs/webui.md`. |
| Review policy: a resolved thread was treated as a fix. | Added the zero-unresolved-thread merge gate, with fixed/already-fixed/false-positive evidence, to `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md` and the review section of `plans/implemented/298-pre-review-gate.md`. The rule also covers this remediation PR. |

## Resolved code findings

The code-side follow-ups originally listed here were fixed by #405 and are
recorded in [plan 310](../310-review-feedback.md): the `wss://` branch is
removed, the HTTPS socket contract is two clients plus three ESP-IDF-reserved
sockets (five total), `companion reidentify` persists `companion_bleid`, and the
stale fuzz `known_findings` are closed. The boot-timing docs now match the
#397/#405 `BOOT_SPLASH_SHOWN` and cycle-counter behavior.

## Verification

- `python3 tools/pre_review.py --base origin/master`
- `sh sim/scripts/check-doc-tokens.sh`
- `python3 tools/check_review_threads.py --repo MaxRink/furble --pr <number>`
- `python3 tools/check_flash_readiness.py --ready`
- At the time of this audit, the readiness gate remained blocked only by the
  missing memory profile dumps and the full permutation run. #406 and #407
  closed both items; see the banner above and plan 313.
