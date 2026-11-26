# 359 - Sony 2019 generation: discovery, ATT MTU, location (upstream issue 316)

> Renumbered from 345. Plan 345 is the plans-cleanup document opened by PR 484,
> which was in flight and not in this branch's base, and 346 to 358 are claimed
> by other open pull requests, so this plan took the next free number, 359. The
> branch's code, docs and commit messages were updated with it.

## Motivation

Upstream issue 316 reports a Sony ILCE-7RM4 (2019) that is not discoverable,
refuses pairing and never receives location data. A triage identified three
causes. All three were re-verified in this tree before any change.

| Cause | Verified location | What it did |
| --- | --- | --- |
| Discovery | `lib/furble/protocol/AdvertisementProtocol.cpp:73` | `matchesSonyAdvertisement` required tag `0x22` bits 7, 6 **and 1**. A 2019 body advertises `0xE8` or `0xEC`, which clears bit 1, so `Sony::matches` answered false and the body never reached the camera list. |
| ATT MTU | `lib/furble/Sony.cpp:223` | The DD11 location packet is one 91 or 95 byte write, which needs an MTU of at least 98 with the three-byte ATT header. No path verified or respected the negotiated MTU. DJI Osmo raised the local preference to 517; nothing else asked for anything, so a link at the 23-byte ATT default could not carry the packet and the failure was invisible. |
| Location | `lib/furble/Sony.cpp:185` | `updateGeoData` was gated on `locationEnabled()`, which reads DD30 (allow) and DD31 (enable). A body that does not expose the pair returned false forever, so no location was ever written. |

The issue also asked about a shared defect: `Camera::connect` returning false
while the client is connected (`lib/furble/Camera.cpp:610` and `:641`). Section
"Shared defect" below records what it actually is.

## Known advertisement values

| `0x22` | Bits 7, 6 | Bit 1 | Body | Evidence |
| --- | --- | --- | --- | --- |
| `0xE8` | set | clear | 2019 generation: the ILCE-7RM4 in upstream issue 316 | Triage report; spec note U-31 records that bit 1 may be clear. No raw capture in this project. |
| `0xEC` | set | clear | 2019 generation, same report, bit 2 also set | Triage report. |
| `0xE2`, `0xEE`, `0xF2` | set | set | 2020 and later: ZV-1F, ILCE-7RM5 generation | Modelled by `SonyVirtualCamera` from its tag flags. `0xE2` is pairing plus location, `0xEE` adds bit 2, `0xF2` adds an enabled location. No raw advertisement of a current body is in the corpus. |

Bit 1 is not a camera/no-camera bit. The body is a camera whenever bits 7 and 6
are set; bit 1 separates the two generations. Bits 7 and 6 stay required, so a
byte that clears either is still rejected.

## Design

### Discovery

- `AdvertisementProtocol::SonyGeneration` (`Current`, `Legacy`) classifies a
  parsed advertisement by bit 1.
- `matchesSonyAdvertisement(data, bytes)` keeps its signature and accepts both
  generations. The new overload takes a `SonyGeneration &` out-parameter, written
  only on a match, so `Sony` can record what it saw. The two-argument form
  delegates, so there is one matcher and no second rule set.
- `Sony` stores `Generation::{Unknown, Current, Legacy}`. A fresh pair parses
  the advertisement. A saved record has no advertisement (the vendor record
  holds name, address, type and our UUID) and Sony does not scan on reconnect,
  so it stays `Unknown` and the discovered GATT profile decides instead.

### ATT MTU

Two facts changed what the fix had to be, and both belong in the record:

1. `NimBLEDevice::setMTU` was already used by `DJIOsmo::_connect`, which raises
   the local preference to 517 before connecting. The tree did know about MTU,
   for exactly one vendor, and it never checked what the body answered.
2. The pinned `components/esp-nimble-cpp` 2.5.0 `NimBLEClient::connect` takes an
   `exchangeMTU` argument that defaults to true and issues the exchange from its
   connect-established path (`NimBLEClient.cpp:1195-1196`). Every board
   sdkconfig pins `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256`. So the request was
   already being made on every link and the missing piece was the floor, the
   verification and the write decision. The plan does not claim the stack never
   negotiated.

The two halves live in shared code, in `lib/furble/Camera.cpp`, because the ATT
MTU is a property of the link and the local preference is a process-wide
resource, not a vendor one:

- `Camera::requestAttMtu(floor)` raises `NimBLEDevice::getMTU()` to the 158-byte
  floor before a connect. It only raises: DJI Osmo's own 517 request survives.
  It must run before the link, because that is when the exchange happens.
