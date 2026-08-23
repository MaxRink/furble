# 119 - Sim companion GATT (app) coverage

Status: design only. Extends the SDL sim's companion "rig" so a mocked GATT
central drives the companion service end to end, including the new password gate.

**Codex-implementable. Startable NOW** for the existing characteristics (the
companion GATT service and the rig transport already exist on fork master). The
password-gate assertions depend on `plans/116-companion-password.md` landing.

## Motivation

`plans/50-companion-app-design.md` companion GATT service has landed
(`FurbleCompanion`, `FurbleCompanionService`). The sim already has a companion
"rig": `sim/CompanionRigTransport.cpp`, `sim/shim/FurbleCompanionService.h`, the
driver hooks `rigConfigure` / `startRig` / `rigInjectPendingPairing` /
`rigConfirmPairing`, and one scenario `sim/scenarios/e2e/companion-pairing-input.txt`
that covers the pairing modal focus contract. What is NOT covered is the actual
GATT surface a phone uses: writing a location fix, receiving status notifies,
the settings TLV request/response, the trigger characteristic, and (once
`plans/116` lands) the password auth gate. This doc adds a mocked central that
drives each and asserts furble's response.

## Scope

In scope, extending the existing rig:

- Driver actions (in `sim/driver.h` / `sim/driver.cpp`, mirroring the existing
  `companion-*` actions) for a mock central:
  - `companion-write-fix <lat> <lon> <alt> <sats> <age_ms>`: write a
    `companion_fix_t` to the Location characteristic.
  - `companion-read-status`: capture the latest Status notify.
  - `companion-settings <op> <wire_id> [value]`: write a settings TLV request
    (`list`/`get`/`set`) and capture the indicated response.
  - `companion-trigger <op> [hold_ms]`: write the Trigger characteristic.
  - `companion-auth <password>`: run the `plans/116` challenge (read nonce,
    write HMAC response).
- Driver queries (`ui.*` / `companion.*`) so scenarios can assert:
  - `companion.gps_source` (0 none / 1 uart / 2 companion) reflects a written
    fix, via the existing `GPS::setExternalFix` arbitration in `plans/50`.
  - `control.shutter_presses` / `control.focus_presses` (already exposed as
    `cameraShutterPresses` etc. in `sim/driver.h`) increment on a Trigger write.
  - `companion.status_*` fields match `Control`/battery/GPS state.
  - `companion.settings_status` and the echoed value for a TLV get/set.
  - `companion.auth` state and that privileged writes are refused before auth
    and accepted after, with an empty password auto-authenticating.
- New scenarios under `sim/scenarios/e2e/`:
  - `companion-location-fix.txt`: write a fix, assert `companion.gps_source
    companion` and that a stale fix ages out after 30 s (no fix).
  - `companion-status-notify.txt`: connect a camera, assert the status notify
    reports connected count and control state.
  - `companion-settings-tlv.txt`: `list` terminates with `id 0xFF`; a `set`
    changes the target setting and a device-local id is refused.
  - `companion-trigger.txt`: `op 4` timed shutter fires exactly one press and one
    release; a trigger with no camera is refused.
  - `companion-auth-gate.txt` (needs `plans/116`): wrong password refused, right
    password accepted, empty password auto-authenticated.

Out of scope:

- Real BLE. The rig is a TCP stand-in for a GATT central, per the existing
  design.
- The Android app itself (`plans/116b`).

## Files to change

- `sim/driver.h` / `sim/driver.cpp`: the new mock-central actions and queries.
- `sim/CompanionRigTransport.cpp`: extend the rig frame to carry
  location/status/settings/trigger/auth exchanges.
- `sim/shim/FurbleCompanionService.h`: expose the auth state once `plans/116`
  lands.
- New scenario `.txt` files under `sim/scenarios/e2e/`.

## Settings and defaults

None. Test-only. Uses `FURBLE_RIG` (already a sim compile define) and the rig
enable path.

## Dependencies

- `plans/50` companion GATT + `sim/CompanionRigTransport.cpp`: **landed.
  Startable NOW.**
- `plans/116-companion-password.md`: the `companion-auth-gate.txt` scenario is
  gated on 116. The other four scenarios are not.
- The status-notify scenario reuses the real-vs-fake Control seam; align with the
  existing `control.*` driver queries.

## Risks

- **The rig is a stand-in, not real BLE.** The pairing scenario already documents
  that some hardware symptoms (the #32 encoder-only input break) cannot be
  reproduced through the rig. State the same caveat for the auth gate: the sim
  proves the app-level logic and refusal contract, not the ATT-layer encryption.
- **Query drift.** Add `companion.*` queries alongside the existing `ui.*` set
  rather than overloading `ui.*`, and document them in `plans/121`.
- **Fix arbitration.** The companion fix must lose to a live UART fix; assert
  that ordering, not just that a companion fix is accepted.

## Codex self-verification (headless)

Build the sim once, then run the new scenarios (SDL dummy video is enough, as
`run-e2e.sh` sets):

```
# Populate the dep cache once (or set FURBLE_DEP_ROOT), then:
python3 tools/gen_lv_conf.py sdkconfig.m5stick-s3 sim/lv_conf.h
sh sim/build.sh

# Whole e2e suite, including the new companion scenarios:
sh sim/scripts/run-e2e.sh

# Or a single new scenario:
SDL_VIDEODRIVER=dummy sim/build/furble-sim \
  --script sim/scenarios/e2e/companion-trigger.txt
```

`run-e2e.sh` exits non-zero if any scenario fails an `assert`. Exit 0 proves the
companion location/status/settings/trigger (and, with `plans/116`, the auth
gate) headless, with no phone and no BLE radio.

## Residual (Claude / hardware) verification

- `plans/50` / `plans/116` hardware suite: a real phone over real BLE, MTU
  negotiation, encryption, numeric-comparison pairing, and the password prompt.
  The rig cannot prove the ATT encryption layer.
