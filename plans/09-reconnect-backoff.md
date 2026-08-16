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
