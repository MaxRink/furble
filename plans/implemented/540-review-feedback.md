# 540 - Review feedback for the HAL register verification pass

## Scope

Review of #540 (`fix/hal-datasheet-verification`), head `fa228154`, against
`origin/master` at `b8b273f3`. The review was posted as a comment review
(`COMMENTED`, 2026-11-27) because the account that authored the PR cannot submit
a "request changes" review to itself. Nothing was merged, pushed or edited on
the branch; every build and measurement ran in a throwaway worktree.

Line references are positions in the tree the PR produces. Each row records the
fix a remediation commit owes, so the table can be filled in as fixed, already
fixed or a false positive the way `AGENTS.md` requires.

## PR #540

| # | Finding | Where | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | The stuck-BUSY cost is stated as 11,200 us. It is 10,200 us: when BUSY is already high the rise wait returns before its first delay, so the 1,000 us assert budget is never spent. 200 + 10,000 = 10,200. | `lib/hal/SX1262.h:44`; `plans/implemented/385-hal-register-verification.md:54` | Fixed in the remediation commit on this branch: `lib/hal/SX1262.h` states 10,200 us for a line stuck high, `plans/implemented/385-hal-register-verification.md` says the same, and two host cases pin it. | `sx1262 stuck-high spend is the reset pulse plus the ready budget` compares the constant sum with the literal 10,200; `sx1262 stuck BUSY spends reset plus the ready budget, 10,200 us` compares the measured spend. Both pass, and `hal-drivers` reports 241 checks. |
| 2 | `docs/components/sx1262.md` re-numbers its timing-constants table (77/78/79/81/88/92, all correct) but leaves the facts table and the sleep-sequence table on the pre-change lines the guard's seven added header lines moved. Three citations now point at blank lines. | `docs/components/sx1262.md:34` (`SX1262.h:70`, is a comment; `RESET_PULSE_US` is `:77`), `:37` (`:68`, blank; opcode is `:75`), `:38` (`:72`/`SX1262.cpp:41`; settle is `:79`/`:50`), `:39` (`SX1262.cpp:43-46`, blank; confirm poll is `:52-55`), `:43` (`SX1262.cpp:41`, `SX1262.h:72`), `:52` (step 3, `SX1262.cpp:20-23`; write is `:44-47`), `:53` (step 4, `SX1262.cpp:24`, blank; settle is `:50`), `:16` (`SX1262.h:7` to `:30`; the comment now runs to about `:45`) | Fixed: every citation in `docs/components/sx1262.md` was re-derived from the post-change source, including the eight named here, the constants table, the intermediate-section rows and the new inference section. | Audited with a script that prints each cited line's text: `lib/hal/SX1262.h:7-56`, `:15-17`, `:18-36`, `:35-36`, `:37`, `:38-41`, `:54-55`, `:64-74`, `:82`, `:83`, `:84`, `:85`, `:86`, `:88`, `:95`, `:99`, `:101-111` and `lib/hal/SX1262.cpp:25-58`, `:26-28`, `:30-44`, `:37-44`, `:46-49`, `:51-52`, `:54-57` all land on the construct each row names. |
| 3 | The SX1262 defect's premise is stated as a datasheet fact. The document says the reset is followed by calibration (8.1 p. 53), that BUSY is set high in STARTUP at power-up or after a reset (9.1 p. 60), that BUSY goes low as the radio leaves Sleep (8.3.1 p. 55) and that BUSY is high during calibration (13.1.12 p. 80). It never says BUSY is low in a window after the NRESET release of an awake part, and the plan itself records that no rise time is documented (U-6). The fix is safe either way — it can only add a wait — but the severity rests on an inference. | `lib/hal/SX1262.cpp:30-34`; `plans/implemented/385-hal-register-verification.md:54`; `docs/components/sx1262.md:50` | Fixed: the premise is labelled an inference in the driver comment, the plan's defect row and the page, with the mechanism cited (9.1 page 60, 8.3.1 page 55, 13.1.12 page 80) and the reason the fix is safe either way stated. The page gained a section that says so in full. | `lib/hal/SX1262.cpp:30-36`, `lib/hal/SX1262.h:22-28`, `docs/components/sx1262.md` section "The window after the reset release", and the plan's defect 1 row. |
| 4 | The BQ27220 rationale states "an all-ones word from a gauge that is not estimating" as the field case in the Evidence column. SLUUBD4A 2.21 p. 25 gives the 0 to 100 range and no sentinel and says nothing about what a non-estimating gauge returns. The range check stands on its own. | `docs/components/bq27220.md:62`; `lib/hal/BQ27220.cpp:61-67` | Fixed: the driver comment and the page now say the all-ones field story is inferred, and the page's unverified list gained a row naming the bench read that would settle it. The range check stands on section 2.21 alone. | `lib/hal/BQ27220.cpp:61-68` and `docs/components/bq27220.md` row 6 and its unverified list. |
| 5 | The new IP5306 section cites `IP5306_Class.cpp` line 33 for `BOOST_OUT_BIT = 0x02`. Line 33 is the WLED write; the constant is line 28, `REG_SYS_CTL0 = 0x00` is line 14 and `setPowerBoostKeepOn()` is lines 112 to 120, not 113 to 121. The plan's IP5306 row repeats "line 33". | `docs/components/ip5306.md:78`; `plans/implemented/385-hal-register-verification.md` IP5306 table | Fixed: line 28 for `BOOST_OUT_BIT`, line 14 for `REG_SYS_CTL0`, lines 112 to 120 for `setPowerBoostKeepOn()`. The plan's IP5306 table carries the same corrections. | M5Unified 0.2.20 `src/utility/power/IP5306_Class.cpp:14`, `:28` and `:112-120`, read in `.pio/libdeps/m5stick-c-plus/M5Unified`. The `Power_Class.cpp:648` citation in row 3 of the same page was re-checked and is correct. |
| 6 | The new MPU-6886 row cites `MPU6886_Class.cpp` line 38 for the `0x10` ACCEL_CONFIG write. Line 37 is ACCEL_CONFIG; line 38 is GYRO_CONFIG. | `docs/components/mpu6886.md:102` | Fixed: line 37 for the ACCEL_CONFIG write, and the init table is cited as lines 37 to 43. | M5Unified 0.2.20 `src/utility/imu/MPU6886_Class.cpp:37`, `:38`, `:40`, `:41`, `:42`, `:43`. |
| 7 | The AXP192 page, which this PR edits, still cites `src/FurblePlatform.cpp:114` and `:115` and `include/FurblePlatformPolicy.h:33` to `:35`. The writes are at `src/FurblePlatform.cpp:186-187` and the masks at `include/FurblePlatformPolicy.h:47-48`, which is what the PR's own plan table says. Page and plan contradict each other. | `docs/components/axp192.md` rows 1 to 5 and its code map; `plans/implemented/385-hal-register-verification.md` AXP192 table | Fixed: the five rows and the code map now cite `src/FurblePlatform.cpp:185-188` and `include/FurblePlatformPolicy.h:26-60`, with the channel map at `:43-45` and the masks at `:47-48`. | A grep for the two register numbers in `src/FurblePlatform.cpp` gives 186 and 187; `include/FurblePlatformPolicy.h:47-48` holds 0xCC and 0x00. |
| 8 | The case that pins the total still explains it as "hold the header at 15,700 us" after the bound moved to 16,700. | `tests/host/hal_drivers_test.cpp:666` | Fixed: the case comment now holds the header at 16,700 us and states the 10,200 us spend. | `tests/host/hal_drivers_test.cpp`, the comment above the stuck-BUSY cases. |
| 9 | The plan says the M5PM1 comment's behavioural half is "pinned by the platform PMIC cases, which drive the shim through `setLedEnLevel`/`getPowerConfig`". Those cases drive `tests/host/boundaries/platform_shims/M5PM1.h`, not `sim/shim/M5PM1.h`, the file this PR edits. Nothing asserts the sim shim's LED or power-config path. The conclusion (a comment needs no test) is right; the evidence names the wrong double. | `plans/implemented/385-hal-register-verification.md:56` | Fixed: the plan says the platform PMIC cases drive `tests/host/boundaries/platform_shims/M5PM1.h`, which already recorded 0x06, and that nothing asserts the sim shim's LED or power-config path. | `plans/implemented/385-hal-register-verification.md`, defect 3 row. |
| 10 | Two rows in the plan's "Facts still unverified" list are closable from the PDFs: U-7 (SetSleep 0x84 and the 500 us window) is Table 13-1 p. 72 and the Caution on the same page, and U-8 (the twice-per-second rule) is 5.3 p. 64 verbatim. | `plans/implemented/385-hal-register-verification.md` U-7, U-8 | Closed: U-7 and U-8 are marked closed with the pages, and the SX1262 and BQ27220 tables carry the upgrade. The sleepConfig bit encoding stays open because Table 13-2 was not read. | Table 13-1 page 72 and the Caution on the same page, per this review; SLUUBD4A section 5.3 page 64, per this review. |
## Checked and not findings

