# 173 - Review feedback for the #283 and #289 follow-up

## Scope

This is the review-feedback record for plan 173's 2026-11-13 follow-up on
`fix/283-289-sim-scheduler`. That branch carries the environment growth
guard for issue #283, the SDL3 rpath for sanitized macOS builds, and the
issue #289 load proof. Every finding is recorded as fixed, already fixed or a
false positive, with evidence.

## Subagent review of `fix/283-289-sim-scheduler`

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Subagent review | The master `TZ` growth was described as a live race. `Timezone::apply` runs in `runSimulator()` before `panelReady.store(release)`, and until then the main thread only waits on `panelReady.load(acquire)`. No other thread walks `environ`: the M5GFX `dbg` thread is suppressed, and the watchdog reads `getenv` once in `watchdogStart()`. | Fixed. Verified in `sim/main.cpp`: `Timezone::apply` precedes `panelReady.store`, and the main thread sleeps on the flag before `Panel_sdl::loop()`. The probe's 24 main-thread calls all came from `SDL_Init`. The `sim/main.cpp` comment, plan 173, `docs/sim.md`, `plans/95`, the README #283 row and `sim/CLAUDE.md` now say the growth is ordered and was not a live race. `main()` still creates `TZ` first, so the guard can enforce the stricter rule: no growth after `SDL_Init`. The historical root cause, the pre-`a57e77e7` `RESTART_STEP` growth in the middle of the loop, is unchanged. Commit `feb22632`'s message still calls it a live race; this record supersedes it. | `sim/main.cpp`, `plans/173-sim-scheduler-visibility.md`, `docs/sim.md`, `plans/95-engineering-lessons.md`, `plans/README.md`, `sim/CLAUDE.md` |
| 2 | Subagent review | In `CONTRIBUTING.md` the new rpath and `run-env-order.sh` sentences split "`sim/lsan.supp` and `sim/ubsan.supp`." from "Those files list only...", so "Those files" referred to the wrong thing. One line also ran past 80 columns. | Fixed. The two sentences now follow "...each with its reason.", and the paragraph is rewrapped. | `CONTRIBUTING.md` |
| 3 | Subagent review | The guard comment said removing a variable can reallocate `environ`. glibc `unsetenv` shifts entries in place and never reallocates. | Fixed. The comment now says removal shifts the array in place without a reader lock, so a concurrent walk can miss an entry, and that it is rejected for that reason. | `sim/env_mutation_guard.c` |
| 4 | Subagent review | The guard did not hook `putenv` or `clearenv`, so "never grow the environment" was not fully enforced. | Fixed. `putenv` takes the name up to `=` and is checked like `setenv`, or like `unsetenv` when there is no `=`. `clearenv` is rejected after `SDL_Init` under any target. | `sim/env_mutation_guard.c` |
| 5 | Subagent review | Nothing proved the growth branch fires: the positive legs would also pass if it never rejected anything. | Fixed. `run-env-order.sh` compiles a small program that calls `SDL_Init(SDL_INIT_EVENTS)` and then adds `FURBLE_GUARD_SELFTEST`. The program must exit 86 with the exact guard message before any simulator leg runs. This runs in the Linux sim-e2e job. | `sim/scripts/run-env-order.sh` |
| 6 | Subagent review | `sim/CLAUDE.md` had a line of about 110 columns in the sanitizer bullet, and plan 173 had an overlong line in the `TZ` paragraph. | Fixed. The macOS SDL3 text is its own bullet, and both passages are rewrapped. | `sim/CLAUDE.md`, `plans/173-sim-scheduler-visibility.md` |
| 7 | Subagent review | Record every finding with its resolution and index the record. | Fixed. This file, indexed in `plans/README.md`. | `plans/173-review-feedback.md`, `plans/README.md` |
