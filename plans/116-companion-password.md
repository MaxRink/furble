# 116 - Companion connection password (shared-secret auth gate)

Status: implemented by PR #166. See "Implementation state" below for the wire
contract, the two threat-model decisions, and what is still owed.
Firmware auth gate on the companion GATT service from
`plans/50-companion-app-design.md`, which has landed as `FurbleCompanion` /
`FurbleCompanionService`. Section 116b covers the matching Android app change.

**Firmware (116) is Codex-implementable and host-testable now.** The companion
GATT service already exists on fork master. The Android app change (116b) is
**Claude / Gradle** (needs an Android build and a real phone).

## Motivation

`plans/50` protects the companion link with BLE bonding, encryption and LE
Secure Connections numeric comparison. That is the right protection for a device
with a screen and a button. But two real cases weaken it:

- The screenless nodes (`plans/33` headless, `plans/42` Waveshare) cannot do
  numeric comparison. `plans/42` already reaches for a shared-secret /
  proof-of-possession model because there is no display to confirm a passkey on.
- A user who flashes several units wants a simple, settable shared password they
  can provision at flash time (`plans/114`), rather than a per-device pairing
  dance, while still keeping the default (no password) working exactly as today.

This doc adds a settable shared password as an application-level gate on the
companion service's privileged operations, reusing `plans/50` section 7's
security intent but simplified: one password, set at flash time or over the
console, checked with a challenge before privileged writes. If no password is
set, the link works exactly as it does today (bonding + encryption only), so
existing users see no change.

## Where the check lives

The gate is an application-level challenge on the companion **control** path, on
top of the existing BLE encryption, not a replacement for it. Design:

- Add an auth state per companion connection, default `unauthenticated`.
- Reuse the OTA control characteristic pattern from `plans/50` or add a small
  `auth` opcode on the existing Settings/Trigger characteristic. Preferred: a
  dedicated 1-attribute **Auth characteristic** (write + indicate) so the gate
  is explicit and does not overload an existing char.
- Challenge-response so the password never crosses the wire in the clear even
  though the link is already encrypted:
  1. Phone writes `auth begin`.
  2. furble indicates a random 16-byte nonce.
  3. Phone indicates `HMAC-SHA256(password, nonce)` (truncated to 16 bytes).
  4. furble computes the same and, on match, moves the connection to
     `authenticated`; on mismatch, increments a fail counter and rate-limits.
- Privileged operations (Settings write, Trigger write, and OTA control if
  present) are refused with an ATT error / status while `unauthenticated` AND a
  password is set. Status read and the Device Information Service stay readable
  (they are non-privileged), matching `plans/50`'s tiering.
- **Fallback when no password is set:** if the stored password is empty, every
  connection is treated as `authenticated` immediately, so behavior is identical
  to today. This is the explicit "connection still works" requirement.

`mbedtls` HMAC-SHA256 is already in the image (the certificate bundle pulls
mbedTLS in on network builds; on pure-BLE builds add the small `mbedcrypto`
dependency, measured in the size check).

## Scope

In scope:

- The auth state machine on `FurbleCompanionService`.
- A `COMPANION_PASSWORD` setting (string, default empty) stored in NVS via
  `Settings`, settable over the console (`companion password set <pw>` /
  `companion password clear`) and via the `plans/114` flasher provisioning TLV.
- Rate limiting: after N consecutive auth failures on a connection, drop it and
  back off, so the password cannot be brute forced over BLE.
- The password is never read back: `companion password` prints `set` or `unset`.

Out of scope:

- Per-phone distinct passwords. One shared secret, capped at one companion bond
  per `plans/50`.
- Replacing bonding/encryption. The password is defence in depth on top of them.
- Key rotation UX beyond set/clear.

## Files to change

- `src/FurbleCompanionService.cpp` / `include/FurbleCompanionService.h`: the auth
  characteristic, per-connection auth state, the challenge, and the privileged-op
  gate.
- `include/FurbleSettings.h` / `src/FurbleSettings.cpp`: `COMPANION_PASSWORD`
  setting and its `wire_id` (shared with `plans/114`/`plans/50`). Wire id 47 is
  the companion-password contract, reserved in `include/CLAUDE.md`. IMU already
  owns wire id 46 on master. Do not reuse either id for another setting.
- The PR27 console table: `companion password` subcommands.
- `sim/shim/FurbleCompanionService.h` and the companion rig
  (`sim/CompanionRigTransport.cpp`): mirror the auth handshake so
  `plans/119-companion-gatt-sim.md` can drive it.

## Settings and defaults