- **Can this brick or unpower a board?** No. Nothing under `src/`, `include/` or
  `components/` changes, no source under `src/`, `components/`, `include/` or
  `sim/` includes `SX1262.h` or `BQ27220.h`, and no `CMakeLists.txt` outside
  `tests/host` names `lib/hal`, so no linked image changes. No rail, charger or
  gauge write changes on the T-Beam, the Waveshare or the Core2.
- **Registers the PR now writes:** none. The only write remains SetSleep 0x84 +
  0x00, unchanged. The reset values the pages record were checked against the
  datasheets: AXP192 REG 82H 0x83, REG 83H 0x80 with bit 7 the internal
  temperature enable, REG 84H 0x32 (p. 41); AXP2101 REG 18 default 0x17 with
  bit 3 gauge enable set at POR (p. 34), REG 80 bits 0-4 the DCDC enables
  (p. 50), REG 90 with "aldo1 enable" printed on both bit 0 and bit 4 (p. 53);
  M5PM1 PWR_CFG 0x06 default 0x17 with bit 4 LED CONTROL and bit 3 5VIN/OUT
  (p. 13).
- **Test doubles:** the SX1262 fake gained `busyLowPollsAfterReset` and
  `busyNeverRises` and 4 of the 5 named cases fail on revert; the case literally
  named `sx1262 waits for BUSY to rise after reset` (`:677`) passes either way,
  and the PR body is accurate that only four fail. The BQ27220 fake is the
  register map and 2 cases fail on revert. The M5PM1 change is a comment and has
  no double, by construction.
