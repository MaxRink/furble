# 550 - L5 and dual-band GNSS receivers, review feedback

Review of PR 550 (`feat/l5-dual-band`, head `3f4129c3`) against `origin/master`
`baa4ac23`, in a throwaway worktree at `/tmp/review-550`. Verdict: request
changes. Nothing blocks in the two classes the brief names (a wrong signal key,
or a detection that misreads an L1-only part as dual-band); both were checked
against the vendor PDFs and hold. Four findings should be fixed before merge and
three are nits. Posted as a PR review comment, not committed: this account is
the PR author, so GitHub refuses a formal request-changes.

## What verified clean

- The four L5 key ids against u-blox F10 SPG 6.00 IFD UBX-23002975 R02, 181
  pages, printed page equals PDF page: GPS L5 `0x10310004` page 158, Galileo E5a
  `0x10310009` page 158, BeiDou B2a `0x10310028` page 158, QZSS L5 `0x10310017`
  page 159, Table 46 on 159. All four match `lib/furble/protocol/GpsUbx.h:93,98,102,106`,
  including the `0x10310028` the PR flags as the trap.
- No L5 key in the M10 IFD UBX-21035062 R03: 4.9.21 page 152, Table 42, ends at
  `GLO_L1_ENA`. No `CFG-PM` group anywhere in the F10 IFD (`grep -c` = 0), no
  GLONASS key in its 4.9.20 table.
- B1I vs B1C and B1I vs GLONASS: MAX-M10S DS footnote 13 page 6 and IM-MAX
  2.1.3 page 10. Held by `tests/host/gps_ubx_test.cpp` `testPlan()` and
  `testB1cRejections()`.
- Power save vs B1C: RN-M10 7.2 page 15 and 5.1.5 page 8, DS-MAX note 28 page
  13. Held by `testPlan()`, `testB1cConfigOrder()` and
  `sim/scenarios/e2e/gps-ubx-power-save-b1c.txt`.
- Detection cannot misread an L1-only part: the plan is only built after the
  probe (`src/FurbleGPS.cpp:1795-1806`), the probe answer overwrites the MON-VER
  hint (`src/FurbleGPSUbx.cpp:163-167`), a later MON-VER cannot reopen it
  (`:547-549`), and all four keys must answer (`:499-508`). One NAK, held by
  `gps-ubx-l1-only-refuses-l5.txt:23`.
- The band-pair rule: IM-F10N 2.1.3 page 10, enforced by both rows sharing one
  `writeSignal`.
- Ran: `ctest` 223 of 223; `gps_ubx_test` 321 checks; `gps_ubx_m10_test` 100;
  `console_commands_test` 1344; `gps_ubx_fuzz` seeds 3 and 7 at 20000 iterations,
  0 failures; `check_sim_scenarios.py`; `check_doc_citations.py` 0 of 3202;
  `pre_review.py --base origin/master` 13 gates (needs clang-format 21 on PATH);
  the built simulator with `run-e2e.sh`, all 277 scenarios passed, exit 0.

## 1. The UI gate disables the wrong controls (src/FurbleUI.cpp:9700-9713)

`updateGPSPowerCapability()` disables everything in `m_Status.gpsPowerWidgets`,
and the only members are the GPS Power page's two rollers
(`src/FurbleUI.cpp:9892`): the "Receiver" policy (`GPS_POWER`, options
`Always on / Standby (PCAS12) / Rail cycling`, `include/FurbleUI.h:852`) and
"Sleep between fixes" (`GPS_DUTY`, `include/FurbleUI.h:853`). Neither is a
u-blox PSM mode, and `Settings::GPS_PSM` is not exposed in the LVGL UI at all
(`grep -n GPS_PSM src/FurbleUI.cpp` returns nothing), so the control the PR means
to gate is not on this page.

Consequences on the very part this PR adds:

- "Standby" is `RXM-PMREQ` software standby on a UBX receiver
  (`src/FurbleGPS.cpp:807-829`), and IM-F10N appendix page 64 says the NEO-F10N
  supports hardware backup and software standby. The gate removes a working mode.
- Rail cycling and the duty interval are furble's own levers, and plan section 7
  says they keep working on this part because it has no PSM. The gate removes
  them, on a receiver that draws 21 mA.

