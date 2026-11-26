# 149: Ricoh GR IV sleep shutter gate

## Status

Phase 1 implemented. Phase 2 (wake) implemented in the host-tested state
machine below. Both phases are hardware bench pending: the awake capture is
unchanged, the asleep refusal does not freeze the camera, and whether a reconnect
actually wakes a GR IV is unproven. Nothing in this plan replaces the fresh
OperationMode gate: a body that does not report CAPTURE is never written to.

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

## Phase 1 change (implemented, hardware bench pending)

- `lib/furble/Ricoh.{h,cpp}`: the pre-capture gate performs a fresh `gattRead`
  of OperationMode (`1452335a-ec7f-4877-b8ab-0f72e18bb295`) before every
  shutter press. Capture proceeds only on `0x00` CAPTURE. On PLAYBACK,
  OTHER, POWER_OFF_TRANSFER, or a read failure both writes are skipped and one
  ESP_LOGW names the mode, for example
  `Ricoh shutter refused: mode 0x01 PLAYBACK`. Phase 1 refused BLE_STARTUP with
  `Ricoh shutter refused: camera asleep (mode 0x02 BLE_STARTUP)`; phase 2 hands
  that state to the wake machine described below instead, so the phase 1 refusal
  log for standby is now the wake's failure line.
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
into the target task, so a refusal currently surfaces as ESP_LOGW lines only
(visible on the debug console): `Ricoh wake <FROM> -> <TO>` transitions plus,
when the wake does not confirm, `Ricoh shutter refused: camera did not wake`.
Changing the signature to propagate a refusal to the console `shutter` command
and the UI is follow-up work for a separate slice. It matters more now: a press
that has to wake a body can occupy the target task for up to about 7 s before
the refusal line appears.

## Phase 2: the wake sequence

Wake an asleep GR IV by disconnect plus reconnect: dropping the link and
re-establishing it prompts the camera to leave BLE standby, after which a
fresh OperationMode read reports CAPTURE. Writing CameraPower to wake the
camera remains unimplemented: nothing in the referenced sources and no firmware
or official-app evidence shows that a power write wakes the body, and the
Ricoh Imaging handler accepts `0` and `1` as a power state request, not as a
wake command.

### Which characteristic and which sequence

- `OperationMode` (`1452335A-EC7F-4877-B8AB-0F72E18BB295`, Camera service) is
  the only readable authorization source. `0x00` CAPTURE authorizes; `0x02`
  BLE_STARTUP is the standby state the wake exists for; every other value is a
  refusal the wake cannot fix, so it is not even attempted.
- `CameraPower` (`B58CE84C-0666-4DE9-BEC8-2D27B27B3211`) stays `0x01` ON in
  standby, which is exactly why it is not an authorization source.
- The wake itself is a link operation, not a characteristic write: drop the
  link, wait, reconnect to the same bonded peer, and re-run the whole connect
  handshake. The handshake re-derives every characteristic pointer after the
  rediscovery, which is what keeps the wake out of the standby
  use-after-free class recorded below.