- **Size and RAM:** `pio` is absent and there is no `.pio/build`, so
  `check_size_budget.py` could not run; `tools/stack_budget.json` names no HAL
  entry and the changed functions are in no firmware task's graph. The image is
  unchanged by construction, not by measurement, and the PR should say so.
- **Static analysis:** not runnable here; `check_static_analysis.py --base
  origin/master` reports `no compile_commands.json in .pio/static-build`.
- **Gates reproduced:** `pre_review.py --base origin/master` `PASS: 13 gates`
  with clang-format 21.1.8 and PyYAML present; `check_datasheet_citations.py`
  `32 cited, 3 page gaps, 0 errors of 35 tokens`; `check_doc_citations.py` `0
  errors of 3229 citations in 176 files`; `check_doc_tokens.sh` present;
  `check_portability_inventory.py` clean; `hal_drivers_test` `PASS (239
  checks)` against `PASS (219 checks)` on master.
- **Documentation mandate:** satisfied for coverage. Every changed behaviour has
  its component page updated in the same PR; the gaps are accuracy (findings 2,
  4, 5, 6, 7), not missing pages.

## Documents opened for this review

| Document | Revision | sha256 (first 8) | Pages |
| :--- | :--- | :--- | :--- |
| Semtech SX1261/2 DS.SX1261-2.W.APP | Rev 2.2, Dec 2024 | `6d783125` | 8.1 p. 53, 8.2.2 p. 54, 8.3.1 p. 55, Table 8-2 p. 56, 9.1 p. 60, 13.1.1 p. 72, 13.1.12 p. 80 |
| TI BQ27220 TRM SLUUBD4A | Rev A, Nov 2022 | `44779f22` | 2.11 p. 23, Table 2-6 p. 23, 2.21 p. 25, 2.23/2.24 p. 25, 5.1 p. 63, 5.3 pp. 63-64 |
| X-Powers AXP192 (EN mirror) | 53 pp | `6b3e45b9` | REG 82H-84H p. 41, REG 5EH p. 31 |
| X-Powers AXP2101 | Rev 1.0, 2021-05-31 | `42101198` | 6.13.2.14 p. 34, 6.13.2.68 p. 50, 6.13.2.75 p. 53, 6.13.2.88 p. 57 |
| M5PM1 Chip User Manual | V 1.9 | - | Register map pp. 4-11, PWR_CFG p. 13 |
| M5Unified | 0.2.20 | - | `M5PM1_Class.cpp`, `AXP192_Class.cpp`, `AXP2101_Class.cpp`, `MPU6886_Class.cpp`, `IP5306_Class.cpp` at the cited lines |

