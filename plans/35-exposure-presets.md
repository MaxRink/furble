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
- Applies to the bulb duration picker (plans/04) and the intervalometer
  Shutter spinner. Count, Delay and Wait keep the digit spinner, they are
  not exposure values.
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
- When entering preset mode with a value not in the series, snap to the
  nearest series entry and show it.
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
