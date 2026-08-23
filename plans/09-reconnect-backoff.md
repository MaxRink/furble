# PR09: exponential backoff for infinite reconnect

## Goal

Replace the flat 5 second retry loop used by infinite reconnect with exponential
backoff capped at 120 seconds. Default off, so current behavior is unchanged.

## Scope

- Backoff delay computation and reset rules in `Control::connectAll()`.
- Make the retry delay interruptible so disconnect stays responsive.
- New setting to enable the feature.
- All boards. No hardware dependency.
- Out of scope: the connect timeout itself, connection parameters (PR10).

## Files to change

| File | Anchor | Change |
|---|---|---|
| `src/FurbleControl.cpp` | 96-97 | keep the per attempt connect timeout as is |
| `src/FurbleControl.cpp` | 115-118 | reset the backoff on success |
| `src/FurbleControl.cpp` | 120-122 | leave the `STATE_DISCONNECTING` early return |
| `src/FurbleControl.cpp` | 124-130 | replace `vTaskDelay(SLEEP_INFINITE_MS)` with a sliced backoff delay |
| `src/FurbleControl.cpp` | 216-220 | reset the backoff when the user starts a connect |
| `src/FurbleControl.cpp` | 222-244 | reset the backoff on disconnect |
| `include/FurbleControl.h` | 72-74 | add backoff constants next to the existing timeouts |
| `include/FurbleControl.h` | 141-146 | add backoff state members |
| `include/FurbleSettings.h` | 16-29 | add `RECON_BACKOFF` |
| `include/FurbleSettings.h` | 145-148 | add `storage_type<RECON_BACKOFF>` = `bool` |
| `src/FurbleSettings.cpp` | 11-24 | add table row |
| `src/FurbleSettings.cpp` | 209-215 | add to the `save<bool>(false)` default group |
| `src/FurbleUI.cpp` | 1613-1621 | add the toggle next to Infinite-ReConnect |

Verified current state:

- `include/FurbleControl.h:72-74`:
  `TIMEOUT_DEFAULT_MS = 30 * 1000`, `TIMEOUT_INFINITE_MS = 5 * 1000`,
  `SLEEP_INFINITE_MS = 5 * 1000`.
- `src/FurbleControl.cpp:97` picks `TIMEOUT_INFINITE_MS` when infinite reconnect
  is on, so each attempt gives up after 5 s.
- `src/FurbleControl.cpp:124-130`: when infinite reconnect is on the task sleeps
  a flat `SLEEP_INFINITE_MS` and returns `STATE_CONNECT`. The retry cycle is
  therefore about 10 s and never grows.
- `src/FurbleControl.cpp:98` takes `m_Mutex` for the whole of `connectAll()`,
  including that `vTaskDelay`.
- `src/FurbleControl.cpp:222-228` `Control::disconnect()` sets
  `STATE_DISCONNECTING`, calls `ble_gap_conn_cancel()`, then blocks on the same
  `m_Mutex`.
- `src/FurbleControl.cpp:115-118` clears `failcount` on success.
- `src/FurbleUI.cpp:1243` passes `Settings::load<Settings::RECONNECT>()` into
  `connectAll()`.
- `src/FurbleUI.cpp:1616-1619` builds the Features page with `AUTOCONNECT`,
  `FAUXNY`, `RECONNECT`, `MULTICONNECT`.

## New settings

| Enum | NVS key | Namespace | Type | Default |
|---|---|---|---|---|
| `RECON_BACKOFF` | `recon_backoff` | `FURBLE_STR` | `bool` | `false` |

Key is 13 characters, under the 15 character NVS limit. Default `false` keeps the
flat 5 s delay at `src/FurbleControl.cpp:127`.

## Menu placement

`Settings > Features > Reconnect backoff`, directly after Infinite-ReConnect at
`src/FurbleUI.cpp:1618`. Use `UI::addSettingItem()`.

If PR08 has already landed, place it under `Settings > Bluetooth` instead, which
matches the agreed final settings tree. Features is the fallback so this PR stays
independent of PR08.

The toggle only has an effect when Infinite-ReConnect is on. Disable or grey the
item when `RECONNECT` is off, using the same event pattern already used for the
`RECONNECT` icon at `src/FurbleUI.cpp:750-767`.

## Implementation notes

- Backoff schedule, in seconds: 5, 10, 20, 40, 80, 120, 120, and so on. Compute
  as `min(SLEEP_INFINITE_MS << attempt, BACKOFF_MAX_MS)` with
  `BACKOFF_MAX_MS = 120 * 1000`.