## Resolution

All ten findings are fixed on `fix/hal-datasheet-verification`, in one remediation
commit stacked on the two the review read. The two blockers first, because each
made a claim the tree did not support:

1. **The stated stuck-BUSY spend.** The rise wait is satisfied by its first
   sample when the line is already high, so it never delays and the 1,000 us
   assert budget is not spent. A line stuck high costs the 200 us reset plus the
   10,000 us ready budget: **10,200 us**. The header, the plan and the case
   comment say that now, and two new host cases pin it, one against the constant
   sum and one against the measured spend, so the two cannot drift apart again.
   The reviewer's measurement with the branch's driver was `stuck-high: ok=0
   delayed=10200 us`, which the second case now reproduces as an assertion.
2. **The page renumbering.** `docs/components/sx1262.md` was rewritten rather
   than patched: every citation in it was re-derived from the post-change source
   and checked line by line with a script that prints the cited line's text. The
   eight citations the review named, the rows that moved with them and the
   citations into `lib/hal/SX1262.cpp` all resolve to the construct their row
   names. The same audit over the other seven component pages this PR touches
   caught the AXP192, IP5306 and MPU-6886 rows this review named, and one the
   review did not: `bq27220.md` cited `lib/hal/BQ27220.h:6`, the `HalBus.h`
   include, for the read-only header comment that starts at line 8.

The eight smaller findings:

3. The low-BUSY window is now labelled an inference in the driver, the plan and
   the page, with the clauses that do exist cited as the mechanism (9.1 page 60,
   8.3.1 page 55, 13.1.12 page 80) and the reason the fix is safe either way
   stated: a line that is already high satisfies the wait without a delay.
4. The all-ones field story is labelled an inference in the driver and the page,
   and the page's unverified list now names the bench read of `0x2C` on a removed
   cell that would settle it.
5. `ip5306.md` cites line 28 for `BOOST_OUT_BIT`, line 14 for `REG_SYS_CTL0` and
   lines 112 to 120 for `setPowerBoostKeepOn()`. The `Power_Class.cpp:648`
   citation in the same page was re-checked and is correct.
6. `mpu6886.md` cites line 37 for the ACCEL_CONFIG write, and the init table as
   lines 37 to 43.
7. `axp192.md` cites `src/FurblePlatform.cpp:185-188` and
   `include/FurblePlatformPolicy.h:26-60`, with the channel map at `:43-45` and
   the masks at `:47-48`.
8. The case comment holds the header at 16,700 us and states the 10,200 us spend,
   so the file no longer carries the old bound.
9. The plan's M5PM1 evidence names `tests/host/boundaries/platform_shims/M5PM1.h`
   as the double those cases drive, and says plainly that nothing asserts
   `sim/shim/M5PM1.h`'s LED or power-config path.
10. U-7 and U-8 are closed from Table 13-1 page 72 with its Caution, and from
    SLUUBD4A section 5.3 page 64. The sleepConfig bit encoding stays open because
    Table 13-2 was not read.

### What is not claimed

`check_size_budget.py` and `check_static_analysis.py` need `pio` and a
`compile_commands.json`. The branch changes no firmware source: nothing under
`src/`, `include/` or `components/` changes, no source under `src/`,
`components/` or `include/` includes `SX1262.h` or `BQ27220.h`, and no
`CMakeLists.txt` outside `tests/host` names `lib/hal`, so the image and its
static RAM are unchanged by construction. That is reasoning, not a measurement,
and CI is where the measurement happens.

### Gates after the remediation

