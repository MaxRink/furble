# 66: Camera pairing code on the furble display

## Motivation

During camera pairing, the camera can show a Bluetooth code that the furble user cannot see. The user therefore cannot compare the code or approve numeric comparison with confidence. Showing the requested code on furble fixes that visibility gap without adding a setting.

## Design

The `Camera` base class owns the NimBLE client security callbacks. It records passkey-display and numeric-comparison requests with the camera, connection handle, code, and a response deadline aligned to the 30 second SMP timeout. The callback publishes a request to the existing UI request queue and returns immediately, so the NimBLE host task is not blocked. The UI task owns one LVGL pairing modal shared with the existing companion prompt.

The camera modal shows the camera name and a large six-digit code. Numeric comparison has Confirm and Cancel. Passkey display has Cancel only. Timeout and cancellation reject the request and disconnect the client. A build with no handler registered, which is every `FURBLE_NO_DISPLAY` image, answers the request where it is raised with NimBLE's own default instead. When `FURBLE_CONSOLE` is enabled the code is printed, `pair no` cancels either prompt and `pair yes` confirms a numeric comparison; `pair yes` is refused on a passkey-display prompt, which has nothing to confirm.

## Verification

- Run `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug` and
  `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e waveshare-s3-eth`. The headless
  environment matters: it compiles the fall-through, not the modal.
- Ricoh is the only current vendor that explicitly requests `BLE_HS_IO_KEYBOARD_DISPLAY`. Its numeric-comparison callback is the current hardware-oriented path.
- A vendor using `BLE_HS_IO_DISPLAY_YESNO` can trigger numeric comparison through `onConfirmPasskey`.
- `onPassKeyDisplay` is the passkey-display path. No current non-Ricoh vendor explicitly selects that IO capability. `onPassKeyEntry` remains the input path and has no code to display.
- Fujifilm normally uses just-works pairing and should not produce a code. Use the virtual peers or a passkey-capable vendor to exercise the UI and callback paths.
- With `FURBLE_CONSOLE`, verify the printed code, `pair yes`, `pair no`, and timeout rejection.
- Hardware testing is pending. Only Fujifilm hardware is available for this repository.

## Implementation state

Implemented on `feat/66-pairing-code`, rebased onto master at ab638874 (after
#261 replaced the simulator connection fakes with the production Control,
Camera, CameraList and Scan over MockNimBLE). The Ricoh override now uses the
base callback chokepoint; the DJI Osmo override is preserved as an explicit,
sourced `autoAcceptPairing()` policy. Hardware verification remains pending:
see the hardware gate below.

### Rebase onto #261

`sim/shim/Camera.h` and `sim/FurbleControlSim.cpp` are deleted on the new
master, so this branch's 62-line shim pairing seam and its `FurbleControlSim`
mutex work were both dropped rather than revived. The mutex work has an
equivalent in the production `src/FurbleControl.cpp` already carried by this
branch. The seam was replayed onto the production `Furble::Camera`:
`hostSetPairingRequest()` and `hostExpirePairing()` are now compiled for
`FURBLE_HOST_TEST` or `FURBLE_SIM` and publish through the same
`publishPairingRequest()` the real `onConfirmPasskey` calls, against the live
client's `NimBLEConnInfo`. Everything downstream of that point is production
code running against MockNimBLE, so the simulator now proves what the answer
does on the wire and not only what the modal does on screen.

`tests/host/peer/RicohVirtualCamera.*` and `tests/host/nimble/MockNimBLE.*`
moved to `lib/testing/`, so the MITM handshake and injection plumbing this PR
adds is at the new paths. `connectRequested` and `CONNECT_REQUEST_GRACE_MS` from
#261 live in the same region of `connectTimerHandler` as this PR's connecting
camera read; they are independent, and `camera-pairing-cancel.txt` and
`camera-pairing-expiry.txt` both prove the connect-failed path still runs for a
link torn down by a pairing rejection (`control.connected` reaches zero and the
"Connection failed" branch runs `doDisconnect`).

## Headless boards

`src/CMakeLists.txt` excludes `src/FurbleUI.cpp` under `FURBLE_NO_DISPLAY`, and
`waveshare-s3-eth` is a release environment. Nothing there ever calls
`Camera::setPairingRequestCallback`, so a design that only records the request
and waits for a UI would strand the peer's SMP procedure and turn a connect that
worked on master into one that fails.