- Reset the attempt counter to zero on all of:
  - a successful connect (`src/FurbleControl.cpp:115-118`),
  - `Control::connectAll(bool)` being called by the user
    (`src/FurbleControl.cpp:216-220`),
  - `Control::disconnect()` (`src/FurbleControl.cpp:222-244`).
- The delay must be interruptible. Today the code blocks the control task for
  5 s while holding `m_Mutex`, and `Control::disconnect()` waits on that mutex.
  A 120 s block would freeze the disconnect button for two minutes. Sleep in
  slices of 100 ms and break out early when `m_State == STATE_DISCONNECTING`.
  Do not extend the block just because the delay got longer.
- Keep the existing `failcount < 2` path for the non infinite case untouched.
  That path returns `STATE_CONNECT_FAILED` after two failures and has no delay.
- Read the setting once when `connectAll(bool)` is called, not on every retry, so
  a settings change does not race with an in flight loop.
- Log each retry with the attempt number and the delay so the behavior is visible
  in `pio device monitor`.
- Consider adding a small random jitter later. Not in this PR. Keep the change
  minimal and predictable.

## Dependencies

- Independent. Does not need PR06, PR07 or PR08.
- Menu placement improves if PR08 has landed.
- Pairs well with PR07 because a sleeping device benefits most from long idle
  gaps between retries.

## Risks

- Slow recovery. With backoff on, a camera that comes back after 3 minutes may
  take up to 2 more minutes to be picked up. This is the intended tradeoff.
  Document it in the setting name and the PR body.
- Disconnect responsiveness. Covered by the sliced delay above. This is the main
  correctness risk in the PR.
- Reset rules. Missing one of the three reset points leaves the device stuck at
  the 120 s ceiling after a transient failure. Cover each reset point with a
  test.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Fresh NVS boot. Confirm `RECON_BACKOFF` defaults to false, then enable
   Infinite-ReConnect and power the camera off. Confirm the retry log shows the
   existing flat 5 s cadence, matching master.
3. Enable backoff. Power the camera off. Confirm the logged delays follow
   5, 10, 20, 40, 80, 120, 120 seconds.
4. During the 120 s delay, press disconnect. The UI must return to the main menu
   within about 1 second.
5. Power the camera back on during a long delay. Confirm the connect succeeds on
   the next attempt and the counter resets to 5 s on the following failure.
6. Start a fresh connect from the menu during a long delay. Confirm the counter
   resets.
7. Repeat steps 2 to 5 on one AXP192 board.

Battery drain, on-board instrumentation only, no external power meter:

- Unplugged 60 minute runs on M5StickS3 with Infinite-ReConnect on and no camera
  present, once with backoff off and once with backoff on. Log battery voltage
  and percent every 30 s. Report percent per hour for both. The backoff run
  should drain measurably less because the radio spends more time idle.

Camera testing:

- Only Fujifilm cameras are available. Run all steps above with Fujifilm.
- The change is in `Control`, above the per vendor camera classes, so every
  vendor takes the identical code path. FauxNY covers the state machine without
  a radio, though note FauxNY connects instantly so it cannot exercise the
  failure path.
- Sony, Nikon, Canon and Ricoh are not hardware tested. State this in the PR
  body along with the note that no vendor specific code is touched.

## References

- [ESP-IDF power management API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html)
  for why longer idle gaps let the CPU stay in a lower power state.
- [Espressif nimble power_save example](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md)
  for the current cost of an active radio compared with an idle one.

## Implementation state

Implemented in PR #19 on the fork. Deviations from the plan:

- The fix goes further than a sliced delay. `connectAll()` now snapshots the
  camera list under `m_Mutex` and releases the mutex before any connection
  attempt or retry wait. A `m_ConnectAbort` flag plus a `m_ConnectInProgress`
  flag let `disconnect()` interrupt both the attempts and the wait, then safely
  clear the targets under the mutex.
- The retry wait checks both `m_ConnectAbort` and `STATE_DISCONNECTING` every
  100 ms slice, so cancel takes effect within about 100 ms.
- After rebasing onto the integrated master, state transitions go through
  `Control::setState()` so the sleep lock bookkeeping from PR07 stays correct.
- The toggle lives in Settings > Features after Infinite-ReConnect and is
  greyed out while Infinite-ReConnect is off, as planned.
- Backoff schedule, reset points, setting name and default all match the plan.
- Verified: builds for m5stick-s3. The deadlock was reproduced on the StickS3
  before the fix. The fix walk, cancel during connecting with the camera off,
  is still pending on hardware.

## Follow-up: shorten the first retry (2026-08-21)

