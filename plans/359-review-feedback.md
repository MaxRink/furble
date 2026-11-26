# 359 review feedback

An adversarial review of `fix/316-sony-7rm4` (PR 508) against its own claims. Two
review passes are folded into this record: the first, recorded at `370bba46`,
attacked six statements and closed three gaps; the second re-ran every
measurement independently, added findings 7 and 8, and corrected three numbers
the first pass recorded. Where the two disagree the corrected figure is stated
with the command that produced it.

Findings 1, 3, 4 and 7 were fixed on the branch. Finding 2 is open and is the
only reason this PR is held: the branch is 493 bytes over the `m5stack-core-debug`
slot on its own, and the tree-wide headroom change (PR #518, plan 347, 25,392
bytes) has not landed. Finding 5 is a measurement correction. Finding 6 is the
CI pair that is red because of finding 2, not an external blocker. Finding 8 is
recorded rather than changed.

## Findings

| # | Severity | Finding | Resolution | Evidence |
| --- | --- | --- | --- | --- |
| 1 | BLOCKING (process) | No `CLAUDE.md` was updated although the branch touches `lib/furble/` and `tests/host/`. `AGENTS.md` and `CLAUDE.md` require every PR to update each directory guide it touches, and the MTU helper, the profile-over-advertisement gate and the mock's MTU model are contracts a later change can break silently. | Fixed in `370bba46`. `lib/furble/CLAUDE.md` gained the MTU contract (raise only, bounded poll, read at each use, poll `connectCancelled()` on the connect path) and the gate rule; `tests/host/CLAUDE.md` gained the mock MTU model, `mockAttWriteRejections()` and the heap-charging warning. | Doc diff. `python3 tools/pre_review.py --base origin/master` passes 10 gates and `sh sim/scripts/check-doc-tokens.sh` passes. |
| 2 | BLOCKING (CI) | The branch does not fit the `m5stack-core-debug` slot. The merge build at `48fe3096` is 1,741,293 bytes against 1,740,800. Measured against the same job on the branch's base, the branch adds 1,212 bytes to a 719-byte margin. | Open, and deliberately not trimmed. The lane recovers with PR #518 (plan 347), which measures a 25,392-byte drop on the same env. This PR waits for it and then rebases. | `gh run view -j 139736753716`: `Flash: [==========] 100.0% (used 1741293 bytes from 1740800 bytes)`. Base `7caf0468`, job 139730672773: `used 1740081 bytes`. Delta 1,212. |
| 3 | NON-BLOCKING | The plan claimed "nothing that works today changes behaviour". The saved-record row reaches `BestEffort` because a record carries no advertisement, so an already-paired body without DD30/DD31 switches from "no location" to a direct DD11 write while its first pairing took `Strict`. | Fixed in `370bba46` as a stated behaviour change with its reason, and recorded as follow-up 4 (persist the generation in a versioned record). Code unchanged. | Plan diff, "Why not a per-body flag". |
| 4 | NON-BLOCKING | A retryable failure was treated as a capability refusal: `updateGeoData` set `m_GeoUnsupported` on the first failed DD11 write in `BestEffort`, so one application error suppressed location for the rest of the session. This is the "skip-once must not silently never send" case. | Fixed on the branch: `m_GeoFailures` counts consecutive refusals, clears on a successful write, and `m_GeoUnsupported` is set only at `GEO_FAILURE_LIMIT` (3). A busy body is retried on the next tick; a body that refuses the packet stops after three writes. The precise ATT-status distinction stays as follow-up 5. | Red first: with the counter reverted, `sony_geo_test` prints `FAIL: a busy write is retried on the next tick`, `FAIL: the retried write delivers the location` and `FAIL: the refusing body was tried the bounded number of times`. Green after the fix: `sony_geo_test` exits 0. |
| 5 | NON-BLOCKING | The first pass recorded `sizeof(NimBLEClient)` as `208` at the base, `216` at the pre-fix commit and `208` at the head, measured on arm64. The `216` does not reproduce on this ABI: it is `208` at `7caf0468`, `60c51302`, `bab2304d`, `48fe3096` and `370bba46`. | Corrected. The 8-byte drop the Linux `sim-e2e` job saw is an ABI difference (libstdc++ member layout), not an arm64 measurement. The fix still holds and the operative evidence is the Linux jobs, which pass at the head. Documented in `tests/host/CLAUDE.md`. | `sizeof` probe compiled against `lib/testing/nimble/MockNimBLE.h` from each commit: `208 208 208 208 208`. |
| 6 | BLOCKING (external, as recorded) | The first pass recorded master as "32 bytes over the same slot since #501", so both red checks were reported as red for every PR. That is not what CI shows. | Corrected. In CI master is under the slot and green: 1,740,081 at `7caf0468` (job 139730672773) and 1,740,265 at `c644f39e` (job 139824607115). The branch's own 1,212 bytes is the overflow, and the 25,392-byte PR #518 is the headroom that changes the outcome. | Both master jobs named above report their size and both PlatformIO runs are green; `gh api repos/MaxRink/furble/commits/c644f39e/check-runs` lists no failure. |
| 7 | NON-BLOCKING | Half a gate was read as no gate. The gate condition was `!allow \|\| !enable`, so a profile that exposed only DD30 or only DD31 fell to `BestEffort` for the `Legacy` and `Unknown` generations and wrote DD11 ungated. This is the one path in the new logic that can send an ungated location packet to a body whose profile says it has a gate. | Fixed: only `!allow && !enable` reaches `BestEffort`, and a partial profile keeps `Strict` with a warning. `SonyVirtualCamera` now models the two characteristics separately (`location_allow_present`, `location_enable_present`) so the case is tested. | Red first: with the old condition, `sony_geo_test` prints `FAIL: the DD00 profile selects the expected location gate` and `FAIL: no location write is attempted` for the new partial-profile case. Green after the fix. |
| 8 | NON-BLOCKING | `SonyVirtualCamera` gates the DD11 accept path on the same per-characteristic flags now, so a peer with only DD30 refuses a packet for the missing lock and a peer with only DD31 refuses it as not enabled. That is stricter than any captured body and only affects tests. | Recorded. No production behavior depends on it. | Peer diff and `sony_geo_test` case 3b. |

