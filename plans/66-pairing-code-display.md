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

Implemented on `feat/66-pairing-code`. The Ricoh overrides now use the base callback chokepoint. Hardware verification remains pending.

Resolved: the pairing modal now captures the active encoder group focus before it opens and restores it after it closes. Both `showCompanionPairing` and `showCameraPairing` save `lv_group_get_focused(m_Group)` into `m_PairingPrevFocus` before the modal footer buttons take focus, and the unified `closePairingDialog` restores it via `lv_group_focus_obj` guarded by `lv_obj_is_valid`, then clears the member. Because every close path (Confirm, Cancel, timeout, disconnect, camera-side cancel) funnels through `closePairingDialog`, this single restore point covers both modals. This is the canonical unified focus-restore fix for the companion and camera pairing modals. It clears the focus-restore bug that hardware testing root-caused in the companion pairing modal that this modal generalizes.

Resolved: the camera modal now adds its Confirm and Cancel footer buttons to the encoder input group and focuses the primary action, mirroring `showCompanionPairing`. Before this the camera modal created footer buttons but never added them to `m_Group`, so on an encoder-only board the modal took no input and, once it closed, the page below was left with focus on a deleted button. Confirm is focused for numeric comparison, otherwise the sole Cancel button is focused.

Resolved: `m_PairingCamera` is now a `std::weak_ptr<Camera>` instead of a raw pointer. Cameras are owned by `Control` through `shared_ptr` (plan 106), so a disconnect can free the camera while the modal is still open. `showCameraPairing` resolves the incoming raw pointer against `Control`'s connecting camera and active targets and stores the owning `shared_ptr` as a weak reference. The footer callbacks and the pairing timer `lock()` it and treat an expired reference as gone, which closes the modal instead of dereferencing freed memory. A separate `m_PairingIsCamera` flag distinguishes a camera modal from a companion modal because an expired weak reference cannot.

Resolved: entry-dereference use-after-free hardening in `showCameraPairing`. The raw `Camera*` reaches this function from a request enqueued on the NimBLE host task, so a disconnect that frees the camera between the callback enqueue and the UI drain would make any early dereference a use-after-free. The function previously called `hasPendingPairing`, `pairingTimedOut`, `getPairingType`, and `getPairingCode` on the raw pointer before resolving it against the shared_ptrs `Control` owns. It now resolves the owning `shared_ptr` first and, if the pointer is no longer owned, treats the camera as gone and returns without touching it. Every subsequent read (`hasPendingPairing`, `pairingTimedOut`, `cancelPairing`, `getPairingType`, `getPairingCode`, `getName`) goes through the locked `shared_ptr`, so the object cannot be freed underneath the modal setup. `closePairingDialog` also regained the `lv_obj_is_valid(m_PairingDialog)` guard before `lv_msgbox_close_async`, matching the companion and sim-query paths that guard the same handle, and clears the handle unconditionally afterward. This is Ricoh-companion only and hardware-untestable in this repository.

The pairing timer now records a `FURBLE_SIM_TIMER_FIRE("pairing_timer")` beat like every other periodic UI timer, so the simulator power model and timer accounting see it fire.

Scope and hardware status: this change is Ricoh-companion only. Ricoh is the only current vendor that requests numeric-comparison pairing, so it is the only path that raises the camera modal on real hardware. The three fixes here are not hardware-testable in this repository: only Fujifilm cameras are available and they use just-works pairing that never produces a code or a modal. The simulator stubs the pairing API to a permanently idle state, so the camera modal cannot be raised in the sim either; the companion modal focus-contract scenario covers the shared focus and re-entrancy path but not the Ricoh camera path. This work is declared untested pending Ricoh hardware.