`docs/settings-and-controls.md:195-199` and `plans/512-dual-band-l5.md:227`
describe a different behaviour from the code. No test covers the path:
`gpsPowerWidgets` and `updateGPSPowerCapability` appear only in
`src/FurbleUI.cpp` and `include/FurbleUI.h`. Either gate nothing, or gate the
setting that maps to CFG-PM.

## 2. `bands` claims a signal write that never happened (GpsUbx.cpp:609)

`lib/furble/protocol/GpsUbx.h:512` documents `PlanSummary::bands` as "actually
written, 0 when no signal write happened"; `lib/furble/protocol/GpsUbx.cpp:609`
sets it from `caps.bands` with no check on `request.gnss_set`. With the settings
leaving every constellation bit alone, the codec, built and run standalone:

```
gnss_set=0 dual:  bands=3 signals=0 unset=18 power=0 l5_written=1
```

So `src/FurbleGPSUbx.cpp:232-236` logs "GPS signals: L1 and L5, both bands of
each enabled constellation" with nothing queued, `src/FurbleGPSUbx.cpp:702` sets
`status.l5_written`, and `src/FurbleConsole.cpp:1715` prints `l5_signals:
written`. Gate `out.bands` on `(request.gnss_set != 0) && !out.gnss_refused` and
add the negative case to `testDualBand()`.

## 3. AssistNow Autonomous is disabled on dual-band receivers, unsourced
## (GpsUbx.cpp:608, :633)

`b1c` is true whenever BeiDou is in the mask on a dual-band part, so
`out.ana = request.ana && !b1c` turns ANA off. Standalone codec:

```
dual GPS+BDS ana: request=1 plan=0
m10  GPS+BDS ana: request=1 plan=1
```

The only source is RN-M10 5.1.5 page 8, an SPG 5.10 statement. NEO-F10N DS
Table 5 page 6 lists AssistNow Autonomous with no B1C caveat. With the default
constellation set (BeiDou on) ANA is therefore silently off on a NEO-F10N, and
the warning cites a rule no F10 document states. Cite an F10 source or leave ANA
alone on `FAMILY_F10`.

## 4. B1I is deleted from BBR only, and the comment misstates the firmware
## (GpsUbx.cpp:605-607, :729-734)

F10-IFD R02 page 158 lists `CFG-SIGNAL-BDS_B1_ENA 0x1031000d`, so the firmware
has the key; it is the NEO-F10N module that has no B1I (DS-F10N 1.3 page 5,
Table 4). The plan deletes B1I from BBR (`src/FurbleGPSUbx.cpp:265`) and leaves
the RAM value standing, and the pre-PR plan wrote B1I = 1 to RAM and BBR whenever
BeiDou was on and GLONASS off (`GpsUbx.cpp:692`). A NEO-F10N that ran an earlier
furble therefore enters the session with B1I = 1 in RAM while this plan writes
B1C = 1 beside it (`:735-743`) -- the combination DS-MAX footnote 13 page 6
forbids. If that batch is refused, `src/FurbleGPSUbx.cpp:461-481` sees an L5 key
in it, demotes to L1 and logs "refused the L5 signal keys" for a BeiDou problem.
Writing `B1I = 0` costs one known key, is accepted by the model
(`sim/ubx_m10.cpp:217-219` rejects only B1I *on*), and removes the ambiguity.

## 5. An L5 probe answer leaves `psm` and `glo` permissive (FurbleGPSUbx.cpp:163-167)

The probe sets `bands` only. For a receiver whose MON-VER names no family,
`CAPS_UNKNOWN` keeps `psm = true` and `glo = true`
(`lib/furble/protocol/GpsUbx.h:476-485`), so `bands = L1|L5` arrives with a plan that
writes GLONASS keys and CFG-PM keys to an F10 and NAKs. "Fail closed" holds for
the band and not for the two capabilities that come with the family. Derive
`psm`/`glo` from the probed family, or set both false when the probe answers L5.
No test covers `CAPS_UNKNOWN` plus an L5 answer.

## 6. Page and section citations off by one

The gate cannot see external pages, and several are wrong. Verified against each
PDF:

| Cited | Actually | Where |
| :--- | :--- | :--- |
| DS-F10N 1.3 page 4 | page 5 | `GpsUbx.h:434-444`, plan 5 and 7, `ublox-neo-f10n.md` fact 2, `sim/ubx_m10.cpp:98`, `gps_ubx_m10_test.cpp` header |
| DS-F10N Table 3 page 4 | page 5 | plan 6, `gps_ubx_test.cpp` header, `gps_ubx_m10_test.cpp` header, `sim/ubx_m10.cpp:422`, `ublox-neo-f10n.md` fact 6 |
| DS-F10N Table 4 page 5 | page 6 | plan 5, `GpsUbx.h:536-540`, `GpsUbx.cpp:605-606` and `:730-731`, `sim/ubx_m10.cpp:99,215-216`, `gps-ubx-dual-band.txt:43`, `ublox-neo-f10n.md` fact 3 |
| DS-F10N Table 6 page 5 | page 6 | `ublox-neo-f10n.md` fact 3 |
| DS-F10N Table 16 page 12 for software standby | software standby is page 13 | plan 7, `sim/ubx_m10.cpp:56`, `ublox-neo-f10n.md` fact 7 |
| DS-MAX Table 3 page 4 | page 5 | plan 6 |
| "VALDEL of an unknown key is NAKed (F10-IFD 3.10.4 pages 54 and 55)" | VALDEL is F10-IFD 3.10.3 page 52, M10 3.10.3 pages 51-52 | `GpsUbx.cpp:722-724`, `:730-731`, `:753-756`, `docs/protocols/gnss-ubx.md` |

Right, and worth keeping as they are: F10-IFD pages 158 and 159 Table 46, M10
page 152 Table 42, F10-IFD 1.2 page 10, IM-F10N 2.1.3 page 10, 2.1.4 page 11,
pages 63 and 64, DS-F10N 1.1 page 4, Table 2 page 4, Table 15 page 12, DS-MAX
notes 13 and 28, Tables 15 to 17 page 13, RN-M10 pages 8 and 15.

## 7. Nits

- `bands_probed` is set on the timeout path too (`src/FurbleGPSUbx.cpp:166`), so
  `src/FurbleConsole.cpp:1690` says "asked the receiver" for silence, and
  `include/FurbleGPS.h:107-108` claims more than the field records.
- `sim/scenarios/e2e/gps-ubx-dual-band.txt:33-40` cannot fail: `m10.ram.<key>`
  falls back to the defaults (`sim/ubx_m10.cpp:177-190`) and the f10 preset has
  all four L5 keys at 1 (`sim/ubx_m10.cpp:111-124`). Use `m10.bbr.<key>`, which
  returns `unset` unless an accepted VALSET stored it (`sim/fake_uart.cpp:896`).
  `gps-ubx-dual-band-glonass-refused.txt:21-22` is trivially true for the same
  reason; `naks 0` is the real assertion there.
- The fuzz rate invariant uses `plan.gnss` rather than the effective mask
  (`tests/host/fuzz/gps_ubx_fuzz.cpp:224-226` against `GpsUbx.cpp:602`), and
  never checks the band pair.
- `VirtualM10::maxRateHz()` duplicates the codec table; nothing compares the two,
  so plan section 10's "rather than against itself" is stronger than the test.
- `tests/host/console_commands_test.cpp:1452` asserts `power_save_supported:
  false` for a bare struct, while the firmware's pre-probe `CAPS_UNKNOWN` has
  `psm = true`.

## 8. docs-capture and what to prove before merge

Verified: no capture script seeds `gps_model`, so all six run the CASIC model
(`sim/fake_uart.cpp:160`); the new render paths are behind a UBX backend
(`src/FurbleUI.cpp:10296-10303`) or clear themselves for a non-UBX backend
(`:9703-9704`); no PNG changes. The claim holds for the pages this PR can reach.
But the capture script's own header says two images depend on the built binary
("the Device info and About heap rows come from the modelled allocator"), and
this PR adds a vector member and a `push_back`, so prove it rather than argue it:
run `sh sim/scripts/docs-capture.sh` on the merge base and on the head with the
same board set and `FURBLE_SIM_RIG=0`, and diff the PNG trees. Identical means
done; different means either those two heap rows (name them in "Not done") or a
real regression that needs regenerating.

