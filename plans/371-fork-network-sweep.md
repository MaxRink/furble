# 371 Fork network sweep

State: research complete, action items dispatched.

Baseline: fork master `1d666b43`, upstream `gkoh/furble` tip `0cac22aa`
(checked 2026-11-26). The sweep covers the whole fork network, which plan
`implemented/303-upstream-sync.md` does not: 303 audits upstream master, upstream
unmerged issue branches and the Lumix PR, but no fork branch.

## Why sweep the forks at all

Upstream has 41 forks. Twenty-five are byte mirrors of gkoh master and carry no
work. The remaining fifteen hold 123 commits that are absent from our master by
SHA. Of those, 54 already exist in our tree under the same subject (our history
was rebased, so the SHAs differ), one is the single upstream-only commit, and 68
needed a per-commit verdict.

A commit was only called missing after three checks: `git cherry` patch-id
against our master, an exact-subject match against both our master and gkoh
master, and a content check for the symbol, UUID or constant the commit
introduces. No verdict here rests on a timestamp, because `pushed_at` is
unreliable in this network: six forks carry gkoh master's own push time, and
`daozhao` reports a `pushed_at` earlier than its `created_at` while being
identical to gkoh master.

## Verdicts by fork

| Fork | Best branch, commits ahead | Verdict |
| :--- | :--- | :--- |
| `tiredboffin/furble` | `tb-xt5`, 38 | Already present: became upstream #212, plus the NimBLE peer-address fix. The tail of the branch is debug scaffolding. |
| `Oaksen/furble` | `189-support-canon-eos-r-gps`, 13 | Already present: upstream #199 and PR #230. |
| `sentrybottale/furble` | `245-support-the-new-m5sticks3`, 9 | Already present: upstream #250. |
| `sky18Dragon/furble` | `feature/ricoh-*`, 5 | Already present: upstream #268 and #270. |
| `herrfrei/furble` | `nikon-smart-mode`, 1 | Already present: upstream #211. |
| `zhuimeng2026-hub/furble` | `master`, 9 | Two new commits worth taking, seven already present. Newest work in the network. |
| `slgray/furble` | `master`, 2 | Interesting hardware: the only AMOLED 1.8 bring-up in the network. |
| `LittleBidet/xteink_x3_furble_port` | `master`, 2 | Interesting for one shared-layer claim, not for the board. Upstream PR #313, closed unmerged. |
| `nariox/furble` | `moviemode`, 6 | Feature idea only: the branch ends in a save-state commit and never became a PR. |
| `ediiiiiiii/furble` | `master`, 1 | Minor: a fourth intervalometer roller. |
| `filovre/furble` | `devin/...nikon-ble-linux-script`, 5 | Out of tree: a Python Nikon remote. Documentation value only. |
| `sbstjn/furble` | 9 | Dead: the 2023 Arduino Core2 port, upstream PR #50 closed. |
| `Cronkan/furble` | 6 | Dead: 2021 pre-ESP-IDF code. |
| `wolcano/furble` | 4 | Dead: 2022 Canon support, later rewritten upstream. |
| `petabyt/furble-glued` | 8 | Skip: 6,966 lines vendoring NimBLE headers and forking the camera library. |

## What is worth taking

1. Web installer chip family, from `LittleBidet` PR #313. Defect, not a port:
   `web-installer/manifest.tmpl` emits an `ESP32` entry with the bootloader at
   4096 and an `ESP32-S3` entry at 0 for every platform, and
   `web-installer/generate-manifest.py` only substitutes the platform, version
   and asset base. A published S3 image therefore also offers an ESP32 entry
   pointing at S3 binaries, and choosing it flashes a bootloader at the wrong
   offset. Tracked in plan 372.
2. Intervalometer shutter hold, from `zhuimeng2026-hub/furble`
   (`78a9a7b5`). Upstream ships 30 ms; the fork raises it to 400 ms with a
   settings migration for stored values. 30 ms is plausibly below what a body
   needs for autofocus plus shutter start. Tracked in plan 373, and it must be
   decided per camera family rather than by copying the number.
3. Shutter release durability on queue overflow, from `LittleBidet` PR #313.
   The only item in that PR's list that we do not obviously already have:
   `Control::sendCommand` drops a command when the queue is full and only logs
   it, so a release queued behind a press can be lost. Tracked in plan 374.
4. Camera photo and movie mode, from `nariox/furble`. Our `Camera` has no mode
   concept and `CanonEOSRemote` hard-codes its control byte. Worth a design
   note before any code, since the branch is unfinished.
5. The fourth intervalometer roller, from `ediiiiiiii/furble`. The only commit
   in the network that cherry-picks clean. Low value, zero risk.

## Cleared, so nobody re-ports them

These look like fixes and are already in our tree, each verified by symbol or
file:

- BLE IRK and identity-key distribution for persistent resolvable private
  addresses (`ac876625`), ours in `lib/furble/Device.cpp`.
- The NimBLE peer address type fix (`e626f8ed`), ours in
  `patches/ble_gap.patch`.
- Self-delete-on-connect-failure and the false-negative connect path, ours in
  `lib/furble/Camera.cpp`.
- The Fujifilm "configured" wait and connection parameters after connect
  (upstream PR #223), deliberately not carried, recorded in plan 303.
- Serial test mode (upstream issue #235), superseded by the headless S3 profile,
  the console and the simulator.
- Build-tool hardening from `3c58f1f8`: our `patches/ble_gap_patch.py` uses the
  same marker and additionally locks and raises on a rejected hunk. The only
  missing piece is Windows `patch.exe` discovery.

## Upstream

gkoh master is one commit ahead of us, `0cac22aa` "Enable watchdog on
M5StickS3" (upstream PR #310). We are a superset: `Platform::watchdogEnable`
arms the M5PM1 at 45 s with a 1 s feed, where upstream calls `wdtSet(255)` and
aborts when PMIC init fails. No newer upstream commit exists.

Upstream PRs closed without merge were checked one by one: #4, #50, #108, #132,
#205, #223, #258, #307, #313. #108 and #132 have no head repository left, so
only their patches survive; both are superseded by `Settings::RECONNECT` and the
current backlight handling.

## Limits

Every verdict is static: diff reads, symbol and file presence checks, and
cherry-pick conflict counts. No candidate was built or run. Forks were not
modified and nothing was posted to any fork.

## Follow-up: state

The three action items are dispatched as separate PRs, each with its own plan
doc and independent review: 372 web installer chip family, 373 intervalometer
shutter hold, 374 control release durability. Items 4 and 5 above need a
decision before code.