Hardware evidence after #121 landed: a stale-session reconnect still felt slow,
about 45 seconds. The first connect attempt fast-fails, then furble waited a full
17 seconds before the first retry. That 17 second floor came from the earlier
stale-session hint, which assumed the camera holds its previous BLE session for
a long time. In practice the camera usually releases the session within a couple
of seconds, so the long first wait just stalls the reconnect for no benefit.

Change: the first retry now waits a short fixed 2.5 seconds instead of 17 seconds.
Later retries keep the existing exponential backoff (5, 10, 20, 40, 80, 120
seconds) and its cap unchanged, so a persistently unreachable camera still backs
off exactly as before. Rationale: match the retry cadence to how quickly the
camera actually clears its session, so a session it releases in a few seconds
reconnects in a few seconds.

The backoff arithmetic moved into `include/FurbleReconnectBackoff.h`
(`ReconnectBackoff::delayMs`), a header with no BLE, FreeRTOS or NVS dependency.
`Control::connectAll()` calls it, and a host unit test,
`tests/host/reconnect_backoff_test.cpp` (ctest `reconnect-backoff`), pins the
curve: the first retry is quick, backoff-disabled retries hold a flat base wait,
and backoff-enabled retries grow exponentially to the cap. The test bound on the
first retry (at most 3 seconds) fails if the value is restored to 17 seconds,
which was confirmed by mutation.

No new UI toggle. The existing `RECON_BACKOFF` setting still gates the
exponential curve for later retries.

PENDING HARDWARE RETEST: the 2.5 second first retry is chosen for feel and needs
on-device confirmation that a stale-session reconnect now recovers in a few
seconds without spinning the radio.

## Follow-up: skip the first retry when furble initiated the disconnect (2026-08-23, task #54)

Bench report: reconnect after disconnecting via furble also took a while. The
2.5 second first retry (above) exists so a stale session on the camera can
expire before furble reconnects. That reason only applies to a peer-initiated
drop (camera power-off, supervision timeout, out of range), where the camera may
still hold the previous BLE session for a moment. When furble itself initiated
the disconnect, the interactive Disconnect or the clean pre-restart teardown of
plan 68, the camera got a proper link termination and holds no stale session, so
the 2.5 second wait is avoidable latency.

Change: the first retry is now conditional on who caused the prior disconnect.
`ReconnectBackoff::delayMs()` gained a `furbleInitiated` argument; at attempt 0
it returns `FIRST_RETRY_FURBLE_MS` (0, immediate) when furble initiated, and the
unchanged `FIRST_RETRY_MS` (2.5 s) otherwise. Only the first retry is affected;
the exponential curve for attempt >= 1 is identical either way, so a persistently
unreachable camera still backs off exactly as before.

How furble-initiated is known: `Control` tracks a `m_FreshConnect` bit.
`connectAll(bool)` sets it, because a connect started there is always a fresh,
furble-initiated connect (the first ever connect, a user reconnect after
Disconnect, or the boot autoconnect after a clean restart, all of which follow a
furble-initiated disconnect or no disconnect at all). It is cleared on the first
successful connect. A mid-session drop re-enters connect through the
`STATE_ACTIVE` path, not `connectAll(bool)`, so `m_FreshConnect` stays false
there: the only way a live target re-enters connect without `connectAll(bool)` is
a peer-initiated drop, which is exactly the case that must keep the backoff. The
bit is exposed as `control.fresh_connect` in the debug console `debug control`
snapshot.

Scope note: the camera power-off case the user already accepted (about 7 s
supervision timeout, then the backoff) is unchanged. That is a peer-initiated
drop, so it keeps `FIRST_RETRY_MS`.

Tests: `tests/host/reconnect_backoff_test.cpp` gains
`testFurbleInitiatedFirstRetryIsImmediate`, pinning the pure `delayMs()` curve
for both initiators. A new Control harness,
`tests/host/reconnect_initiator_test.cpp` (ctest `reconnect-initiator`), drives
the production control task against MockNimBLE through both paths and pins the
timing: a furble-initiated fresh reconnect whose first attempt fails reaches
active in well under the 2.5 s wait, while a peer-initiated supervision-timeout
drop still waits it out. Reverting the `delayMs()` condition (always
`FIRST_RETRY_MS` at attempt 0) fails both, which was confirmed by mutation: the
fresh reconnect went from about 0.1 s to about 2.7 s.

PENDING HARDWARE CONFIRM (user): disconnect via furble then reconnect is fast;
camera power-off then reconnect is unchanged (about 7 s supervision plus the
backoff).