| Command | Result |
| :--- | :--- |
| `cmake --build <host build> --target hal_drivers_test` | clean, `-Wall -Wextra -Werror` |
| `hal_drivers_test` | `hal-drivers: PASS (241 checks)`, two more than at review |
| `sh tools/run_sanitized_host_tests.sh '^hal-drivers$'` | PASS under ASan and UBSan |
| `ctest --test-dir <host build> --output-on-failure` | `100% tests passed out of 223` |
| `python3 tools/pre_review.py --base origin/master` | 13 gates pass |
| `python3 tools/check_datasheet_citations.py` | 0 errors |
| `python3 tools/check_doc_citations.py` | 0 errors |
| `sh sim/scripts/check-doc-tokens.sh` | tokens present |
| `clang-format --dry-run --Werror` on the changed C and C++ | clean, clang-format 21.1.2 |

The branch was rebased onto `origin/master` at `18bbcb5a` before the remediation,
because master had moved `sim/hardware_model.cpp` and the plan's citations into it
had to be re-derived. Every citation the rebase moved was re-checked.

## The two checks that failed after the remediation

Head `2526e65c` addressed all ten findings and left two checks red. Both
assertions were read from the job logs before anything was changed.

### ASan and UBSan host tests, job `140252663079`

One case failed out of 220, and the sanitizer named the site:

```
167/220 Test #167: control-e2e-multi-connect-fujifilm ... Subprocess aborted
/usr/include/c++/15/bits/stl_uninitialized.h:145:17: runtime error: reference
binding to null pointer of type 'struct Write'
  #4 FujifilmVirtualCamera::writes() const lib/testing/peer/FujifilmVirtualCamera.cpp:140
  #5 shutterWriteCount tests/host/control_e2e/control_e2e.cpp:98
  #6 waitForShutterWrites tests/host/control_e2e/control_e2e.cpp:134
  #7 scenarioMultiConnectFujifilm tests/host/control_e2e/control_e2e.cpp:642
```

Neither of the two behaviour changes on this branch is in that stack. Both were
audited anyway.

- `BQ27220::read()`: the only caller in the tree is
  `tests/host/hal_drivers_test.cpp`. It initialises the struct before every
  call (`BQ27220::Reading socKept = {4200, -200, 64, 300, true, true}`), so a
  failed read leaves the previous sample in place rather than an uninitialised
  one, and the new cases assert exactly that for the 101 and 0xFFFF words. No
  source under `src/`, `include/` or `lib/furble` names the gauge, so there is
  no other caller to audit.
- `SX1262::sleep()`: the rise wait is issued after `port.setReset(false)`, so
  the pin is never sampled while NRESET is held, and the confirmation wait is
  issued after `port.transfer()` returns and after the 500 us settle, not
  before the transfer. `hal-drivers` passes under both sanitizers, including
  the case that compares the measured spend with the stated bound.

The cause is in the test double, and it predates this branch: nothing under
`lib/testing/` changes here. `FujifilmVirtualCamera::write()` appends to
`m_Writes` while it holds `Host::peerWriteJournalMutex()`, and `writes()` copied
the vector without that lock, so the copy could run while `push_back()` was
reallocating it. `WriteJournal.h` already states that the mutex is what keeps
the append and the snapshot from overlapping; only the append side took it.
ThreadSanitizer on the same scenario reports the pair directly: the append in
`write()` against the copy in `writes()`, 29 reports in total.

Fixed with a mutex owned by the peer, `m_JournalMutex`, which serialises exactly
three things: the append in `write()`, the snapshot in `writes()` and the clear
in `clearEvents()`. The append takes it in a scope of its own rather than for the
whole handler, so the lock is never held across anything that can yield on the
simulator's scheduler.

The first attempt took `Host::peerWriteJournalMutex()` in the accessor instead,
and the simulator hung: `sim/BleSim.cpp` already holds that process-wide mutex
around `peer->writes()`, because the other peer families return a view and need
the caller's lock, so the accessor tried to take one non-recursive mutex twice.
The `Simulator end-to-end` job on that head failed the `peer-fuji-basic-write`
scenario with `SIM WATCHDOG: failing the run` and exit 3, which is the watchdog
reporting the wedged UI task. The per-peer mutex removes the nested acquisition
and keeps the calling convention intact; the probe now also locks
`Host::peerWriteJournalMutex()` around the accessor, so a regression to the
process-wide mutex hangs the probe instead of the simulator, and its ctest
timeout reports it.