## The red checks before the rebase, and whether they were real

The pre-rebase head was `bab2304d`. Six checks were red there: four named
failures plus the two size checks. All four were real, and two were production
defects, not test noise.

| Check | Was it real | Reproduction | What fixed it |
| --- | --- | --- | --- |
| Host camera protocol tests | Yes. `tests/camera/camera_protocol_tests.cpp` still asserted that tag `0x22` bit 1 is required, which is the rule the PR removes. | At `bab2304d`: `check failed at line 194`, `FAIL Cross-vendor advertisement parsing`. | `dd5f17aa` replaces the case. It is not a deletion: `0x42` and `0x82` stay independent negative cases for bits 7 and 6, and `0xc0` and `0xc8` must match and report `SonyGeneration::Legacy`. The head prints `PASS Cross-vendor advertisement parsing` and `All camera protocol tests passed`. |
| Python tooling tests (`test_connect_cancel_contract`) | Yes, and a production defect. `verifyAttMtu` waited in 10 ms slices on the connect path, which holds `Camera::m_Mutex`, without polling the cancel token, so a user disconnect could not abort the connect. | At `bab2304d`: `Lists differ: ['lib/furble/Camera.cpp:1233', 'lib/furble/Camera.cpp:1221'] != []`. | `48fe3096` polls `connectCancelled()` in the same loop body, which is the plan 148 contract. The head prints `Ran 8 tests ... OK`, and the contract test itself was not modified in this branch. |
| sim-e2e and sim-sanitizers (`hw-memory-webui-first-boot`) | Yes, but in the double. The mock client's member layout put `heap.free` 8 bytes under the floor on the Linux runner, and no scenario or floor was edited. | At `bab2304d`: `ASSERT_MIN FAILED: heap.free expected >= 22319 got 22311`. | `48fe3096` moves `m_AttMtu` into existing padding. `git diff 7caf0468 48fe3096 -- sim/` is empty, so nothing was re-baselined. Both Linux jobs pass at the head. |
| build (m5stack-core-debug) and the aggregate success | Yes, and it is this branch's own 1,212 bytes, not a tree-wide regression. | Job 139736753716: 1,741,293 against 1,740,800. | Not fixed. It needs PR #518. See findings 2 and 6. |

