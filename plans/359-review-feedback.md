# 359 review feedback

An adversarial review of `fix/316-sony-7rm4` (PR 508) against its own claims. It
was asked to break six statements: that discovery classifies bit 1 rather than
requiring it, that the matcher still cannot accept a non-Sony device, that the
MTU helper cannot lower a link that already has a larger MTU, that a below-floor
link is skipped rather than failing every GPS tick, that the location gate comes
from the discovered DD00 profile, and that the mock NimBLE changes did not weaken
another camera suite. Findings 1 and 2 were fixed on the branch. Findings 3 to 5
are recorded rather than changed, with the reason stated. Finding 6 is the CI
blocker and is not this branch's to fix.

## Findings

| # | Severity | Finding | Resolution | Evidence |
| --- | --- | --- | --- | --- |
| 1 | BLOCKING (process) | No `CLAUDE.md` was updated although the branch touches `lib/furble/` (`Camera.cpp`, `Camera.h`, `Sony.cpp`, `Sony.h`, `protocol/AdvertisementProtocol.*`) and `tests/host/`. `AGENTS.md` and `CLAUDE.md` require every PR to update each directory guide it touches. The ATT MTU helpers, the profile-over-advertisement gate and the mock's new MTU model are all contracts a later change can break silently. | Fixed. `lib/furble/CLAUDE.md` gained the MTU helper contract (raise only, bounded poll, read at each use, poll `connectCancelled()` on the connect path) and the "a discovered characteristic decides, the advertisement only breaks the tie" rule with the saved-record consequence. `tests/host/CLAUDE.md` gained the mock's MTU model, `mockAttWriteRejections()` as the only observable, the 517 peer default, and the heap-charging warning about `sizeof(NimBLEClient)`. | Doc diff. `python3 tools/pre_review.py --base origin/master` and `sh sim/scripts/check-doc-tokens.sh` pass. |
| 2 | BLOCKING (CI) | The branch does not fit the `m5stack-core-debug` app slot. The PR head builds at 1,741,293 bytes against 1,740,800, so `build (m5stack-core-debug)` and the aggregate `success` check are red. This is not only the tree-wide overflow: the branch adds 1,028 bytes it compiled itself, because master measured 1,740,265 at `f1fa36a0`, whose only later commits on the base path are docs and simulator changes. | Open, and deliberately not trimmed. Master is 32 bytes over the same slot since #501 and a separate change is reclaiming at least 1 KiB there. The branch is whole and waits for that; see finding 6. | `gh run view` on job 139736753716: `Flash: [==========] 100.0% (used 1741293 bytes from 1740800 bytes)` and `Error: The program size (1741293 bytes) is greater than maximum allowed (1740800 bytes)`. The local rebuild of the same tree reports 1,741,041, so the delta, not the absolute number, is what the toolchain comparison can use. |
| 3 | NON-BLOCKING | The plan claimed "nothing that works today changes behaviour" and that `BestEffort` requires the pair to be absent *and* the body not to have advertised the current generation. The saved-record row satisfies the second condition vacuously, because a record stores no advertisement, so an already-paired current-generation body without DD30/DD31 switches from "no location" to a direct DD11 write on every reconnect while its first pairing took `Strict`. | Fixed in the plan as a stated behaviour change with the reason it is accepted (a versioned vendor record is needed to carry the generation) and recorded as follow-up 4. The code is unchanged: the per-body alternative is worse, and the profile is still the fact the code needs. | Plan diff, "Why not a per-body flag". |
| 4 | NON-BLOCKING | A retryable failure is treated as a capability refusal. `updateGeoData` sets `m_GeoUnsupported` on any failed DD11 write in `BestEffort`, not only on a refusal, so one application error suppresses location for the rest of the session. | Recorded as follow-up 5. Not changed here: `Camera::gattWrite` returns a bool with no ATT status, the cheap version is a consecutive-failure counter, and this lane has no flash to spend (finding 2). Behaviour is logged once per session and recovers on the next connect. | Host probe against the virtual peer: `gate=1`, `after busy tick: writes=1 connected=1`, `after 2 clean ticks: writes=1 connected=1`. The peer answered `Fault::Busy`, an ATT application error with the link up, and the cleared fault never got a retry. |
| 5 | NON-BLOCKING | `sizeof(NimBLEClient)` is charged against the modelled device heap by `sim/heap_count.cpp`, so the new `m_AttMtu` field moved three calibrated `hw-memory-*.txt` floors until it was placed in existing padding. That fix is invisible: the next field that does not fit the padding breaks the same scenarios. | Documented in `tests/host/CLAUDE.md` with the measurement rule. Measured: 208 bytes at the base, 216 at the pre-fix commit, 208 at the head, on arm64; the Linux `sim-e2e` job passes, which covers the second ABI. | `sizeof` probe: `208`, `216`, `208` for `7caf0468`, `60c51302`, `48fe3096`. |
| 6 | BLOCKING (external) | Master itself is 32 bytes over the same slot since #501, so both red checks are red for every PR. After a 1 KiB reclaim the branch would still be about 36 bytes over, because its own cost is 1,028 bytes. | Reported, not worked around. A partial trim of the feature to absorb a tree-wide regression would hide the real budget problem; if the reclaim lands short, the shortfall is reported with these numbers rather than papered over. | 1,740,832 (master, over by 32) - 1,024 (a 1 KiB reclaim) + 1,028 (this branch) = 1,740,836. |