The new probe `fujifilm-journal-race` runs the overlap on purpose, one thread
appending shutter writes through the peer and one taking snapshots and
clearing. Two mutations pin it: dropping the snapshot lock makes
ThreadSanitizer report four races and `run_tsan_race.sh` fail closed, and
reaching for the process-wide mutex inside the accessor hangs the probe. It has
no plain twin because no other sanitizer can see the overlap, and it joins the
ThreadSanitizer exclusion list in `tools/run_sanitized_host_tests.sh` for the
same reason as the other three probes.

Two siblings are recorded gaps, not fixed here. `notifications()` has the same
shape, but its writer holds the subscription delivery lock rather than the
journal mutex, so it needs a lock of its own and a probe; and
`NimBLEClient::mockDropLink()` called from a test thread against a running
target is a race in the scenarios' fault injection, not in the journal. Both are
in the ThreadSanitizer report for this scenario. Neither is in the failing
assertion, and fixing either changes how the scenarios inject faults, which does
not belong in this PR.

### Simulator end-to-end, job `140252662698`

The failing step is `Fuzz the no-touch UI (all modeled panels)` and the
assertion is its determinism replay:

```
=== determinism replay seed 31337 (600 steps) ===
FAIL determinism replay seed 31337 (fuzz report lines diverged)
-FUZZ COVERAGE pages=28: about=5 ... connected=37 ... main=223 ...
+FUZZ COVERAGE pages=28: about=5 ... connected=39 ... main=221 ...
```

`connected` and `main` move by two in opposite directions and every other line
of the two reports matches. Two runs of the same binary with the same seed
disagree, so the branch's code cannot be the cause: the simulator compiles no
`lib/hal` source, and the branch's only file under `sim/` is a comment-only hunk
in `sim/shim/M5PM1.h`, which changes no behaviour.

Rebasing does not address it either. `18bbcb5a..baa4ac23` is PRs 536 and 539, and
it changes `docs/hardware-catalog.md` and four files under `plans/`. No scenario,
harness, manifest or gate moved, so the failure is not in a scenario master
changed.

So this one is a flake, and the shape says load: two nominal identical runs of
one binary diverged, which no code change on this branch can cause. The same
step passed on master at `18bbcb5a`, and on this machine the whole no-touch fuzz
suite, eight seeds plus the seed 31337 replay, passes: `All fuzz seeds behaved as
expected (8 runs)` and `PASS determinism replay seed 31337`.

The first attempt at the sanitizer fix then turned this job red for a real
reason that belongs in this record. `sim/BleSim.cpp` holds
`Host::peerWriteJournalMutex()` around `peer->writes()`, because the other peer
families return a view and need the caller's lock. That attempt took the same
mutex inside the accessor, so the UI task wedged on a non-recursive mutex:
`peer-fuji-basic-write` finished with `SIM WATCHDOG: failing the run` and exit 3
(job `140271049726`). The per-peer mutex described above removes the nesting,
`furble-sim --script sim/scenarios/e2e/peer-fuji-basic-write.txt` exits 0, and
the same binary hangs when the accessor is put back on the process-wide mutex.

### Results

| Check | Result |
| :--- | :--- |
| `ctest` on the plain host build | `100% tests passed out of 224` |
| `control_e2e_test multi-connect-fujifilm`, 40 consecutive runs | 40 of 40 pass |
| `fujifilm-journal-race` under `run_tsan_race.sh` | 0 race reports, `fujifilm-journal-race: PASS` |
| the same probe with the snapshot lock removed | 4 race reports, the wrapper fails closed |
| the same probe with the process-wide mutex in the accessor | hangs, reported by the ctest timeout |
| `furble-sim --script sim/scenarios/e2e/peer-fuji-basic-write.txt` | exit 0, and hangs with the process-wide mutex in the accessor |
| `sh sim/scripts/run-fuzz.sh` with `FURBLE_SIM_NO_TOUCH=1`, 600 steps | all eight seeds and the seed 31337 determinism replay pass |
| ASan and UBSan host tests at `086fb5ab` | green, job `140271049681` |