`Camera::publishPairingRequest` therefore falls through: when no handler is
registered, or the registered handler declines the request, the request is
answered where it was raised with `answerPairing(true)`. That is exactly what
NimBLE's own `NimBLEClientCallbacks::onConfirmPasskey` does
(`components/esp-nimble-cpp/src/NimBLEClient.cpp`), so a headless image keeps
the behaviour it had before this feature existed. The callback signature returns
`bool` for the same reason: `UI::sendRequest` returns false when the request
queue is full, and a queued-but-never-shown prompt has no timer to expire it
either, so that case takes the same fall-through.

`tests/host/camera_pairing_peer_test.cpp` covers both shapes against a real MITM
peer: with no handler registered, and with a handler that declines. Both must
complete the connection with exactly one injected accept.

Consequence for the console: on a headless console build the request is answered
before `pair yes` could ever reach it, so the `pair` command is only useful on a
build that has a display holding the modal open. `pair` on a passkey-display
prompt is refused outright, because that prompt has nothing to confirm.

## Response window

`PAIRING_WINDOW_MS` is 30000, not the 120000 this branch first used. NimBLE
abandons the pairing procedure after `BLE_SM_TIMEOUT_MS`, defined as 30000 in
`components/bt/host/nimble/nimble/nimble/host/src/ble_sm.c` of the pinned
ESP-IDF and armed by `ble_sm_proc_set_timer()`, which is the
30 second Security Manager timeout of Bluetooth Core Vol 3 Part H 3.4. An
injected answer after that goes nowhere, so a longer window would leave a
live-looking code on screen that can no longer authorize anything. At the
deadline the pairing timer rejects the request and disconnects, which fails the
connect and raises the existing "Connection failed" path, so the expiry is
visible rather than silent. `camera-pairing-expiry.txt` asserts the injected
answer is a reject and that the link goes down.

## Vendor pairing policy and sources

Removing the vendor overrides outright would have flipped every vendor from
NimBLE's auto-accept to requiring a user press, which is an unsourced protocol
change for vendors that have no comparison step at all. The policy is now
explicit, one virtual `Camera::autoAcceptPairing()` defaulting to false:

- **Ricoh**: prompts. The GR IV requests `BLE_HS_IO_KEYBOARD_DISPLAY` and uses
  MITM LE Secure Connections numeric comparison, and a 2026-08 bench run
  recorded the Ricoh callback producing comparison code `810510` against an
  integration image that showed nothing on screen. That capture is the source
  for showing the code, and it is the whole reason this plan exists.
- **DJI Osmo**: keeps auto-accept, declared in `lib/furble/DJIOsmo.h` with its
  source. DJI's own reference implementation, the
  [Osmo-GPS-Controller-Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo)
  this vendor was built from (plans/74-dji-osmo.md), never calls
  `esp_ble_gap_set_security_param`, sets no IO capability and no `ESP_LE_AUTH_*`
  requirement, and issues every GATT operation with `ESP_GATT_AUTH_REQ_NONE`
  ([ble/ble.c](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/ble/ble.c)).
  The link is just works. The approval the user gives is the protocol's own
  first-pair verification and camera approval response over `0xFFF5`, already
  implemented in `DJIOsmo::_connect`, not a Bluetooth passkey. There is no code
  to compare, so a prompt would be a dead end.
- **Everything else** (Canon, Sony, Nikon, Panasonic, Fujifilm, FauxNY) prompts.
  These negotiate just works in practice, so `onConfirmPasskey` does not fire
  and nothing changes for them. When one does negotiate numeric comparison the
  prompt is the correct behaviour and is precisely the gap the 2026-09-02 X100VI
  bench run exposed.

## Passkey display

`onPassKeyDisplay` publishes and returns `NimBLEDevice::getSecurityPasskey()`
rather than a constant compiled into furble. NimBLE injects that value for a
`BLE_SM_IOACT_DISP` action and only falls back to the callback's return value
while it is still the 123456 default, so sourcing it from the stack is what
keeps the screen and the wire in step: a build that configures its own passkey
displays that passkey. No randomisation is introduced here, so an unconfigured
build still displays 123456 and the display path carries no MITM protection.
`docs/ui-walkthrough.md` says so rather than implying a per-session value. The
peer test sets a non-default passkey and asserts both the published code and the
value handed to the stack, so the constant is no longer pinned by the test.

