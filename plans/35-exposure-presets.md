# 35: Exposure time presets in 1/3 stops

## Goal

Select bulb and intervalometer shutter durations from the photographic 1/3
stop series with plus and minus stepping, instead of dialing digits.

## Motivation

Upstream issue gkoh/furble#244 tracks this. When the camera is in bulb mode
the remote controls the exposure time. Photographers adjust exposure in 1/3
stop steps, so stepping 8 s to 10 s to 13 s is one action each. Dialing
three digits per change is slow in the dark with cold fingers. The preset
UI existed before the LVGL rewrite and was lost. The maintainer confirmed
in #244 that the issue now tracks exactly this.

## Upstream coordination

No new issue. This PR closes #244. Comment on #244 before starting, noting
the planned approach and that the bulb page from #292 gets the same picker.

## Scope

One PR.

- A preset stepping mode for duration selection using the standard series:
  1, 1.3, 1.6, 2, 2.5, 3.2, 4, 5, 6, 8, 10, 13, 15, 20, 25, 30, 40, 50,
  60, 80, 100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000 seconds.
- Applies to the bulb duration picker (plans/04) only. Review narrowed the
  scope: the intervalometer Shutter is a button-press duration, not an
  exposure time, so it keeps the digit spinner along with Count, Delay and
  Wait. The original enable-time snap also rewrote and persisted the stored
  Shutter value (default 30 ms became 1 s) on every boot, which the
  bulb-only scope and lazy snapping remove.
- Toggle between digit entry and preset stepping so arbitrary values stay
  possible. Default stays digit entry, current behavior.
- Preset stepping UI: the value shown large, plus and minus step one series
  entry, hold to repeat. On touch boards plus and minus are buttons, on
  stick boards the two outer keys step and the middle key confirms.

## Files to change

- src/FurbleUIIntervalometer.cpp, src/FurbleUIBulb.cpp: picker mode.
- include/FurbleUI.h: series table, picker mode state.
- include/FurbleSettings.h, src/FurbleSettings.cpp: PRESET_PICKER bool,
  NVS key preset_picker, default false.

Line anchors depend on the merged state of feat/04, resolve at
implementation time.

## Implementation notes

- Store the selected value in the existing interval_t and BULB nvs_t
  representations, seconds with the fractional values 1.3, 1.6, 2.5, 3.2
  stored as milliseconds. No storage format change.
- Never snap and save on enable or at boot. Entering preset mode with a
  value not in the series shows the stored value unchanged and only tracks
  the nearest series entry; the first step in the picker snaps onto the
  series and saves. Switching the picker off snaps a non-representable
  value (fractional seconds, 1000 s) to the nearest digit-editor value
  before the rollers render, without persisting until the user changes
  something.
- The series is a static constexpr array. No allocation.

## Risks

- Low. UI only, no BLE or camera change.
- Sub second series entries below 1 s are out of scope, the series starts
  at 1 s as listed in #244.

## Verification

- Build matrix, all five envs.
- On the StickS3: step through the series in both pickers, confirm
  boundary behavior at 1 and 1000, confirm digit mode unchanged, confirm
  a preset value round trips through NVS after reboot.
- With the X100VI in bulb mode: 10 s preset exposure, then step to 13 s,
  confirm the exposure lengths on camera.

## Dependencies

- plans/04 (bulb page, merged as feat/04-bulb-timer, upstream #292).
- None otherwise.

## References

- https://github.com/gkoh/furble/issues/244
- https://github.com/gkoh/furble/pull/292

## Implementation status

Implemented the preset picker for the bulb Duration field only. Review
narrowed the scope from the original bulb-plus-shutter plan: the
intervalometer Shutter is a button-press duration, not an exposure time,
and the enable-time snap-and-save destroyed its stored value on every
boot. The Shutter field keeps the plain digit spinner.

Rebase notes:

- `PRESET_PICKER` is assigned wire_id 30, continuing after `CONN_SAVER` (29)
  from PR 24.
- `src/FurbleCompanion.cpp` settingType and settingValue cover
  `PRESET_PICKER` as SETTING_BOOL.
- Console `appliesImmediately` stays false: the UI applies its own toggle
  live, a console or companion write applies on reboot, matching the
  console "applies: on reboot" output.

- Added the 31-entry 1/3-stop series in milliseconds.
- Added the `PRESET_PICKER` bool setting with the `preset_picker` NVS key and
  a false default. The setting is available under Features (appended last)
  and through the debug settings console.
- Digit rollers remain the default. Enabling preset mode never modifies or
  saves the stored value; the picker shows the stored value and the first
  step snaps onto the series and saves. Disabling the picker snaps
  non-digit-representable values (fractional seconds, 1000 s) to the
  nearest whole-unit value before the rollers render, unsaved until the
  next user edit, so the rollers never receive an out-of-range index.
- The digit rollers stay in their input group while hidden. LVGL 9 group
  navigation skips objects with a hidden ancestor, so toggling the picker
  does not scramble the traversal order.
- Touch boards get plus and minus buttons, added to the input group so the
  synthesized A/B/C buttons can reach them. Stick boards hide the buttons
  and use the outer keys for minus and plus, the middle key confirms.
  Pressed and long-press-repeat events provide one-step and hold-to-repeat
  behavior.
- Preset values use the existing packed NVS representation. Whole seconds use
  the seconds unit. Fractional entries use milliseconds. No storage format
  changed.

The plan called for the full five-environment release build matrix. The
m5stick-s3 build was verified with `FURBLE_VERSION=dev FURBLE_TEST=0`. It
passes. The remaining environments build in CI.

Sim and host verification (no hardware required):

- The protocol golden corpus covers the new `PRESET_PICKER` setting at wire
  id 30. `make generate` is deterministic (regenerating leaves the goldens
  unchanged) and `make test` passes, so the get, set, list, and response
  TLV encodings are pinned.
- `sim/scenarios/e2e/exposure-preset.txt` drives the real Features switch to
  enable the picker, asserts the `PRESET_PICKER` setting persisted, then
  opens the Bulb Duration page and steps the picker one 1/3-stop entry. The
  step snaps and saves the stored bulb duration, so the persisted
  `Settings::BULB` value (30 s default stepping up to 40 s) proves the picker
  selection survived. Teeth confirmed by mutation: dropping the stepped-value
  apply in `stepPreset` fails the scenario at the bulb-duration assertion.
- The sim assertion surface additions (`preset_picker` in the toggle and
  boolean-assert maps, the `bulb_ms` query, the `preset-step` action) are all
  inside `#if defined(FURBLE_SIM)`, so the production binary is unchanged.

The sim covers the UI, the setting persistence, and the setting TLV encoding.
The on-camera exposure change still cannot be verified without hardware.

Hardware verification is still pending, including X100VI bulb exposures at
stepped values. Also verify on hardware that the middle-key confirm firing
on LV_EVENT_RELEASED (the other menu back paths use LV_EVENT_CLICKED)
behaves correctly, in particular that no stray release event after a
long-press repeat exits the page.
