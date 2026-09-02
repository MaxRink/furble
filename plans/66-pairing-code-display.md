# 66: Camera pairing code on the furble display

## Motivation

During camera pairing, the camera can show a Bluetooth code that the furble user cannot see. The user therefore cannot compare the code or approve numeric comparison with confidence. Showing the requested code on furble fixes that visibility gap without adding a setting.

## Design

The `Camera` base class owns the NimBLE client security callbacks. It records passkey-display and numeric-comparison requests with the camera, connection handle, code, and a two-minute response deadline. The callback publishes a request to the existing UI request queue and returns immediately, so the NimBLE host task is not blocked. The UI task owns one LVGL pairing modal shared with the existing companion prompt.

The camera modal shows the camera name and a large six-digit code. Numeric comparison has Confirm and Cancel. Passkey display has Cancel only. Timeout and cancellation reject the request and disconnect the client. When `FURBLE_CONSOLE` is enabled, the code is printed and `pair yes` or `pair no` answers the pending request.

## Verification

- Run `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3`.
- Ricoh is the only current vendor that explicitly requests `BLE_HS_IO_KEYBOARD_DISPLAY`. Its numeric-comparison callback is the current hardware-oriented path.
- A vendor using `BLE_HS_IO_DISPLAY_YESNO` can trigger numeric comparison through `onConfirmPasskey`.
- `onPassKeyDisplay` is the passkey-display path. No current non-Ricoh vendor explicitly selects that IO capability. `onPassKeyEntry` remains the input path and has no code to display.
- Fujifilm normally uses just-works pairing and should not produce a code. Use the FauxNY fake test path or a passkey-capable vendor to exercise the UI and callback paths.
- With `FURBLE_CONSOLE`, verify the printed code, `pair yes`, `pair no`, and timeout rejection.
- Hardware testing is pending. Only Fujifilm hardware is available for this repository.

## Implementation state

Implemented on `feat/66-pairing-code`, rebased onto master at 82f0740a. The
Ricoh and DJI Osmo overrides now use the base callback chokepoint. Hardware
verification remains pending: see the hardware gate below.

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

With `FURBLE_CONSOLE` both runs can be scripted: the code is printed as
`pair.confirm: NNNNNN` or `pair.display: NNNNNN` and `pair yes` / `pair no`
answers the pending request.

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

The modal render and layout no longer need hardware. The simulator drives the real `showCameraPairing` and the real pairing timer through a `FURBLE_SIM`-gated seam, so the code and layout are proven on host.

- Seam: the sim shim `Camera` (`sim/shim/Camera.h`) now holds a settable pending-pairing state instead of a permanent idle stub, and a `camera-pair-request [confirm|display] <code>` scenario action (`UI::simScenarioAction`, `#if defined(FURBLE_SIM)`) injects it on Control's connecting camera. The real pairing timer then raises the same modal `showCameraPairing` builds on device. No shipping firmware path is changed by the seam: the action and the query keys below are all inside `#if defined(FURBLE_SIM)`, so the release binary is unchanged.
- Query keys (`UI::simQueryState`, `#if defined(FURBLE_SIM)`): `pairing_code` reads the six-digit code from the live LVGL label, `pairing_kind` reports `confirm` or `display`, and `pairing_overflow` reports whether the modal box or its content extends past the panel.
- Typed action: `camera-pair-request` is a first-class `scenario_action_kind_t::PAIRING_REQUEST` in the canonical parser, so the prompt kind and the six-digit code are validated at the scenario boundary and revalidated before dispatch, like every other action. `camera-pair-accept`, `camera-pair-reject` and `camera-pair-expire` are canonical simple actions. Every one of them reports `m_SimActionResult`, so a scenario can assert `applied` or `expect no-effect`.
- Scenario: `sim/scenarios/e2e/camera-pairing-code.txt` connects a FauxNY camera, injects a numeric-comparison pairing, and asserts the modal is open, the kind is `confirm`, the rendered code matches, and there is no overflow. It is registered in `sim/scenarios/manifest.json` for all three boards, so `run-e2e.sh` runs it on 135x240, 80x160 and 320x240 and the code renders and the layout fits at every panel width.
- State scenario: `sim/scenarios/e2e/camera-pairing-state.txt` covers passkey-display and numeric-comparison prompts, proves a duplicate callback cannot replace the code already shown, proves an accept on a display prompt has no button to click, and drives cancel, accept, and expiry through the real modal handlers. The simulator shim models the two-minute deadline and has an explicit expiry seam so this runs deterministically without a two-minute wall-clock wait. Registered for all three boards.
- Physical-button scenario: `sim/scenarios/e2e/camera-pairing-notouch.txt` seeds `no_touch` so the shipped physical-button layout is live, then asserts the modal fits, the code renders, the page underneath reports `ui.overflow no`, and the focus is restorable after the modal closes. Registered for the two narrow non-touch panels, 135x240 and 80x160.
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

`tests/host/console_commands_test.cpp` registers `pair` in the command
contract and answers a request raised on a live `Control` target, which is the
case the `getTargetCameras()` walk in `cmdPair` exists for.

Mutation checks (each reverted after):

- `injectConfirmPasskey(connInfo, accepted)` forced to `true`: the peer test
  fails on "the injected answer is a reject".
- `onConfirmPasskey` publishing `pin + 1`: the peer test fails on "the
  displayed code is the code the camera generated".
- the modal `max_width` cap removed: `camera-pairing-code.txt` fails on
  `ui.pairing_overflow expected 'no' got 'yes'`.

Residual hardware-only check: a Ricoh camera producing a real numeric-comparison passkey, to confirm the code furble shows matches the camera and that Confirm and Cancel drive the real NimBLE passkey response and disconnect. The render, the layout at every panel width, the modal focus contract, and the shared-ownership use-after-free hardening are host-proven; hardware only confirms the real passkey exchange. This work is declared untested on the Ricoh BLE path pending Ricoh hardware.

The camera state machine also rejects malformed or out-of-range callback codes, rejects a second request while one is visible so the code being confirmed cannot differ from the code displayed, and rejects an answer whose connection handle is stale. These guards remain hardware-pending for the real NimBLE callback exchange.