- `Camera::verifyAttMtu(floor)` polls `NimBLEClient::getMTU()` in 10 ms slices,
  up to 200 ms, and reports what the link settled on. The poll is required: the
  exchange is queued and answered on the host task, so a single read would
  report the 23-byte default on real hardware even when the body answered
  correctly. The wait uses `vTaskDelay` where FreeRTOS is present and the wall
  clock otherwise, the same `__has_include` rule `Fujifilm.cpp` already uses for
  its registration wait, so the simulator stays on its virtual clock.
- `Camera::attMtu()` exposes the live value. It is read again at each use
  instead of cached, because a late completion raises it mid-session.

Sony calls `requestAttMtu` before `m_Client->connect()` and `verifyAttMtu` right
after it returns.

Fragmentation was rejected. The DD11 payload is one fixed-format packet whose
length field (`0x5d` or `0x59`) covers the whole body, no captured exchange shows
the camera reassembling a long or prepared write, and two ATT writes would
deliver a truncated packet to the vendor parser. A link that cannot carry
`payload + 3` therefore gets no location at all, reported once per session, and
the shutter path keeps working. The other families are untouched: they never
called an MTU API, and their own writes are unchanged at the negotiated 256
(Fujifilm's 23-byte geotag, Ricoh's and LUMIX's structures).

### Location

Chosen: capability **detection from the discovered profile**, with the
advertisement generation as the tie-breaker. Not a per-body static flag.

`Sony::_connect` probes the DD00 service for DD30 and DD31 after discovery and
picks a gate:

| DD30 + DD31 | Generation | Gate | Behaviour |
| --- | --- | --- | --- |
| both present | any | `Strict` | The existing allow/enable handshake, unchanged. The 7RM5 firmware confirms the pair. |
| both absent | `Legacy` | `BestEffort` | DD11 is written directly. |
| both absent | `Unknown` (saved record) | `BestEffort` | DD11 is written directly. |
| both absent | `Current` | `Strict` | Today's behaviour, plus a warning. A current body is expected to expose the pair; a short discovery does not become an unproven write. |
| exactly one present | any | `Strict` | Half a gate is not an absent one. A profile that exposes only DD30 or only DD31 still describes a body with the handshake, so a short discovery keeps the strict path, which sends nothing while the missing characteristic is missing, and logs a warning. Review finding 7 fixed this; only the both-absent rows reach `BestEffort`. |

Why not a per-body flag:

- Furble cannot identify the model. `Sony::matches` does not retain or decode the
  two model bytes, Sony's model codes for ILCE bodies are not documented in the
  corpus, and a saved record carries no advertisement at all. A static table
  would be a guess that also has to be maintained per firmware release.
- The GATT profile is the fact the code actually needs. A body either has the
  allow/enable pair or it does not, and firmware updates change characteristics,
  not model names.
- The table above is conservative where it can be, but one row is a deliberate
  behaviour change and is stated as one rather than folded into "nothing that
  works today changes behaviour" (that earlier wording was wrong; review finding
  3 in `plans/359-review-feedback.md`). A body that exposes no DD30/DD31 receives
  no location today. After this change it receives a direct DD11 write on every
  session whose gate is `BestEffort`, which for an already-paired camera is the
  saved-record row: the record stores no advertisement, so the generation is
  `Unknown` and the profile decides. The first pairing of the same body can take
  the `Strict` row instead when it advertised the current generation. The two
  sessions therefore differ, and that difference is accepted because the
  alternative is to persist the generation in the vendor record, whose layout is
  validated by `sizeof` on load and cannot gain a field without a migration. It
  is recorded as follow-up 4.

`BestEffort` is honest about failure. A refusal is not treated as a capability
gap on the first answer: `m_GeoFailures` counts consecutive refused DD11 writes,
the count clears on a successful write, and `m_GeoUnsupported` is set only after
`GEO_FAILURE_LIMIT` (3) refusals, so an application error such as a busy body is
retried on the next GPS tick while a body that refuses the packet format stops
after three writes instead of one per tick. Both the too-narrow link and the
refusal are logged once per session (`ESP_LOGW` for the MTU, `ESP_LOGI` for a
camera refusal). Nothing reaches the user as an error, and the shutter is
unaffected. The release writes on disconnect are skipped in best-effort mode
because the body has no DD30/DD31 state to release.

## Shared defect: `Camera::connect` return value

