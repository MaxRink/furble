# 159 - capture-backed camera peer certification

## Objective

Turn the simulator into a sound compatibility oracle for every camera path
implemented by furble. For an exact camera, firmware, board, configuration,
and furble revision, the oracle must say one of:

- `PASS_CERTIFIED`: the requested feature matched a complete hardware capture
  and its physical outcome.
- `FAIL_CERTIFIED`: the exact profile reproduced a captured incompatibility or
  a deterministic conformance violation.
- `UNCERTIFIED`: evidence is missing, inferred, synthetic, stale, or outside
  the captured feature envelope.

The simulator must never turn an assumption into a pass. This makes every
reported pass or fail trustworthy while being honest that an unobserved
hardware behavior cannot be proven by software alone.

Certification is feature-scoped. Advertising, first pairing, saved reconnect,
focus, shutter, Bulb, video, GPS, standby, and disconnect can have different
results for the same camera profile.

## Evidence rules

Every profile field carries one provenance class:

1. `hardware-capture`: raw HCI, over-air, ATT, GATT, power, or physical-outcome
   evidence from the exact model and firmware.
2. `official-semantic`: the camera maker documents the user-visible behavior,
   but not necessarily its private BLE bytes or timing.
3. `common-implementation`: behavior is independently exercised by a public
   implementation, but may be cross-model or reverse engineered.
4. `cross-model-inferred`: behavior is copied from a related camera or firmware.
5. `synthetic`: a deliberately invented fuzz or fault behavior.

Common implementations and official documentation should enrich every peer.
They are valuable for finding missing states, commands, negative cases, and
capture targets. Only `hardware-capture` may certify private bytes, ordering,
security, ATT results, or timing for an exact profile. Inferred and synthetic
fields force `UNCERTIFIED` for any feature that depends on them.

Pin imported sources to a commit and record their license. Initial sources
include:

- Fujifilm: [fffw](https://github.com/tiredboffin/fffw), including its captured
  GATT YAML and firmware-to-profile map.
- Canon: [canon-bluetooth-control](https://github.com/3bl3gamer/canon-bluetooth-control),
  [eos-remote-web](https://github.com/RReverser/eos-remote-web), and
  [CanonBLEIntervalometer](https://github.com/robot9706/CanonBLEIntervalometer).
- Sony: [freemote](https://github.com/coral/freemote),
  [AlphaRemote](https://github.com/Staacks/alpharemote), and
  [ILCE7M3ExternalGps](https://github.com/whc2001/ILCE7M3ExternalGps).
- DJI: DJI's [Osmo GPS Controller Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo).
- Ricoh: [ricoh-gr-bluetooth-api](https://github.com/dm-zharov/ricoh-gr-bluetooth-api).

No imported implementation is a wildcard compatibility guarantee. Conflicts
between sources remain explicit capture questions.

### Initial evidence ledger

The 2026-08-30 Sol reviews are design audits, not capture evidence. This
ledger records whether a claim has enough source identity to guide a peer. A
missing commit, license, or capture ID prevents import and certification.

| Area | Evidence currently identified | Pin and license status | Allowed use now |
| --- | --- | --- | --- |
| Fujifilm | `tiredboffin/fffw` GATT maps and firmware mapping | Commit and license must be pinned before import | Research and capture targeting |
| Canon | `3bl3gamer/canon-bluetooth-control`, `RReverser/eos-remote-web`, and `robot9706/CanonBLEIntervalometer` | First and third repositories have no confirmed repository license; all commits remain unpinned | Research-only semantic comparison; no code or fixture import without permission |
| Sony remote | `coral/freemote` and `Staacks/alpharemote` | Commits and licenses must be recorded before import; their shutter ordering reports conflict | Research-only, source-conflicting |
| Sony GPS | `whc2001/ILCE7M3ExternalGps` | Repository intentionally has no license | Research-only protocol hypothesis; no code or fixture import without permission |
| DJI | `dji-sdk/Osmo-GPS-Controller-Demo` at `92fe23e5a749f189593f980a26a105c3bb66aa1c` | Demo code says MIT while protocol documents are under DJI EULA and source headers contain mixed terms | Citation and independent reimplementation only until licensing is clarified |
| Ricoh older family | `dm-zharov/ricoh-gr-bluetooth-api` at `8c55b79928295f4c0f9a8b0f6f4e1015aeb3d016` | Unlicense; importable with provenance | Cross-model research only; every inherited field forces `UNCERTIFIED` |
| Ricoh GR IV HDF | [Issue 267 testimony](https://github.com/gkoh/furble/issues/267#issuecomment-4965515611), PR 270, and merge `ea84822e225585f6f257b6100799acd295d01bce` | Furble code is MIT; testimony is citation-only | Hardware smoke evidence only, not a byte-level fixture |
| Nikon Smart | [birdcam](https://github.com/attilaolah/birdcam/tree/93ffc86a85a474dd884e4ac0168a991c1fdb1822/nikon/coolpix) at `93ffc86a85a474dd884e4ac0168a991c1fdb1822`, [nsg protocol](https://github.com/hurui200320/nsg/blob/5a9117def8fad5b75771a52837562ae02b9c80c8/doc/nikon-z-gps.md) at `5a9117def8fad5b75771a52837562ae02b9c80c8`, and [Z50 II issue 257](https://github.com/gkoh/furble/issues/257) | birdcam is MIT but includes third-party-derived material; nsg is AGPL-3.0; issue output is citation evidence | Clean-room research only; no complete firmware-qualified public capture exists |
| Panasonic S5 | [lux-lat-long-log protocol](https://github.com/tobiasbrummer/lux-lat-long-log/blob/87e51686c8496ca20fa8b14433ba548383f8da5b/PROTOCOL.md) at `87e51686c8496ca20fa8b14433ba548383f8da5b` | MIT, importable with notice | Hardware report lacks raw HCI and exact firmware; useful only as an uncertified S5 template |
| Panasonic G9 II | [LUMIX-G9II-Remote-Control](https://github.com/Aikhjarto/LUMIX-G9II-Remote-Control/blob/25c91ebebd5d27ecf531405ee49b6d27e2c085d8/src/LumixG9IIRemoteControl/LumixG9IIBluetoothControl.py) at `25c91ebebd5d27ecf531405ee49b6d27e2c085d8` | GPL-3.0; clean-room reference only | No raw capture or firmware-qualified validation; must not inherit the S5 profile |

Before the first peer implementation PR, replace every unresolved ledger cell
with an immutable source commit or hardware-capture manifest and verify the
license. Material without a permissive license remains citation-only and must
not be copied into source, tests, or fixtures.

## Exact profile identity

A certifiable run binds all of the following:

```text
camera
  vendor, marketing model, exact GATT model string, firmware, region
  selected camera BLE role or application mode
  raw advertisement identity and GATT database digest

furble
  Git revision, build configuration, board, ESP-IDF and NimBLE revisions
  controller capabilities, persisted camera record, settings digest

fixture
  schema version, capture manifest, source commits, normalized trace digest
  raw capture digests, required feature corpus, calibration status
  capture operator, device identity, analyzer identity, and timestamp
  trusted signer, signature algorithm, signature, and trust-policy version
```

Any mismatch, wildcard, unknown firmware, family alias, missing source digest,
or incomplete required trace yields `UNCERTIFIED`.

## Simulator architecture

Plan 158 Phase 1 supplies one virtual clock and orderly worker lifecycle. This
plan depends on plan 158 Phase 2 running production `Control`, `Camera`,
`CameraList`, and `Scan` over MockNimBLE. Camera peers live below that boundary
and must not replace production policy.

Each peer uses shared components:

- immutable profile descriptor and exact raw advertisement sequence;
- exact services, handles, characteristics, descriptors, properties,
  permissions, values, MTU, PHY, and connection-parameter policy;
- camera-side bond and application-registration stores, separate from the
  furble-side store;
- GAP, SMP, ATT, subscription, indication-confirmation, power, and physical
  camera state machines;
- scheduled callbacks tagged with a connection generation so an old event
  cannot corrupt a reconnect unless a capture proves that behavior;
- ordered trace journal with raw values, status, initiator, and timing bounds;
- captured-failure overlays and separately labeled synthetic fault overlays.

The transport must enforce real characteristic properties and security. The
current mock behavior that makes every characteristic readable, notifiable,
indicatable, and writable is not eligible for certification. Notifications
and disconnects are scheduled events, never synchronous calls from `write()`.

## Capture and fixture format

Store raw captures separately from normalized fixtures. Secrets may be
redacted, but lengths, byte positions, association method, error codes, and
ordering must remain reproducible.

```text
tests/corpus/cameras/<vendor>/<model>/<firmware>/<role>/
  manifest.json
  advertisement.json
  gatt.json
  security.json
  timing.json
  features/<feature>/<case>.trace.json
  hashes.sha256
```

The normalized trace vocabulary is:

```text
ADV CONNECT CONN_PARAMS SECURITY DISCOVER READ WRITE
SUBSCRIBE INDICATE CONFIRM NOTIFY CAMERA_STATE PHYSICAL_OUTCOME DISCONNECT
```

Timing uses repeated captured samples or explicit min, median, p95, and max
bounds. Do not invent Gaussian delays or random failure percentages from a
single trace.

## Vendor peer designs

### Fujifilm Basic and Secure

Select profiles by model, firmware, and GATT hash. Import the pinned fffw GATT
maps instead of exposing one universal service superset. Model Basic tokens,
application-profile persistence, selected destination, and network standby.
Model Secure SMP numeric comparison, LTK and IRK persistence, RPA rotation,
status acknowledgement, profile-specific CCCDs, delayed registration, the
10-second geotag request cadence, and two-write shutter transactions.

Current strict-peer findings:

- Saved Secure reconnect can match the stable serial but connect to a stale
  RPA because production does not update the saved address.
- Numeric comparison is silently accepted instead of being confirmed by the
  user.
- Secure profiles differ materially. Some omit NOT8 and NOT10, while NOT3 can
  disconnect tested X-H2 family cameras.
- The synthetic X100VI golden file is not hardware evidence.

### Canon Smart and Remote

Use role-exclusive peers. A camera never advertises a fictional union of the
Camera Connect and BR-E1 services. Persist BLE bonding separately from Canon
application registration.

Remote state includes camera drive mode, autofocus result, capture count,
Bulb, video, two-second release, zoom capability, standby, and RF loss. A
strict peer requires a complete `8c,0c` click to start Canon Remote Bulb and a
second complete click to stop it.

Smart state follows the captured registration order: subscribe, identify,
wait for camera acceptance, send stable UUID and device identity, then commit.
A saved reconnect does not repeat new registration. GPS follows the captured
request, enable, acknowledgement, then 20-byte update sequence. Session state
must be generation-owned and UTC conversion must be timezone-independent.

### Nikon Remote and Smart

Use separate role peers. Remote models the `de00` service, paired indication
stages, shutter press `02 02`, release `02 00`, security, and saved reconnect.
Smart models its `2000` family handshake and Blowfish-derived registration,
then reports the captured requirement for a BR/EDR continuation explicitly.
The current build cannot complete that Classic Bluetooth continuation, so a
selected Smart path is a proven furble failure only where the exact preceding
BLE profile is captured. Family aliases remain uncertified.

### Sony

Start with development templates for legacy protocol `0x64` and modern `0x65`,
then certify only exact model and firmware instances. Model FF01 writes, FF02
notifications, reverse Device Name reads, Service Changed, link persistence,
and exact button order. Public implementations currently conflict on whether
half up or full up occurs first after full down. That sequence remains
source-conflicting and `UNCERTIFIED` until the exact model and firmware capture
resolves it. Model-specific focus, zoom, video, Bulb, and lens limits are not
interchangeable.

Location peers model DD21 packet capability, 89-byte and 95-byte variants,
DD30/DD31 locking, DD01 state, UTC and DST fields, and orderly disable. Legacy
cameras that make remote and location modes mutually exclusive must reject the
combined state.

### Panasonic Lumix

Profiles model rotating addresses, readiness notifications, camera identity,
and model-specific registration. The S5 flow requires both readiness
notifications before initialization. Newer bodies such as G9 II use a distinct
XOR challenge and cannot inherit the S5 profile. Persist identity evidence, not
only an OTA address. Exact firmware captures are required before either family
can certify.

### DJI Osmo

Model the official demo's framing, CRCs, sequence numbers, connection request,
single ATT write boundaries, notification status, wake state, record command,
MTU, and approximately 2 Hz status stream. The peer must expose captured
security behavior rather than accepting production's current assumption that
the link is bonded, encrypted, and authenticated. A protocol frame accepted by
ATT is not a physical recording outcome.

### Ricoh GR IV and GR IV HDF

Keep separate exact profiles even when both use the same firmware package.
Model raw advertisement frames, the official six-client registration limit,
actual SMP association, Operation Mode, capture writes and outcomes, power and
standby notifications, and GPS-to-EXIF results.

Current GPS UUID naming and the final byte of the 32-byte payload conflict with
older-family public implementations. Those fields remain uncertified until a
GR IV capture establishes their exact meaning. GR III behavior must never be
used as GR IV certification evidence.

## Production review leads for strict-peer reproducers

The 2026-08-30 reviews reported the following leads. None is an accepted defect
or certified incompatibility until the indicated pinned capture, sanitizer
log, or focused reproducer is added to the fixture evidence ledger. Track and
reproduce them before changing production code:

| Area | Finding | Required evidence |
| --- | --- | --- |
| Control | Stale connect results, callback lifetime, same-target reconnect, queue allocation, and release-on-reset defects | Production-stack generation and teardown traces |
| Canon | Remote Bulb sequence, Smart registration order, stale GPS enable state, ignored subscription results | Exact role and firmware traces with physical outcomes |
| Fujifilm | Stale RPA on Secure reconnect and missing numeric-comparison UI | RPA rotation and SMP captures |
| Sony | Missing FF02 confirmation, incomplete command ordering, and assumed GPS variant | Exact model and firmware command and location traces |
| Lumix | Missing readiness gates and address-only persistence | S5 and newer-family first-pair and reconnect traces |
| DJI | Unproven SMP requirements and MTU/write assumptions | Exact Osmo model and firmware HCI trace |
| Ricoh | Unproven GPS fields and broad family matching | GR IV and GR IV HDF capture plus EXIF outcome |
| Nikon | Smart needs unsupported BR/EDR continuation | Exact BLE and Classic transition trace |

## Implementation sequence

### PR 1: oracle and fixture foundation

- Add manifest schema, provenance types, result API, digest validation, and
  normalized trace journal.
- Define a signed chain of custody from raw capture through redaction and
  normalization to fixture digest. Pin trusted capture signers, capture device
  and analyzer identity, signature algorithm, key rotation, revocation, and
  trust-policy version. Same-repository hashes provide integrity, not hardware
  provenance.
- Make missing, malformed, unsigned, revoked, untrusted, or incomplete evidence
  fail closed.
- Add exact GATT property, permission, ATT error, MTU, subscription, and
  scheduled-callback support to MockNimBLE.
- Add mutation tests proving a missing identity field, inferred field, property
  mismatch, late generation callback, or physical-outcome gap cannot pass.

### PR 2: Fujifilm profiles

- Import pinned fffw GATT definitions with license and source digests.
- Implement Basic and Secure profile state machines, persistence, RPA, SMP,
  exact subscriptions, shutter, geotag, standby, and differential trace tests.

### PR 3: Canon profiles

- Implement role-exclusive Remote and Smart peers, separate registration
  stores, exact registration order, outcome-aware controls, and GPS state.

### PR 4: Sony and Nikon profiles

- Implement exact templates and fail-closed model registries, then add captured
  instances only when their complete corpus exists.

### PR 5: Lumix, DJI, and Ricoh profiles

- Implement distinct model families, physical outcome models, power states,
  and exact capture gates. Do not inherit protocol details across families.

### PR 6: hardware differential runner and release gate

- Drive one scenario DSL against a capture proxy and the virtual peer.
- Compare bytes, properties, security, ordering, disconnect initiator, timing
  bounds, and physical outcomes.
- Revoke a fixture when firmware, GATT, dependency, profile, or capture digests
  change.
- Publish a feature-level compatibility report containing no unqualified pass.

## Acceptance

- Production camera and Control sources run in the SDL simulator.
- Every declared supported exact profile has complete provenance and corpus;
  all other inputs return `UNCERTIFIED` with a machine-readable reason.
- Every captured positive and negative trace replays deterministically.
- Every required protocol mutation is killed by at least one conformance test.
- ASan, UBSan, and TSan cover callbacks, reconnect generations, persistence,
  peer power cycles, and teardown.
- A simulator `PASS_CERTIFIED` or `FAIL_CERTIFIED` includes the exact profile,
  furble revision, board, feature set, and fixture digest.
- No common implementation, official semantic, family inference, or synthetic
  fault is silently promoted to exact hardware evidence.

Until all of these gates pass, the simulator remains a development and
regression tool and must not claim universal camera compatibility.
