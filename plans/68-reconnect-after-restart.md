# Reconnect after restart

Status: implemented in `feat/68-reconnect-restart`. Hardware verification remains
to be run on the M5StickS3 with a Fujifilm camera.

## Motivation

Restarting furble while a camera is connected can leave the camera believing the
old BLE link is still active. The next boot autoconnect starts while that stale
session is still held. A restart from Settings, touch calibration, or the USB
console should release the camera first. An unclean reset must still recover
without asking the user to wait for the camera's timeout.

The acceptance test is: restart while connected, then start an immediate
reconnect. It must succeed without waiting for the camera timeout.

## Root cause

The three `esp_restart()` call sites did not share shutdown behavior. The theme
page disabled the StickS3 watchdog, but calibration and the console reboot did
not. None of them asked `Control` to disconnect its active cameras.

`Control::disconnect()` sent target stop commands, then waited without a bound.
The target destructor initiated the camera disconnect only after the target task
stopped. A slow or stuck BLE stack could therefore delay restart, and a normal
reset gave the camera no clean disconnect at all.

Master requests 30 to 50 ms interval, latency 1, and
`2 * BLE_GAP_INITIAL_SUPERVISION_TIMEOUT`. The NimBLE initial supervision value
is `0x0100`, so the camera-side supervision timeout is 5.12 seconds. The
conn-saver worktree uses a 16 second idle supervision timeout. The existing
first reconnect wait was only 5 seconds, which was too close to the master
timeout and shorter than the conn-saver case.

## Design

- `Platform::restart()` is the only application restart entry point. It calls
  `prepareRestart()`, which asks `Control` to stop all target tasks and cameras,
  waits up to one second, logs a timeout if needed, and disables the StickS3
  watchdog. A timeout never prevents the reset.
- `Control::disconnect()` keeps the existing abort and GAP cancel behavior. The
  target task performs `Camera::disconnect()` before it reports stopped. The
  control task checks completion in short slices and never holds `m_Mutex`
  during a delay. On the clean path it clears targets only after target tasks,
  the connection attempt, and camera connection state have all completed.
- Bounded force-completing disconnect (plans/96 batch 1, items B2 and B3). The
  earlier bounded timeout returned early and left `Control` in
  `STATE_DISCONNECTING` with `m_Targets` and `m_ConnectCamera` intact. The
  control task then discarded every later command, including `CMD_CONNECT`, so
  connect was dead until reboot. On timeout `disconnect()` now force-completes:
  it clears `m_ConnectCamera`, moves to `STATE_IDLE`, and logs the forced
  completion, so `Control` always ends in a recoverable state with an exit edge
  out of `STATE_DISCONNECTING`. `disconnect()` returns `false` only to report
  that the wait did not finish cleanly; `prepareRestart()` still logs and
  continues, and `UI::doDisconnect()` needs no return handling because the state
  is guaranteed recoverable.
- Target lifetime on force-complete. A target whose `m_Stopped` is still false at
  the timeout has a live task blocked inside `Camera::disconnect()`, and that
  task will still write `m_Stopped` through its own object when it returns.
  Freeing the object then would be a use-after-free that the allocator can hand
  to a reconnecting target, which is the brick-class corruption the design must
  avoid. So force-complete does not destroy unstopped targets. It moves them into
  `m_ZombieTargets` under the mutex and destroys only the already-stopped ones.
  The control task calls `reapZombieTargets()` each 50 ms tick and frees a zombie
  only once its `m_Stopped` has flipped, at which point the task has run
  `task_exit` and no longer touches the object. `~Target()` issues its camera
  disconnect only while `m_Stopped` is false, so neither the immediate destroy
  nor the reaper ever makes a radio call, and no radio call runs under
  `m_Mutex`.
- Undroppable `CMD_DISCONNECT` (plans/96 batch 1, item B2). `Target::sendCommand`
  previously used a zero-timeout `xQueueSend` that could silently drop a full
  queue. A dropped `CMD_DISCONNECT` stranded the target task in its command
  loop and hung the disconnect. `CMD_DISCONNECT` now resets the target queue and
  places itself at the front, so it is delivered immediately and cannot be lost.
  The transient shutter, focus, and GPS commands it clears are moot once
  teardown begins. This pairing is what makes the force-complete safe: a target
  that has not reported stopped is blocked inside `Camera::disconnect()`, past
  its last queue read, so destroying its queue in `~Target()` does not touch a
  task that is still waiting on it.
- The first infinite-reconnect retry waits 17 seconds. This gives a one second
  margin over the 16 second conn-saver case and is also longer than master's
  5.12 second timeout. Later retries retain the existing 5, 10, 20, 40, 80,
  and 120 second backoff schedule. One warning says that the camera may still
  hold the previous session.

## Restart-path inventory

| Path | Entry point | Shared behavior |
|---|---|---|
| Theme settings | `UI::addThemeMenu()` | `Platform::restart()` |
| Touch calibration | `CalibrationUI::calibrate()` | `Platform::restart()` |
| USB console | `cmdReboot()` | `Platform::restart()` |

There are no direct application calls to `esp_restart()` outside
`Platform::restart()`.

## Verification

Build both requested S3 environments:

```text
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug
```

Build status: the sandboxed worktree could not run PlatformIO. The
`m5stick-s3` release build was run on the harvest machine at commit time and
succeeded. The `m5stick-s3-debug` build remains to be run with hardware
verification.

On hardware:

1. Connect a Fujifilm camera and confirm the shutter page is active.
2. Restart from the theme settings page. Confirm the log shows the camera
   disconnect before reset and the camera releases the old link.
3. Repeat with touch calibration and with the console `reboot` command.
4. After each reset, leave autoconnect enabled and reconnect immediately. The
   camera must connect without waiting for its supervision timeout.
5. Interrupt a reconnect during the 17 second first retry wait. Confirm the
   existing abort path returns promptly and no Control mutex remains held.
6. Force an unclean reset or power loss while connected. Confirm the first
   retry warning appears once and the patient retry window reconnects after the
   camera releases its old session.

Only Fujifilm hardware is available for this verification. Other camera types
remain covered by code review and FauxNY tests.