## Not run

`check_fuzz.py --full` (the fuzz binary was run directly at seeds 3 and 7). No
hardware: every statement about a real NEO-F10N above comes from the documents.

The simulator build needed the main checkout's `.pio/libdeps/m5stick-s3` and
`managed_components/lvgl__lvgl`, since this worktree has neither. With those the
three new scenarios pass individually and `run-e2e.sh` reports all 277 passed.

## Disposition

Each finding is answered against the tree that fixes it; line numbers are from
that tree. The two classes the review verified (signal keys, exclusivity,
detection, rate clamping) were not reworked.

### 1. The UI gate disabled the wrong controls

Removed rather than narrowed. `Settings::GPS_PSM` is not in the LVGL UI:
`grep -rn GPS_PSM src/*.cpp include/*.h` finds it only in `src/FurbleConsole.cpp`
and `src/FurbleCompanionService.cpp`. The GPS Power page's two rollers are
`Power saving > Receiver` (`m_GPSPowerOptions`: Always on, Standby (PCAS12),
Rail cycling) and `Sleep between fixes` (`m_GPSDutyOptions`), declared at
`include/FurbleUI.h:851-852`. Software standby is RXM-PMREQ, which the F10
firmware has even without a CFG-PM group (IM-F10N appendix page 64), and rail
cycling and duty are furble's own.

Deleted with the gate: `UI::updateGPSPowerCapability`, `Status::gpsPowerWidgets`,
its declaration in `include/FurbleUI.h`, the GPS menu click callback and the
roller `push_back`. A grep for `gpsPowerWidgets` or
`updateGPSPowerCapability` under `src` and `include` returns nothing.

Test: the simulator query `ui.disabled_rollers` (`src/FurbleUI.cpp:5812`) counts
the rollers in the disabled state on the page the UI shows.
`sim/scenarios/e2e/gps-ubx-dual-band.txt:69` opens `gps_power` on the dual-band
receiver and asserts `ui.disabled_rollers 0`. Restoring the old gate fails it.

### 2. `bands` claimed a write that did not happen

`out.bands` starts at 0 (`lib/furble/protocol/GpsUbx.cpp:611`) and is set only
where a key is queued: in `writeSignal` (`:725`) and in the B1I row (`:745`).
`testDualBand()` (`tests/host/gps_ubx_test.cpp:661`) covers all three outcomes: an
undecided constellation queues no key and leaves `bands` at 0, a decided one
carries its band, and a refused plan has `signals.empty() && bands == 0`.

### 3. ANA was disabled on every dual-band receiver with BeiDou

`out.ana = request.ana && !(b1c && !caps.dualBand())` (`GpsUbx.cpp:638`). The
exclusion belongs to the M10 firmware: its data sheet covers AssistNow
Autonomous for B1I and not B1C (DS-MIA Table 5 page 6) and the release notes say
so (RN 5.1.5 page 8). The F10 data sheet lists ANA with no BeiDou caveat
(DS-F10N Table 5 page 6), so a dual-band plan leaves it alone. The exclusivity
matrix gains a row for the difference (`docs/protocols/gnss-ubx.md:223`), and
`testDualBand()` asserts ANA survives BeiDou on an F10 plan while the M10 plan
still drops it.

### 4. B1I was deleted from BBR only

The F10 firmware does have the key (F10-IFD 4.9.20 page 158, 0x1031000d), so the
plan writes B1I = 0 in the same RAM and BBR signal batch as every other key
(`GpsUbx.cpp:744`), which clears a stale RAM value an earlier furble left. The
delete is gone: a VALDEL touches BBR only, and the driver sends deletes after the
writes (`src/FurbleGPSUbx.cpp:281`), so the delete left BBR unset and RAM
unchanged, the mirror image of the finding. The comment cites the F10 interface
description for the key, and the refusal log no longer claims a dual-band
firmware has no B1I signal.

Tests: `testDualBand()` asserts B1I is written off and not listed for delete; the
scenario asserts `m10.ram.1031000d 0` and `m10.bbr.1031000d 0`
(`sim/scenarios/e2e/gps-ubx-dual-band.txt:44-45`).

