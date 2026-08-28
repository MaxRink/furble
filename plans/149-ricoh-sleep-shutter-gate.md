# 148: Ricoh GR IV sleep shutter gate

## Status

Phase 1 implemented. Hardware bench verification pending (awake capture
unchanged, asleep refusal without a camera freeze). Phase 2 (wake) is design
only and is not implemented.

## Root cause

`Ricoh::shutterPress()` unconditionally wrote ShootingFlavor `0x00` and then
OperationRequest `{0x01, 0x01}`. A GR IV in BLE standby keeps the link up and
reports CameraPower `0x01` ON while OperationMode reads `0x02` BLE_STARTUP. A
capture write in that state cold boots the camera (the lens extends) and
wedges its firmware. Hardware observed on 2026-08-28; recovery required a
forced power off.

## Invariant

Power ON is not sufficient to authorize capture. BLE_STARTUP (`0x02`) and
POWER_OFF_TRANSFER (`0x04`) must block all side-effect writes. Only a fresh
OperationMode read of `0x00` CAPTURE authorizes capture. A held connection can
return stale BLE_STARTUP (or stale CAPTURE) forever, so the pre-capture read
must be fresh; cached state is never an authorization source. A non-CAPTURE
reading is always a safe refusal.

## Phase 1 change

- `lib/furble/Ricoh.{h,cpp}`: `captureAllowed()` performs a fresh `gattRead`
  of OperationMode (`1452335a-ec7f-4877-b8ab-0f72e18bb295`) before every
  shutter press. Capture proceeds only on `0x00` CAPTURE. On PLAYBACK,
  BLE_STARTUP, OTHER, POWER_OFF_TRANSFER, or a read failure both writes are
  skipped and one ESP_LOGW names the mode, for example
  `Ricoh shutter refused: camera asleep (mode 0x02 BLE_STARTUP)`.
- Cached `m_LastPower` and `m_LastOperationMode` are seeded from the
  `_connect()` state probe and refreshed by the CameraPower and OperationMode
  notify callbacks. The cache is diagnostic only; the capture decision uses
  the fresh read (stale cache trap, both directions).
- `focusPress()` and `focusRelease()` remain no-ops.
- Host test `tests/host/ricoh_sleep_gate_test.cpp` drives the production
  shutter path against the virtual Ricoh: BLE_STARTUP refuses with zero
  writes, CAPTURE emits flavor then OperationRequest in order, mode flips on
  a held connection are honored in both directions, and a read failure
  refuses. The virtual camera gained configurable CameraPower and
  OperationMode read values.

## UI surfacing remaining

`Camera::shutterPress()` returns void through Control `CMD_SHUTTER_PRESS`
into the target task, so the refusal currently surfaces as the ESP_LOGW line
only (visible on the debug console). Changing the signature to propagate a
refusal to the console `shutter` command and the UI is follow-up work for a
separate slice.

## Phase 2 (future work, not implemented)

Wake an asleep GR IV by disconnect plus reconnect: dropping the link and
re-establishing it prompts the camera to leave BLE standby, after which a
fresh OperationMode read should report CAPTURE. This is bench-gated and must
be verified on hardware before any implementation. Writing CameraPower to
wake the camera is unverified anywhere in the referenced sources and must not
be implemented.

## Citations

- https://github.com/dm-zharov/ricoh-gr-bluetooth-api/blob/master/camera/operation_mode.md
- https://github.com/dm-zharov/ricoh-gr-bluetooth-api/blob/master/camera/camera_power.md
- https://github.com/sky18Dragon/RICOH-GR-Live-View-Shooting/blob/main/docs/power_state_policy.md
- https://github.com/sky18Dragon/RICOH-GR-Live-View-Shooting/blob/main/docs/known_issues.md
- https://github.com/sky18Dragon/RICOH-GR-Live-View-Shooting/blob/main/logs/2026-06-28-camera-power-state-investigation.md
- https://www.ricoh-imaging.co.jp/english/products/gr-3/feature/04.html

## Review notes

- The gate applies only when the OperationMode characteristic exists. Bodies
  matched by the broad Ricoh matcher without the GR camera service cannot
  report a power state and shot fine before the gate, so they keep their
  shutter. The GR IV always exposes the characteristic.
- The fresh read costs one ATT round trip per press, bounded by the ATT
  transaction timeout on a dead link. It runs on the target task, not under
  the camera mutex, so it cannot recreate the teardown wedge.
- GPS and location control writes are not gated yet. Whether they are safe in
  BLE standby is unknown; the bench experiments in this plan cover it.