`git log -L` shows `return connected;` became `return m_Connected;` in
`fbe15456` (Migrate UI to LVGL, #143). The literal report is that `connect()`
returns false while the client is connected, citing `:610` (`bool connected =
this->_connect();`) and `:641` (`liveClient`).

It does not reproduce as a reachable disagreement in that function. Every path
that returns false also forces `m_Connected = false` at `:667`, and every path
that returns true leaves `m_Connected` untouched. So `return connected &&
m_Connected` is byte-for-byte equivalent to `return m_Connected`, and the return
statement is not where a fix would go. The Nikon Smart path is the visible
instance of the real residual: `NikonSmart::connectFinalise` returns false by
design on a *live* link, so the attempt reports failure while the link is up, and
the shared code then tears it down.

The residual inconsistency is the forced `m_Connected = false` at `:667`: when a
vendor `_disconnect()` does not complete its terminate (a stalled
`ble_gap_terminate`, or the deferred-delete model), the camera reports
disconnected while the client is still connected until the supervision timeout.
Changing that means reordering the reclaim and self-delete sequence that plans
148, 173 and 302 and the host lifetimes suites (`client_leak_test`,
`connect_mid_drop_test`, `control_reclaim_uaf_test`, `control_teardown_wedge_test`,
`control_zombie_cancel_test`) pin. It is not contained in a Sony discovery fix,
so it is a ranked follow-up rather than part of this change. No production code
in this plan depends on it.

## Virtual peer coverage

`lib/testing/peer/SonyVirtualCamera` is the only way any of this is testable
without a body, so the model was extended:

- `Config::legacy_bit1` now means what the matcher means: the mode byte's bit 1.
  `centralModelGate()` mirrors the production matcher (bits 7 and 6).
  `legacyGeneration()` reports the modelled generation.
- `SonyVirtualCamera::legacyBody()` is the 7RM4 shape: tag `0x22` bit 1 clear,
  DD00 with DD11 and DD21, no DD30 and no DD31, and DD11 accepted without a lock
  or an enable step (`Config::location_allow_present` and
  `Config::location_enable_present`, both clear). The two are separate flags so
  a partial profile can be modelled and tested.
- `Config::max_att_mtu` (default 517) is the server-side MTU the peer answers an
  exchange with. `negotiateAttMtu`, `requestedAttMtu`, `negotiatedAttMtu`,
  `mtuExchangeCount` and `maxWriteLength` let a test see the request and the
  result.

The mock needed new plumbing for this, because it had no MTU at any layer:

- `NimBLEDevice::setMTU/getMTU` now record and report a local preference
  (default 256, matching the sdkconfigs). `NimBLEDevice::setMTU` used to return
  true without storing anything, so DJI Osmo's 517 had no observable effect.
- `NimBLEClient::connect` gained the real signature's
  `delete_attributes`/`async_connect`/`exchange_mtu` arguments with the same
  defaults, and starts every link at the 23-byte ATT minimum.
- `NimBLEClient::exchangeMTU()` settles on `min(local preference, peer answer)`
  and completes inline. `NimBLEClient::getMTU()` and `maxWriteLength()` expose
  the result.
- `NimBLEMockPeer::negotiateAttMtu(requested)` is a new virtual with a default
  answer of 517, so the other seven peers are unchanged and stay at the 256
  local preference.
- The write path (`NimBLERemoteCharacteristic::writeValue`, `NimBLEClient::setValue`)
  refuses a value above `MTU - 3` and counts it in
  `NimBLEDevice::mockAttWriteRejections()`. That counter is the only observable
  for the real host-side `BLE_HS_EMSGSIZE` failure: the write never reaches the
  peer, so the peer journal cannot show it.

## Red-first evidence

Each fix was written against a test that failed first. The failures below are the
recorded runs on this branch, with the production side reverted and the peer
model kept.

| Fix | Test | Red (production reverted) | Green (with the fix) |
| --- | --- | --- | --- |
| Discovery | `sony-advertisement` | `mode22=0xe8 matched=0`, `0xec matched=0`, `0xc8 matched=0`, only `0xee matched=1`; `FAIL: a 2019 Sony body is rejected by the matcher` | every generation matched, `Legacy`/`Current` reported per case |
| MTU | `sony-geo` | `FAIL: the exchange requests at least the ATT MTU floor`, `FAIL: the negotiated link can carry the 95-byte packet`, `FAIL: the location write fits and is accepted`, `FAIL: the location write was never rejected for size` | 0 failures |
| Location | `sony-geo` | `FAIL: the DD00 profile selects the expected location gate`, `FAIL: the location update reaches the peer`, `FAIL: the legacy body receives the 95-byte location form`, `FAIL: the location write fits and is accepted`, `FAIL: the refused write was attempted once` | 0 failures |

## Implementation state

Done on `fix/316-sony-7rm4`, rebased onto `b711a44a` for the results below.

| Gate | Result |
| --- | --- |
| `sony-advertisement` (new) | pass |
| `sony-geo` (new) | pass |
| `sony-location` (existing 91/95-byte form suite) | pass |
| `camera-regression` | pass |
| `peer-contract` | pass |
| `fujifilm-camera` (23-byte geotag through the new MTU gate) | pass |
| `advertisement-dispatch`, `client-leak`, `connect-mid-drop`, `camera-pairing-peer` | pass |
| `canon-time`, `ricoh-time`, `fujifilm-protocol` | pass |
| `python3 tools/pre_review.py --base origin/master` | pass: whitespace, diff hygiene, settings contracts (6 pre-existing warnings, 0 failures), clang-format (12 changed files), C++ safety, CI workflow contract, simulator scenario manifest, simulator hardware coverage, coupling ratchet, 140 scanner unit tests |
| `tools/check_portability_inventory.py`, `tools/check_hot_path_logs.py` | pass. The portable core contract is unchanged and the hot-path gate leaves its three guarded functions untouched; the new location logs are once per session behind `m_GeoReported`, not per GPS tick. |
| `sh sim/scripts/check-doc-tokens.sh` | pass: `sim documentation tokens are present` |
| Full host `ctest` | not run. The host was saturated with other sessions' PlatformIO and simulator builds, so the whole 198-test suite was not driven; the complete host build succeeded and the thirteen suites above, which drive the changed seams (the Sony path, `Camera::connect`, the mock write path and the peers), pass on it. CI is the confirmation. The remaining host targets link the mock but do not exercise the MTU, the Sony gate or the new peer surface. |

Untested on hardware, as with every non-Fujifilm vendor: only the virtual peer
covers this change.

## What still needs a real ILCE-7RM4

1. The raw advertisement through a full pair and a reconnect: confirm `0x22` is
   `0xE8` or `0xEC` and that bits 7 and 6 really are set on this body.
2. The DD00 profile: confirm DD11 and DD21 exist, DD30 and DD31 do not, and what
   DD21 byte 4 bit 1 reads, which selects 91 or 95 bytes. The 95-byte default is
   a guess for this generation because DD21 is the only source for the choice.
3. Whether DD11 is accepted with no allow/enable step at all, or answered with
   an ATT error. Furble now treats a refusal as a logged no-op, so the capture
   decides whether the direct write is worth keeping or needs a body-specific
   prelude.
4. The negotiated ATT MTU the body answers, and whether it accepts a 95-byte
   write on a 158-byte floor without a long write.
5. Whether the shutter, focus and FF01 opcodes behave the same on this
   generation, which no test in this change exercises against a body.

## Ranked follow-ups

1. `Camera::connect` outcome versus link state (section "Shared defect"). The
   forced `m_Connected = false` can disagree with a live client after an
   incomplete terminate, and a successful `_connect()` whose peer dropped in the
   last microseconds leaves the client attached with self-delete armed. Both are
   in the reclaim ordering that plans 148, 173 and 302 pin.
2. The family-wide MTU floor. Only Sony calls `requestAttMtu` and `verifyAttMtu`.
   Fujifilm's 23-byte geotag and Ricoh's and LUMIX's location structures already
   exceed the 20-byte ATT minimum, and they rely on the 256-byte local
   preference and the library's automatic exchange. A per-family
   `verifyAttMtu` would make that explicit, and each needs its own capture
   before it is claimed.
3. Sony's advertised model code is still not decoded. Retaining it would let the
   location gate and the 91/95-byte choice be derived from the body instead of
   from the profile, but only once the codes are documented.
4. The generation is not persisted, so the saved-record row takes `BestEffort`
   where the first pairing took `Strict` (see "Why not a per-body flag"). Closing
   it needs a versioned vendor record rather than a longer `sony_t`.
5. The precise ATT status is still not available. `Camera::gattWrite` returns a
   bool, so the bounded-retry fix cannot tell a retryable application error from
   a permanent one and treats both the same for `GEO_FAILURE_LIMIT` attempts.
   Plumb the ATT status through `Camera::gattWrite` and latch on a permanent
   class only. Review finding 4 was fixed short of this.
6. The image is 1,212 bytes larger in CI, measured as the difference between the
   `m5stack-core-debug` flash figure on this branch's merge build (1,741,293 at
   `48fe3096`) and the same job on its base `7caf0468` (1,740,081). That lane has
   719 bytes of slack at the base and 535 on master at `c644f39e`, so the branch
   is over the slot on its own. Size work belongs to the tree-wide Core debug
   recovery, not to a partial trim of this feature, so the branch stays whole and
   waits for that recovery. Review finding 6 records the arithmetic.
