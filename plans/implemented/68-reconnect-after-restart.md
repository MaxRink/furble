# Reconnect after restart

Status: implemented in `feat/68-reconnect-restart`, disconnect lifecycle
redesigned after the first hardware run crashed. Hardware verification remains to
be re-run on the M5StickS3 with a Fujifilm camera. The redesign is not claimed
hardware-safe; it awaits re-review and re-verification.

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

## Hardware failure and disconnect lifecycle redesign

The first hardware run of this branch passed normal single
connect/shutter/disconnect and the restart path, but cancel-mid-connect and a
connect/disconnect storm crashed reliably with `LoadProhibited` on
`0xcececece` freed-heap poison. Two use-after-frees were decoded:

1. Connect side: `Control::connectAll` to `Camera::connect` to
   `Fujifilm::subscribe` to `NimBLEClient::getService` on a freed client. A
   fresh connect raced the still-tearing-down prior session over shared NimBLE
   state.
2. Disconnect side: a quarantined `Control::Target::task` to `Camera::disconnect`
   to `Fujifilm::_disconnect` to `NimBLEClient::disconnect` on a freed client.

Root cause: the `NimBLEClient` is owned by NimBLE, not by `Camera`.
`Camera::connect()` creates it with `setSelfDelete(true, true)`, so NimBLE frees
it on the host task right after the disconnect callback, and synchronously
inside `NimBLEClient::connect()` when a connect attempt fails. Neither path
nulls `Camera::m_Client`, so it becomes a dangling pointer. The first design
force-completed a disconnect on a one second timeout and returned, letting a
reconnect proceed while that async teardown was still in flight. Keeping the
`Target` object alive (the quarantine) did not protect the `NimBLEClient` and
`Camera` internals it points at; those were freed and reused underneath the
still-running teardown.

The redesign closes both:

- Force-completing (free and return while teardown is in flight) is safe only on
  the restart path, where `esp_restart()` runs immediately after and the reset
  kills the in-flight teardown, so nothing can reconnect and race it. That is now
  the only caller that opts in.
- The interactive and reconnect path waits for the teardown to actually complete
  before it clears targets and returns to `STATE_IDLE`, so no later connect can
  race a client that is still being freed.
- A new connect does not start while a prior teardown is still draining.
- `Camera` never dereferences a self-deleted `NimBLEClient`: the two disconnect
  derefs are gated on `m_Connected`, which is always false once the client has
  been freed.

## Design

- `Platform::restart()` is the only application restart entry point. It calls
  `prepareRestart()`, which asks `Control` to stop all target tasks and cameras,
  waits up to one second, logs a timeout if needed, and disables the StickS3
  watchdog. A timeout never prevents the reset.
- `Control::disconnect(timeout_ms, forRestart)` keeps the existing abort and GAP
  cancel behavior and sends the undroppable `CMD_DISCONNECT` to every target
  under `m_Mutex`. The target task performs `Camera::disconnect()` before it
  reports stopped. The wait runs in short slices and never holds `m_Mutex`
  across a slice. On the clean path it clears targets only after target tasks,
  the connection attempt, and camera connection state have all completed
  (`disconnectComplete()`).
- Interactive disconnect waits for real completion. `forRestart == false` (the
  `UI::doDisconnect()` caller) waits up to `DISCONNECT_WAIT_MAX_MS` (30 s, sized
  to the connect timeout) for `disconnectComplete()`. A normal teardown finishes
  in well under a second once the aborting connect unwinds; the large bound is
  only a backstop against a genuinely stuck NimBLE teardown, never the normal
  exit. This closes the first design's wedge by completing the teardown, not by
  force-freeing, so a later connect never races a still-freeing client. The wait
  runs on the LVGL task, so each slice feeds the M5PM1 watchdog and holds no
  mutex. It is acceptable for an interactive disconnect to take longer; it must
  never crash and never wedge `STATE_DISCONNECTING` permanently.
- Restart-only force-complete. `forRestart == true` (only `prepareRestart()`)
  waits up to the one second `DISCONNECT_TIMEOUT_MS` and then force-completes.
  This is safe only here because `esp_restart()` runs immediately after and the
  reset kills the in-flight teardown, so nothing reconnects to race it.
  `disconnect()` returns `false` to report the wait did not finish cleanly;
  `prepareRestart()` logs and continues.
- Connect gated on teardown drained. The control task does not leave
  `STATE_CONNECT` for `connectAll()` while any force-completed target is still
  quarantined in `m_ZombieTargets` (`teardownDraining()`). A fresh
  `NimBLEDevice::createClient()` there would race the client a zombie's teardown
  task is still releasing, the connect-side use-after-free. The task stays in
  `STATE_CONNECT` and retries on the next tick; `reapZombieTargets()` clears the
  drain once each teardown task has stopped.
- Target lifetime on force-complete (unchanged, now reached only on the restart
  path or the interactive backstop). A target whose `m_Stopped` is still false at
  the timeout has a live task inside `Camera::disconnect()`, and that task will
  still write `m_Stopped` through its own object when it returns. Freeing it then
  would be a use-after-free the allocator can hand to a reconnecting target. So
  force-complete moves unstopped targets into `m_ZombieTargets` under the mutex
  and destroys only the already-stopped ones. `reapZombieTargets()` frees a
  zombie only once its `m_Stopped` has flipped, at which point the task has run
  `task_exit` and can never touch the object again. `~Target()` issues its camera
  disconnect only while `m_Stopped` is false, so neither the immediate destroy
  nor the reaper makes a radio call, and no radio call runs under `m_Mutex`.
- `Camera` is robust to `NimBLEClient` self-deletion. `Camera::disconnect()` and
  the failed-connect cleanup in `Camera::connect()` call `_disconnect()` only
  when `m_Connected` is true. When it is false the client has already
  self-deleted (on a disconnect callback or a failed connect) and `m_Client`
  dangles, so the guard prevents the disconnect-side use-after-free. Every other
  live-link deref of `m_Client` was already gated on `m_Connected`.
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
7. Cancel mid-connect: start a connect and cancel it while pairing and
   subscription are still running. Confirm no crash and that a following connect
   succeeds.
8. Disconnect storm: repeat connect then immediate disconnect many times in
   quick succession. Confirm no `LoadProhibited` or freed-heap poison crash, that
   each interactive disconnect returns only after the teardown completed, and
   that the device never wedges in `STATE_DISCONNECTING`.

These two scenarios crashed the first design; they are the primary regression
targets for the redesign. The redesign is not claimed hardware-safe until this
run passes.

Only Fujifilm hardware is available for this verification. Other camera types
remain covered by code review and FauxNY tests.