### 5. An L5 answer left psm and glo permissive for an unnamed family

`GPS::serviceCaps` adopts the whole `CAPS_F10` set, not just the band, when the
probe answers L5 and MON-VER did not already name the family
(`src/FurbleGPSUbx.cpp:174`), so no CFG-PM key and no CFG-SIGNAL-GLO key is
written: the F10 firmware has neither (F10-IFD 4.9; IM-F10N appendix page 64)
and an unknown key NAKs the batch that carries it.

Test: a new virtual receiver, `f10-unnamed`, is the same firmware behind the
module string `NEO-F10X`, which `capsFromVersion` cannot match. The scenario
`sim/scenarios/e2e/gps-ubx-dual-band-unnamed.txt` asserts `family 2` after the
probe, `psm_supported 0`, the L5 keys in BBR, and that
`m10.bbr.10310025`, `m10.bbr.10310018`, `m10.bbr.20d00001` and
`m10.bbr.40d00002` stay `unset`, with `m10.naks 0`.

### 6. Page and section citations off by one

Corrected in every place the review listed, and in the places that carried the
same number: DS-F10N 1.3 page 5, Table 2 and Table 3 page 5, Table 4 and
Table 6 page 6, Table 5 page 6 for ANA, Table 16 page 13 for software standby;
DS-MAX Table 2 and Table 3 page 5; F10-IFD 3.10.3 page 52 for the VALDEL of an
unknown key, with VALGET left at 3.10.4 page 55. The files are
`docs/components/ublox-neo-f10n.md`, `docs/components/ublox-max-m10s.md`,
`docs/protocols/gnss-ubx.md`, `plans/512-dual-band-l5.md`, `GpsUbx.cpp`,
`GpsUbx.h`, `sim/ubx_m10.cpp` and the scenario headers.

### 7. Nits

- `bands_probed` after a timeout: `CapsProbe::answered()` (`GpsUbx.h:684`) is set
  only by `CapsProbe::answered(bool)` (`GpsUbx.cpp:792`); the timeout path
  returns L1 with `answered()` false, and `m_UbxCapsProbed = answered`
  (`FurbleGPSUbx.cpp:180`) keeps the status row a guess. The host test now
  asserts `!probe.answered()` after the 2100 ms timeout.
- The dual-band scenario's RAM L5 assertions could not fail: they are gone. The
  scenario reads BBR only, where a key exists only if an accepted VALSET stored
  it, so `m10.bbr.10310004` and its three siblings fail unless furble wrote them.
  The L1-only scenario asserts the same ids are `unset`.
- The fuzz rate check used the requested mask: it now uses the effective mask the
  plan ran with, ceil division, and a band-pair check
  (`tests/host/fuzz/gps_ubx_fuzz.cpp:223-230`).
- The model's `maxRateHz` duplicated the codec with no comparison:
  `testDualBandVariant()` (`tests/host/gps_ubx_m10_test.cpp:353`) compares
  `Ubx::maxRateHz` with `VirtualM10::maxRateHz` over eleven masks, including the
  B1C/B1I pair for BeiDou with GLONASS.
- The console test asserted a state the firmware never has: the bare case now has
  `bands = 1` and `power_save_supported: true`
  (`tests/host/console_commands_test.cpp:1456`).

### 8. The docs-capture claim

Measured, not argued. `sim/scripts/docs-capture.sh` was run with
`FURBLE_SIM_BOARDS=s3` (the script sets `FURBLE_SIM_RIG=0` itself) on the merge
base `baa4ac23` and on the head, each in its own worktree with the same
`FURBLE_DEP_ROOT` and `FURBLE_LVGL_DIR`, and the two `docs/img` trees were
compared with `diff -rq`. The two trees are byte-identical: no file differs, so
this PR changes no rendered pixel, and the Device info and About heap rows carry
the same figures on both revisions.

Each run on its own also rewrites `diag-device-info.png` against the committed
bytes. That is the documented modelled-allocator row, not a head-versus-base
difference: the base run left the same single file modified in its own worktree.
The capture script's header already says not to gate a fresh render against the
committed bytes.