## Connect timer and Control's mutex

`Control::getConnectingCamera()` takes `m_Mutex`, and the old lock-free
`shared_ptr` copy was a real data race, so the lock stays. It must not become a
20 Hz lock on the LVGL task, which cuts against the deliberately lock-free
`isConnected()`. `Control` publishes `getConnectingCameraGeneration()`, a
lock-free counter bumped under `m_Mutex` on every `m_ConnectCamera` write;
`connectTimerHandler` reads that atomic every tick and takes the snapshot only
when it changes, holding the strong reference in `ConnectContext_t` to read
progress from. The reference is dropped as soon as the state leaves
`STATE_CONNECT`/`STATE_CONNECTING`, so the UI never holds the last reference to
a camera Control has released. `tests/host/control_connecting_snapshot_test.cpp`
remains the regression for the race itself.

## Hardware gate

Two bench checks remain, both needing a camera that actually produces a code:

1. Fujifilm X100VI, fresh pairing. Delete the furble bond and the camera-side
   pairing entry, then pair from furble. The camera shows its confirmation
   code; furble must show the same six digits on the pairing modal, and
   Confirm must complete the pairing. On the 2026-09-02 bench run against
   master firmware furble showed nothing here, which is the regression this
   plan closes. Fujifilm normally negotiates just works, so if no code appears
   on either screen the run proves nothing and should be repeated after
   clearing both bonds.
2. Ricoh GR IV, numeric comparison. This is the vendor that requests
   `BLE_HS_IO_KEYBOARD_DISPLAY`, so it is the only path that raises the modal
   through a real MITM handshake. Confirm the code furble shows matches the
   camera, that Confirm completes the pairing, and that Cancel rejects it and
   disconnects rather than pairing anyway.

With `FURBLE_CONSOLE` both runs can be scripted over the USB console on a
`-debug` image. The exact steps:

1. `settings set fauxNY 0`, then `cameras list` to confirm the target is saved.
2. Clear the bond on both sides. The console has no delete, so use the Delete
   menu on the device to drop the saved camera, then `reboot`; on the camera
   clear its pairing entry in its Bluetooth menu. Without this the peers reuse
   their keys and no security callback fires. Re-add the camera with `scan`
   followed by `connect <index>`.
3. Watch the console during that connect. A numeric comparison prints
   `pair.confirm: NNNNNN`; a passkey display prints `pair.display: NNNNNN`.
   Nothing printed means the peer negotiated just works and the run proves
   nothing, so repeat step 2.
4. Compare the printed six digits with the camera screen and the furble modal.
   All three must match.
5. `pair yes` within 30 seconds completes the pairing; `status` then reports the
   camera connected. On a `pair.display` prompt `pair yes` is refused with
   "passkey display prompt, use pair no to cancel"; type the printed code on the
   camera instead.
6. Repeat from step 2 and answer `pair no`. The link must drop and `status` must
   report no connected camera, not a completed pairing.
7. Repeat from step 2 and answer nothing. After 30 seconds the modal must close
   by itself, the connect must fail, and `status` must report no connected
   camera.

Resolved: the pairing modal now captures the active encoder group focus before it opens and restores it after it closes. Both `showCompanionPairing` and `showCameraPairing` save `lv_group_get_focused(m_Group)` into `m_PairingPrevFocus` before the modal footer buttons take focus, and the unified `closePairingDialog` restores it via `lv_group_focus_obj` guarded by `lv_obj_is_valid`, then clears the member. Because every close path (Confirm, Cancel, timeout, disconnect, camera-side cancel) funnels through `closePairingDialog`, this single restore point covers both modals. This is the canonical unified focus-restore fix for the companion and camera pairing modals. It clears the focus-restore bug that hardware testing root-caused in the companion pairing modal that this modal generalizes.

Resolved: the camera modal now adds its Confirm and Cancel footer buttons to the encoder input group and focuses the primary action, mirroring `showCompanionPairing`. Before this the camera modal created footer buttons but never added them to `m_Group`, so on an encoder-only board the modal took no input and, once it closed, the page below was left with focus on a deleted button. Confirm is focused for numeric comparison, otherwise the sole Cancel button is focused.