## What was attacked and found sound

Every point below was re-derived in the second pass rather than accepted from
the branch's own prose.

- **The matcher cannot accept a non-Sony device.** `matchesSonyAdvertisement`
  keeps the company `0x012d` and type `0x0003` gates and still requires tag
  `0x22` bits 7 and 6. Only bit 1 moved from required to classifying, so the
  accepted set is a strict superset *inside* Sony camera advertisements, and a
  device of another vendor cannot reach it. `sony_advertisement_test` pins an
  alien company id, an alien type, a null buffer, a 12-byte buffer, bit 7 clear,
  bit 6 clear, both clear and `0x00`, and pins that the out-parameter is
  untouched when an advertisement is rejected.
- **The MTU helper cannot lower a larger preference.** `requestAttMtu` returns
  before calling `setMTU` when the local preference is already at or above the
  floor, so DJI Osmo's 517 survives, and ATT keeps the negotiated value per
  connection so `setMTU` cannot shrink a live link. `sony_geo_test` case 4
  covers the raise (23 in, 158 out); the 517 case is covered by inspection of
  the early return and by `DJIOsmo.cpp:137`.
- **A below-floor link is skipped, not latched.** The MTU check runs on every
  `updateGeoData` call and re-reads `attMtu()`, so a late exchange completion
  starts the writes. Only the log line is sticky (`m_GeoReported`).
  `sony_geo_test` case 3 asserts no write, the session still connected, and no
  retry on the next tick.
- **The location gate comes from the discovered DD00 profile.** `_connect`
  probes DD30 and DD31 on the service it discovered and the advertisement only
  breaks the tie for a body that advertised the current generation. Case 6 pins
  that a legacy advertisement with the pair present still takes `Strict`.
- **The mock changes did not weaken another camera suite.** No existing host
  test file changed except the cross-vendor matcher, which gained assertions,
  and no simulator scenario changed. The new write-size gate is a new failure
  mode for every suite, not a removed one, and a peer that does not model a
  narrow body answers 517, so its link settles at the local 256-byte preference.
  `NimBLEClient::connect` gained the real signature's defaults, verified against
  `components/esp-nimble-cpp/src/NimBLEClient.h:57`.
- **The mock now matches the pinned stack.** `NimBLEDevice::getMTU()` is
  `ble_att_preferred_mtu()`, `NimBLEClient::getMTU()` is `ble_att_mtu(handle)`,
  and `connect()` issues the exchange from the connect-established path when
  `exchangeMTU` is true (`NimBLEClient.cpp:1195-1196`), which is what the mock
  models.

## Local gates on this head

```
$ cmake --build /tmp/b508-host --target sony_geo_test sony_advertisement_test sony_location_test
$ ./sony_geo_test && ./sony_advertisement_test && ./sony_location_test       # all exit 0
$ /tmp/b508-camera/camera_protocol_tests                                     # All camera protocol tests passed
$ python3 -m unittest tests.test_connect_cancel_contract                     # Ran 8 tests, OK
$ python3 tools/pre_review.py --base origin/master                           # PASS: 10 gates
$ sh sim/scripts/check-doc-tokens.sh                                         # tokens present
```

The full host suite ran for this record: `ctest` reports 200 of 200 tests
passed, including `sony-geo`, `sony-location`, `sony-advertisement` and the
boundary and control suites. The Python tooling suite is the CI job of that
name; the two modules this change touches are `tests.test_connect_cancel_contract`
(Ran 8 tests, OK) and the six pre-review scanner modules, which the
`pre_review.py` gate runs above.
