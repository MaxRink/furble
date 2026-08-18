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