Resolved: `m_PairingCamera` is now a `std::weak_ptr<Camera>` instead of a raw pointer. Cameras are owned by `Control` through `shared_ptr` (plan 106), so a disconnect can free the camera while the modal is still open. `showCameraPairing` resolves the incoming raw pointer against `Control`'s connecting camera and active targets and stores the owning `shared_ptr` as a weak reference. The footer callbacks and the pairing timer `lock()` it and treat an expired reference as gone, which closes the modal instead of dereferencing freed memory. A separate `m_PairingIsCamera` flag distinguishes a camera modal from a companion modal because an expired weak reference cannot.

Resolved: entry-dereference use-after-free hardening in `showCameraPairing`. The raw `Camera*` reaches this function from a request enqueued on the NimBLE host task, so a disconnect that frees the camera between the callback enqueue and the UI drain would make any early dereference a use-after-free. The function previously called `hasPendingPairing`, `pairingTimedOut`, `getPairingType`, and `getPairingCode` on the raw pointer before resolving it against the shared_ptrs `Control` owns. It now resolves the owning `shared_ptr` first and, if the pointer is no longer owned, treats the camera as gone and returns without touching it. Every subsequent read (`hasPendingPairing`, `pairingTimedOut`, `cancelPairing`, `getPairingType`, `getPairingCode`, `getName`) goes through the locked `shared_ptr`, so the object cannot be freed underneath the modal setup. `closePairingDialog` also regained the `lv_obj_is_valid(m_PairingDialog)` guard before `lv_msgbox_close_async`, matching the companion and sim-query paths that guard the same handle, and clears the handle unconditionally afterward. This is Ricoh-companion only and hardware-untestable in this repository.

The pairing timer now records a `FURBLE_SIM_TIMER_FIRE("pairing_timer")` beat like every other periodic UI timer, so the simulator power model and timer accounting see it fire. It is created or resumed only while a companion or camera request can be pending, then paused after a camera request closes or disappears, or when the companion service is disabled. A camera callback starts it when the UI request queue is serviced, so idle devices do not pay a permanent 250 ms wakeup cost.

Scope and hardware status: this change is Ricoh-companion only. Ricoh is the only current vendor that requests numeric-comparison pairing, so it is the only path that raises the camera modal on real hardware. Only Fujifilm cameras are available in this repository and they use just-works pairing that never produces a code, so the real Ricoh passkey handshake cannot be exercised here.

## Bench evidence that motivates this

2026-09-02, Fujifilm X100VI on current master firmware: during a fresh pairing
the camera displayed a numeric confirmation code and furble displayed nothing.
The pairing still succeeded, so nothing was broken, but the user had no way to
compare the code. That is the visibility gap this plan closes. The same gap is
the whole handshake on a Ricoh GR IV, which uses MITM LE Secure Connections
numeric comparison and cannot pair at all without a visible comparison.

## What is now sim-proven vs the residual hardware check

The modal render, the layout, and the answer that reaches the stack no longer need hardware. After the #261 rebase the simulator compiles the production `Camera`, `Control` and `MockNimBLE`, so a scenario with a connected virtual peer drives the real `showCameraPairing`, the real pairing timer, and the real `answerPairing` down to `NimBLEDevice::injectConfirmPasskey`.