## The red checks before the rebase, and whether they were real

All four were real. Two of them were in this branch's own production path.

| Check | Was it real | Reproduction | What fixed it |
| --- | --- | --- | --- |
| Host camera protocol tests | Yes. `tests/camera/camera_protocol_tests.cpp` still asserted that tag `0x22` bit 1 is required, which is the rule the PR removes. | At `60c51302` (the first commit): `check failed at line 194: !Furble::AdvertisementProtocol::matchesSonyAdvertisement(...)`, then `FAIL Cross-vendor advertisement parsing`. | `dd5f17aa` replaces the case. The fix is not a deletion: `0x42` and `0x82` stay independent negative cases, and `0xc0` and `0xc8` must now match *and* report `SonyGeneration::Legacy`. The head prints `PASS Cross-vendor advertisement parsing` and `All camera protocol tests passed`. |
| Python tooling tests (`test_connect_cancel_contract`) | Yes, and it was a production defect. `verifyAttMtu` waited in 10 ms slices while `Camera::connect()` holds `Camera::m_Mutex`, without polling the cancel token, so a user disconnect could not abort a connect. | At `60c51302`: `Lists differ: ['lib/furble/Camera.cpp:1233', 'lib/furble/Camera.cpp:1221'] != []`. | `48fe3096` polls `connectCancelled()` in the same loop body, the plan 148 contract. The head prints `Ran 8 tests ... OK`. |
| sim-e2e and sim-sanitizers (`hw-memory-webui-first-boot`) | Yes, but in the double, not in production. The mock client grew from 208 to 216 bytes and the simulator charges a live client's host size against the modelled heap, so `heap.free` fell 8 bytes under the floor. | `sizeof` probe at `60c51302`: `sizeof(NimBLEClient) = 216`. | `48fe3096` moves `m_AttMtu` into the padding before `m_ConnectTimeout`, back to 208. No scenario and no floor was edited: `git diff 7caf0468 48fe3096 -- sim/` is empty. |

## What was attacked and found sound

- **The matcher still cannot accept a non-Sony device.** `matchesSonyAdvertisement`
  keeps the company `0x012d` and type `0x0003` gates and still requires tag
  `0x22` bits 7 and 6, which is the paired-and-connectable pair. Only bit 1 moved
  from required to classifying. `sony_advertisement_test` pins an alien company
  id, an alien type, a null buffer, a 12-byte buffer, bit 7 clear, bit 6 clear,
  both clear and 0x00, and separately pins that the out-parameter is untouched
  when the advertisement is rejected. The widening inside Sony is by design and
  is a mode byte, not an identity: any Sony device that already sends company
  `0x012d`, type `0x0003` and both high bits is now listed, which is exactly the
  set the 2019 bodies belong to.
- **The MTU helper cannot lower a larger preference.** `requestAttMtu` returns
  early when the local preference is already at or above the floor, so DJI Osmo's
  517 is untouched, and `setMTU` cannot reach an existing link at all because ATT
  keeps the negotiated MTU per connection. Measured against the virtual peer:
  `default preferred=256 negotiated=256`, `dji 517 preferred=517 negotiated=517`,
  `low 23 preferred=158 negotiated=158`. The third line is the raise working.
- **A below-floor link is skipped, not retried into a failure, and the skip is not
  a latch.** The MTU check in `updateGeoData` runs on every call and writes
  nothing while the link is narrow, so a late exchange completion starts the
  writes; only `m_GeoReported` is sticky, and it gates the log line, not the
  write. `sony_geo_test` case 3 asserts one attempt is absent, the session stays
  connected, and the second tick does not retry.
- **The mock changes did not weaken another camera suite.** No existing host test
  file is modified except the cross-vendor matcher, which gained assertions, and
  no simulator scenario is touched. The new write-size gate is a new failure mode
  for every suite rather than a removed one, and a peer that does not model a
  narrow body answers 517 so its link settles at the local 256-byte preference.