| Setting | Type | Default | Effect |
|---|---|---|---|
| `COMPANION_PASSWORD` | string | `""` | empty = no gate (today's behavior); set = challenge required before privileged ops |

Default empty means a fresh device and an upgraded device behave exactly as they
do now: bonding + encryption, no extra step.

## Implementation state

The gate is implemented. The firmware half is the same code the companion app
PRs #195 and #214 carry, so the wire contract cannot drift from the clients.

Setting:

- `Settings::COMPANION_PASSWORD`, a `std::string` at wire id 47, default empty.
- Write-only over the companion settings characteristic. A get returns
  `SETTING_REJECTED` and the list walk skips it.
- Refused by the SD exporter and importer, so the secret never reaches a card.
- Capped at 63 bytes and rejected if it contains a NUL.
- Applied by the provisioning TLV, both as a settings record at wire id 47 and
  through the dedicated `COMPANION_PASSWORD` field tag. The field is no longer
  deferred.

Auth:

- `CompanionAuth` in `include/FurbleCompanionAuth.h` and
  `src/FurbleCompanionAuth.cpp` is the connection-local state machine. It takes
  the HMAC and the nonce generator as function pointers, so it has no mbedTLS
  or ESP-IDF dependency and runs unchanged on the host.
- `src/FurbleCompanionCrypto.cpp` is the firmware HMAC adapter over mbedTLS.
  `tests/host/companion/companion_hmac.cpp` is the host counterpart over the
  shared SHA-256 in `tests/host/companion/HostHmacSha256.h`.
- The nonce comes from `esp_fill_random`.
- The comparison is constant time and the password and nonce buffers are
  zeroed on replacement and destruction.

Wire contract, matching the clients:

| Item | Value |
| --- | --- |
| Auth characteristic UUID | `b57f4f6f-087b-4740-b71d-8262cf26ebbc` |
| Nonce | 16 bytes |
| Response | HMAC-SHA256 truncated to the leading 16 bytes |
| Begin packet | `01 00` |
| Challenge indication | `01 00` then the 16 nonce bytes |
| Proof packet | `01 01` then the 16 response bytes |
| Result indication | `01 02` then the status |
| Status codes | 1 authenticated, 2 rejected, 3 dropped, 4 not required |
| ATT error on a gated write | `0x80` |
| Failure limit | 3, then the link is dropped |

Client sources this was read against:

- `companion/android/app/src/main/java/com/furble/companion/ble/GattConnection.kt`
  and `.../protocol/FurbleProtocol.kt` on PR #214.
- `companion/apple/Sources/FurbleCompanionCore/FurbleBLEClient.swift` and
  `.../FurbleProtocol.swift` on PR #195.

The two apps carry byte-identical firmware halves, so there was nothing to
reconcile. Both were written against an older master and needed two
corrections here: the wire id moved from 46 to 47, because master now ships the
IMU enable switch at 46, and `tests/host/CMakeLists.txt` referenced a
`sim/FurbleCompanionCrypto.cpp` that does not exist in either branch.

Session lifecycle:

- Cleared on connect, on disconnect, and whenever the password is reloaded.
- A password rotation over the settings characteristic revokes the session
  before the acknowledgement is sent, so the acknowledgement cannot race a
  protected follow-up write that still carries the old authorization.
- A response consumes its nonce whether it succeeds, fails, or is malformed.
  One nonce is never valid twice.
- An empty password authenticates every connection immediately, so a user who
  never sets one sees no change. A password that fails to load cleanly is
  treated as set and invalid, never as empty, so a corrupt value cannot become
  a bypass.

## Threat model decisions

- **`handleLocation` requires an encrypted, link-authenticated connection but
  not the password.** It had no check at all before this PR, which was the real
  bug. It stays outside the password gate because both companion apps stream
  fixes as soon as the link is ready and only authenticate before settings and
  trigger, so a password gate there would silently drop fixes with no error the
  app can surface. The write is still bounded by the bonded encrypted link.
- **`COMPANION_CHAR_OTA_CONTROL` and `COMPANION_CHAR_OTA_DATA` have no handler
  yet.** No stubs were added. When their handler lands it must call
  `allowProtected()` first, like `handleSettings` and `handleTrigger` do. OTA
  writes firmware, so it is the most privileged operation on the service.

## Deviations

- Wire id 47, not the 45 this plan first named and not the 46 the app branches
  assumed. Master ships IMU at 46 and the gesture branches claim 45. 47 is free
  on master and on every open PR head except #195 and #214, which expect it.
- Four golden fixtures for id 47, not five. A write-only setting has no list
  record, so a `response-list-47.bin` would assert a record the firmware never
  emits.
- No challenge expiry. A pending challenge stays unprivileged until it is
  answered or the link drops, so an unanswered challenge grants nothing. A
  wall-clock timeout can be separate hardening.
- The host SHA-256 lives in one header shared by the auth and GATT tests rather
  than being copied into each, so the two cannot drift.

## Owed verification

The on-device handshake bench with the Android app from 116b is owed
**post-merge** and is the gate on calling 116 done:

- Set a password over the console, connect from the app, confirm the prompt.
- Correct password unlocks trigger and settings writes.
- Wrong password is refused and the link drops after three failures.
- Replayed proof is refused.
- Disconnect and reconnect require the handshake again.
- Empty password connects and triggers with no prompt.

Until that runs, #166 is verified by the host suite, the simulator, and the
firmware builds only.

## Dependencies

- `plans/50-companion-app-design.md` companion GATT: **landed** (FurbleCompanion,
  FurbleCompanionService on fork master). **Startable NOW.**
- `plans/114-flasher-provisioning.md`: the flasher is the intended way to set the
  password at flash time. 114 and 116 share the settings `wire_id` table.
- `plans/119-companion-gatt-sim.md`: adds the host assertions for this gate; 119's
  password-gate slice depends on 116.

## Risks

- **A set password must never lock out status/DIS.** Only privileged ops gate.
  Test that status notify and DIS stay readable unauthenticated.
- **Timing side channels.** Compare the HMAC with a constant-time compare.
- **Brute force over BLE.** The connection interval bounds attempt rate anyway,
  but add an explicit fail counter and drop, and assert it.
- **Empty-password fallback must be exact.** A subtle bug where empty password
  still gates would break every existing companion user. This is the single
  highest-value test.
- **Size on pure-BLE builds.** Adding `mbedcrypto` for HMAC costs flash on the
  4 MB boards. Measure with `pio run -t size`; if it does not fit on a 4 MB
  board, gate the companion feature build there, per `plans/50`'s stance that
  companion targets the S3 first.

## Codex self-verification (headless, no phone)

Add a host test `companion_auth_test` under `tests/host`, registered in the
host CTest suite, exercising the auth state machine directly (it is plain
logic over a mock connection object):

- Password set, correct HMAC response to the nonce -> connection becomes
  `authenticated`; a subsequent Trigger/Settings write is accepted.
- Password set, wrong HMAC -> stays `unauthenticated`; a Trigger/Settings write
  is refused with the auth error; fail counter increments; after N failures the
  connection is dropped.
- Password set, replayed response with a fresh nonce -> refused (nonce is
  per-connection, single use).
- **Password empty -> connection is authenticated immediately; every privileged
  op is accepted with no challenge** (the fallback contract).
- Status notify and Device Information reads succeed regardless of auth state.

Run:

```
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests --parallel 2
ctest --test-dir build/host-tests -R companion-auth --output-on-failure
```

Also assert the flash cost on the tightest board:

```
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-c-plus -t size
```

Exit 0 and the app fitting the slot prove the firmware side headless. See
`plans/119` for the end-to-end sim assertion through the rig.

## Residual (Claude / hardware) verification

- On a real S3 with the companion service, set a password over the console,
  connect from the phone app, confirm the app is prompted and that a correct
  password unlocks trigger/settings while a wrong one is refused.
- Confirm an empty-password device still connects and triggers with no prompt.

---

# 116b - Android companion app password support

**Claude / Gradle. Not Codex.** Codex has no Android SDK, Gradle or adb, so it
cannot build or run this. Design only here; a Claude/hardware pass builds it.

## Scope (companion/android)

- A password field in the association / settings flow. Stored in the app's
  encrypted preferences, not in plaintext.
- On connect, if furble's Auth characteristic reports "password required" (the
  first privileged write returns the auth error, or a capability flag on the
  Status characteristic advertises it), run the challenge:
  read the nonce indication, compute `HMAC-SHA256(password, nonce)`, write the
  response, and only then enable the trigger and settings UI.
- If no password is required (empty on device), skip the prompt entirely so the
  default experience is unchanged.
- A clear error path when the password is wrong (the device dropped the link
  after N tries): tell the user, do not silently retry-loop.

## Files (companion/android)

- The BLE client that talks to the companion service (add the Auth
  characteristic UUID and the challenge exchange).
- The connection view model / settings screen (password entry, storage,
  required-vs-not state).
- `AndroidManifest.xml` unchanged; no new permission (this is app-level over the
  existing GATT link).

## Verification (Claude / hardware)

- `./gradlew assembleDebug` builds.
- On a real phone against a real password-set furble: prompt appears, correct
  password unlocks, wrong password is rejected with a clear message.
- Against a no-password furble: no prompt, trigger works immediately.
- Note in the PR body that this is hardware-verified with a phone plus the S3,
  and that Codex could not touch it.