- Seam: `Camera::hostSetPairingRequest()` (`FURBLE_HOST_TEST || FURBLE_SIM`) substitutes only the controller event. It publishes through the same `publishPairingRequest()` the real `onConfirmPasskey` calls, using the live client's `NimBLEConnInfo`, so Confirm and Cancel inject for real. `tests/host/camera_pairing_peer_test.cpp` covers the callback entry the seam skips. The scenario action `camera-pair-request [confirm|display] <code>` and the query keys below are inside `#if defined(FURBLE_SIM)`, so the release binary is unchanged.
- Query keys (`UI::simQueryState`, `#if defined(FURBLE_SIM)`): `pairing_code` reads the six-digit code from the live LVGL label, `pairing_kind` reports `confirm` or `display`, `pairing_overflow` reports whether the modal box or its content extends past the panel, `pairing_pending` reports whether any camera Control owns still holds a request, and `pairing_timer` reports whether the 250 ms poll is armed. `ble.pairing_answers` and `ble.pairing_answer` (`sim/driver.cpp`) read the injection tally straight off MockNimBLE, which is the wire-level truth the modal closing says nothing about.
- Typed action: `camera-pair-request` is a first-class `scenario_action_kind_t::PAIRING_REQUEST` in the canonical parser, so the prompt kind and the six-digit code are validated at the scenario boundary and revalidated before dispatch, like every other action. `camera-pair-accept`, `camera-pair-reject` and `camera-pair-expire` are canonical simple actions. Every one of them reports `m_SimActionResult`, so a scenario can assert `applied` or `expect no-effect`.
Every scenario below seeds `ble_peers fuji` (or `fuji-pair`) plus `ble_saved`, so the camera behind the prompt is a production `Camera` on a real MockNimBLE link.

- Render scenario: `sim/scenarios/e2e/camera-pairing-code.txt` connects a virtual Fujifilm peer, raises a numeric-comparison request, asserts the modal is open, the kind is `confirm`, the rendered code matches and there is no overflow, then Confirms and asserts the injected answer is an accept and the link stays up. All three boards, so `run-e2e.sh` proves the code renders and the layout fits at 135x240, 80x160 and 320x240.
- Cancel scenario: `sim/scenarios/e2e/camera-pairing-cancel.txt` is the one that pins the feature's reason to exist. Cancel must reach NimBLE as a reject (`ble.pairing_answer reject`, exactly one answer) and the link must go down (`control.connected` reaches zero with the radio disabled so it cannot come back). All three boards.
- Expiry scenario: `sim/scenarios/e2e/camera-pairing-expiry.txt` drives the deadline path and asserts the same reject and the same teardown, so an expiry cannot quietly become an accept. All three boards.
- State scenario: `sim/scenarios/e2e/camera-pairing-state.txt` covers the passkey-display prompt, proves a duplicate request cannot replace the code already shown and is itself refused at the wire, proves an accept on a display prompt has no button to click, and proves cancelling a display prompt injects no comparison answer while still dropping the link. All three boards.
- Multi-camera scenario: `sim/scenarios/e2e/camera-pairing-multi.txt` connects two peers and raises a request on each. The visible modal keeps its own code, and answering it must hand the modal to the waiting request rather than orphan it. All three boards.
- Physical-button scenario: `sim/scenarios/e2e/camera-pairing-notouch.txt` seeds `no_touch` so the shipped physical-button layout is live, then asserts the modal fits, the code renders, the page underneath reports `ui.overflow no`, the rejection reaches the stack, and the focus is restorable after the modal closes. The two narrow non-touch panels, 135x240 and 80x160.
- Malformed requests are now rejected at the scenario boundary rather than at runtime, so they live as `invalid` fixtures: `action-camera-pair-short-code`, `action-camera-pair-long-code`, `action-camera-pair-kind` and `action-camera-pair-trailing`, each expecting exit 2, plus accept and reject cases in `tests/host/sim_action_parser_test.cpp` including forged `scenario_action_t` values that bypass the parser.
- Layout fix the sim caught: the default LVGL msgbox width is fixed and rendered the modal 259 px wide, overflowing both the 135 px and 80 px panels, and the full content overflowed the 160 px height of the 80x160 M5StickC. `showCameraPairing` now caps the modal width to the panel (`max_width = m_Width - 4`) and drops the wrapped instruction line on the short 80x160 panel (`m_Height < 170`). With these the modal fits every panel; the sim overflow assertion is the guard.
- Second layout fix, caught by reading the committed screenshot: the box fit the panel but the footer did not fit the box, so LVGL shrank the buttons and clipped "Cancel" to "Cance". `ui.pairing_overflow` now also reports `yes` when the footer scrolls or any footer label is wider than the button content it has to fit in, which is the assertion that catches a clipped action name. `showCameraPairing` trims the footer padding and drops the button font one step on panels narrower than 200 px, so both labels render whole on 135x240 while the 320x240 Core keeps the default look. Two action labels do not fit the 80 px M5StickC at any padding or font, so that panel answers the comparison with "Yes" and "No"; a passkey-display prompt has one button and keeps the full "Cancel" everywhere.

  The companion pairing modal has the same footer on the same panels and was not changed here, because #77 is already in that function. It should adopt the same fit when the two land.