- `Camera::reconnectInPlace()` is the seam that does this. It owns
  `Camera::m_Mutex` for the whole sequence the way `Camera::connect()` does,
  keeps the client across the drop (`setSelfDelete(false, false)`), publishes it
  through `m_CancelClient` so `Control::disconnect()` can abort a reconnect
  stuck inside NimBLE, restores self-delete on success, and hands the client
  back to the pool on failure so repeated failed wakes cannot exhaust
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`. It leaves the session flags alone, so a
  wake is not a connect cycle: the user's selection and the target's ownership
  do not change, and no progress-0 blip is published to the UI.

### Is the wake observable in the notification stream

Partly, and never authoritatively.

- Furble subscribes to both `CameraPower` and `OperationMode` notifications, and
  a `CameraPower` transition is hardware-observed: the 2026-08-28 session saw
  `0x00` announced before the standby drop, which the virtual peer models.
- No source records an `OperationMode` notification on wake, and a held or
  freshly re-established link can repeat a stale value in either direction (a
  stale `BLE_STARTUP` forever, or a stale `CAPTURE` after the body slept). A
  notification is therefore a diagnostic and can never authorize a capture.
- The state machine decides on the fresh read it just performed. The virtual
  peer announces the post-wake `CameraPower ON` and `OperationMode CAPTURE`
  transition so `ricoh_wake_test` can assert the notification path is exercised,
  but the test also asserts the capture only follows a fresh CAPTURE read.

### How long the wake waits

The plan records no timing: neither the community documentation nor the
firmware audit states how long a GR IV needs after the link is re-established.
The implementation therefore uses explicit, overridable design bounds, all
declared in `lib/furble/Ricoh.h`:

| Constant | Default | Meaning |
| :--- | :--- | :--- |
| `FURBLE_RICOH_WAKE_SETTLE_MS` | 200 ms | The link stays down so the camera sees a real disconnect. |
| `FURBLE_RICOH_WAKE_CONNECT_TIMEOUT_MS` | 5000 ms | The reconnect's own connect timeout, for an already bonded peer. |
| `FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS` | 2000 ms | The whole bounded wait for a fresh CAPTURE read after the link is back, polled every 50 ms. |

Worst case is therefore a settle plus a connect that can reach its 5 s cap plus
the 2 s probe, all on the target task. Every one of these numbers is a design
bound, not a measurement.

### State machine

`Ricoh::wakeFromStandby()` is an explicit state machine with one transition per
pass, each transition logged as `Ricoh wake <FROM> -> <TO>: <reason>`:

| State | Entry | Work | Exits |
| :--- | :--- | :--- | :--- |
| `IDLE` | No wake attempted on this connection | None | `RELINK` when the gate reads `BLE_STARTUP` |
| `RELINK` | Gate returned ASLEEP | `Camera::reconnectInPlace()` | `PROBE` on a live link, `FAILED` otherwise |
| `PROBE` | Link is back | One fresh `OperationMode` read, then a 50 ms pause | `AWAKE` on CAPTURE, `FAILED` on another mode, a lost link, a cancel, or the deadline |
| `AWAKE` | Fresh CAPTURE read | Hands the authorization to `shutterPress()` | Terminal for this press |
| `FAILED` | Any refusal | Writes nothing at all | Terminal for this press |

`shutterPress()` runs the phase 1 gate first, and only `ASLEEP` enters the
machine. The terminal CAPTURE read of `PROBE` is the authorization, so the
capture writes follow it directly instead of being re-authorized by a second
read. `FAILED` leaves the connection in whatever state `RELINK` reached: on a
probe timeout the relinked connection stays up and the next press retries, and
on a relink failure the client is reclaimed and the camera reports disconnected,
which is the same state a failed connect leaves. Neither path freezes the
camera, and neither path writes a capture.

### Phase 2 tests

`tests/host/ricoh_wake_test.cpp` (`ricoh-wake`) drives the production shutter
path against the extended virtual peer:

- A sleeping body (`CameraPower ON`, `OperationMode BLE_STARTUP`) that wakes on
  reconnect: one relink, at least one post-relink poll, capture writes in
  flavor-then-OperationRequest order, and zero capture requests while asleep.
- A body that never wakes: the full probe budget elapses, no capture write is
  attempted, the relinked connection stays up, a second press retries the wake on
  that same connection, and a body that then reports CAPTURE captures without
  another relink.
- `PLAYBACK`, `OTHER` and `POWER_OFF_TRANSFER` still refuse with zero writes and
  no relink, so the wake only ever fires for `BLE_STARTUP`.
- A failed `OperationMode` read refuses without relinking.
- A wake whose reconnect fails writes nothing and leaves the camera reported
  disconnected, which is the RELINK to FAILED edge and the client reclaim.
- A wake that runs after a cancel stops in the probe instead of spending the
  budget (the plan 148 cancel contract) and still writes nothing.

The virtual peer gained a sleeping-body model (`setSleepingBody()`,
`linkSessions()`, `reconnectCount()`, `operationModeReadCount()`,
`captureRequestsWhileAsleep()`). A capture start received while it is still in
standby is counted and refused, so a regression that removes the gate is
visible instead of silent.

Red first: with the phase 2 change reverted and the peer model in place,
`ricoh-wake` fails on both the successful wake and the timeout
(`a sleeping body is woken by one reconnect`, `wake emits flavor then
OperationRequest in order`, `the body reports CAPTURE after the wake`, `the wake
polls a fresh OperationMode read instead of reading once`, `the wake still
relinks a body that will not wake`, `the wake waits the full probe budget before
refusing`, plus the notification and recovery checks), exit 1, while the three
non-standby refusal cases already pass. `ricoh-sleep-gate` is unchanged in
substance and now asserts the phase 1 invariant as "no capture write" rather
than "no write at all", because the wake repeats the connect-path identity write.

### Deviations from the phase 1 text

- `captureAllowed()` became `captureGate()` plus `readOperationMode()` so the
  gate can report three outcomes. The phase 1 invariant and log lines for every
  non-standby case are unchanged.
- The phase 1 host test asserted zero writes of any kind in BLE_STARTUP. A
  sleeping body now relinks, and the reconnect re-runs the connect path, which
  writes the paired-device identity. The invariant was always about the capture
  writes, so the test now asserts zero capture writes and no capture request
  reaching a standby body. This is the only behaviour change to phase 1.

### What only hardware can confirm

Nothing in this repository can prove the premise, so treat all of it as
unverified until a bench run records it:

1. That dropping and re-establishing the link actually wakes a GR IV 1.20 out of
   BLE standby. The whole phase rests on this one observation.
2. That the body is connectable again after furble terminates the link. A
   peripheral that stops advertising when it drops cannot be reconnected at all,
   and the wake then fails closed to a refusal. The virtual peer models a body
   that always accepts the next link; no source states what a standby GR IV
   advertises after a central terminate.
3. The real settle, connect and wake times behind the 200 ms / 5000 ms / 2000 ms
   bounds, and whether 50 ms is a sensible poll cadence for the body.
4. Whether the wake appears as an `OperationMode` notification, a `CameraPower`
   notification, or neither. Furble does not depend on it, but the peer model
   announces both and that is a model, not a capture.
5. Whether the connect-path paired-device-name write is harmless in standby.
   Furble already performs it on every connect to a sleeping body, so the wake
   adds no new write class, but no source says it is safe either.
6. Whether NimBLE's attribute rediscovery on reconnect behaves as modelled. The
   mock keeps its attribute cache across a reconnect while real NimBLE frees it
   (`delete_attributes` defaults true), and the wake path re-derives every
   pointer through the connect handshake, so the difference should be
   invisible. "Should be" is not a measurement.
7. Whether a body that was woken but not captured drops straight back into
   standby, which would make a quick second press pay for another wake.
8. What the user sees when a wake fails: up to about 7 s of blocked target task
   and then a refusal on the debug log only, because the refusal has no UI
   surface yet (see below).

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

## GATT journal use-after-free (hardware crash 2026-08-28)

Pressing the shutter while the GR IV dropped the link into standby crashed
furble with LoadProhibited on EXCVADDR 0xfefefefe. Backtrace:
Ricoh::shutterPress -> Ricoh::captureAllowed -> Camera::gattRead ->
journalRecord -> NimBLEUUID::toString -> ble_uuid_to_str on freed memory.

Root cause: the pointer-based Camera::gattRead and Camera::gattWrite read the
service and characteristic UUIDs from the NimBLERemoteCharacteristic after
readValue()/writeValue() returned, inside the FURBLE_CONSOLE journalRecord
block. The standby drop during the sleep gate read let the reconnect path
free the remote attribute objects during service rediscovery, so the
post-operation dereference landed on freed memory.

Fix: both wrappers snapshot the service and characteristic UUIDs by value
before the operation and journal the copies, so the characteristic pointer is
never dereferenced again after the operation returns.

Regression test: gatt-journal-uaf (tests/host/gatt_journal_uaf_test.cpp).
It compiles Camera.cpp with AddressSanitizer and FURBLE_CONSOLE, then uses
the virtual peer's faultNextOperation hook to free the client's cached remote
service and characteristic objects (NimBLEClient::dropServiceCache) while the
read and write are in flight. Mutation proven: restoring the post-operation
characteristic->getUUID() dereference makes ASan abort with
heap-use-after-free in NimBLEUUID::toString via journalRecord, matching the
hardware trace.

Residual risk: a free landing while the blocking readValue()/writeValue()
call is still inside NimBLE remains a narrower window this fix does not
cover. That belongs to the broader connection-lifetime work (reconnect
rediscovery racing in-flight vendor operations) and is tracked separately.