## Host coverage of the real security callbacks

`tests/host/camera_pairing_peer_test.cpp` drives the flow through the real
NimBLE security callbacks rather than the host state seam. The Ricoh virtual
peer now models the MITM half of LE Secure Connections: `Config::pairing_code`
makes `secureConnection()` raise `onConfirmPasskey` (or `onPassKeyDisplay`) on
the production `Camera` and block until the central injects an answer, exactly
as the NimBLE host task does. The mock routes `NimBLEDevice::injectConfirmPasskey`
and `injectPassKey` back to that peer and counts them, so a test can assert the
answer reached NimBLE and whether it was an accept.

The test covers three cases with the connect running on one thread and the
answer sent from another, the two threads the device has:

- numeric comparison accepted: the published code is the code the camera
  generated, `answerPairing(true)` injects exactly one accept, and the
  connection completes.
- numeric comparison rejected: `cancelPairing()` injects exactly one reject and
  the connection fails instead of silently pairing.
- passkey display: the prompt is published once with `PASSKEY_DISPLAY` and the
  code furble shows, no numeric-comparison answer is injected, and
  authentication completion clears the prompt.

The test also covers the three cases the review found unguarded:

- an expired request answered with Confirm injects a reject, not an accept;
- a request raised with no handler registered, and one whose handler declines,
  are both answered with NimBLE's default accept and complete the connection;
- an answer recorded against a connection handle that is no longer the client's
  is never injected, and drops the link it cannot authorize. MockNimBLE now
  hands out a real, unique connection handle per link so that guard has
  something to compare.

`tests/host/camera_pairing_test.cpp` additionally asserts that the duplicate
guard and the malformed-code guard each inject exactly one reject, so neither
can be deleted without a red test.

`tests/host/console_commands_test.cpp` registers `pair` in the command
contract and answers a request raised on a live `Control` target, which is the
case the `getTargetCameras()` walk in `cmdPair` exists for. It also pins the
refusal of `pair yes` on a passkey-display prompt.

Mutation checks: see the table in the PR discussion. Every mutation the review
listed as surviving now fails at least one named assertion.

Residual hardware-only check: a Ricoh camera producing a real numeric-comparison passkey, to confirm the code furble shows matches the camera and that Confirm and Cancel drive the real NimBLE passkey response and disconnect. The render, the layout at every panel width, the modal focus contract, and the shared-ownership use-after-free hardening are host-proven; hardware only confirms the real passkey exchange. This work is declared untested on the Ricoh BLE path pending Ricoh hardware.

The camera state machine also rejects malformed or out-of-range callback codes, rejects a second request while one is visible so the code being confirmed cannot differ from the code displayed, and rejects an answer whose connection handle is stale. These guards remain hardware-pending for the real NimBLE callback exchange.

## Fuzz seed 3 on the 320x240 Core

Master already promoted this seed out of the expected-fail pin in #270 (plan
166), for the same reason this branch would have: the walk no longer reaches the
intervalometer layout overflow at step 447. This branch measured the same thing
independently before that landed, on its own 320x240 build of both trees:

- master f425fd3: `FUZZ FINDING [layout-overflow] step=447 page=timer`,
  `observed_delta=245`, exit 1.
- this branch on that base: `findings=0`, `observed_delta=248`, exit 0.

Two independent causes shift the same walk. The pairing work moves the fuzzer's
`companion-request` and `companion-answer` events onto the unified
`closePairingDialog` and the shared pairing timer; plan 166 changes how far the
control task gets per UI slice. Either alone diverges the sequence before step
447. The page itself is untouched by both, and seeds 4, 5, 6, 8, 11, 13, 17 and
23 were checked on the Core build of this branch without reaching it, so no
replacement pin is offered here either. The gap stays where #270 left it.

## Known flake, not caused by this branch

`sim-scheduler` failed one of three serial runs on the unmutated tree during
review. It is a pre-existing host scheduler flake, unrelated to pairing, and is
not fixed here. It is recorded so a future ambiguous mutation run is not blamed
on this change.
