# 67: Bulb exposure UX

## Motivation

Two user reports expose a confusing bulb workflow:

- When the automatic exposure completes, the shutter is released but the run
  page remains visually active. The countdown stays at zero and the action
  still says `Stop`.
- A bulb exposure only works when the camera is in its `B` drive mode. Users
  forget this camera-side prerequisite and start exposures that the camera does
  not hold open.

The fix keeps the page passive and direct. It does not add a modal dialog or
change the camera protocol.

## Design

The bulb page uses three states:

- `IDLE`: no exposure is active. Cancellation and page navigation return here.
- `RUNNING`: the shutter is held, the one-shot timer is armed, and the
  countdown refreshes every 333 ms.
- `DONE`: automatic release has completed. The page shows `Done` with the
  measured elapsed time, resets the countdown to the saved duration, and
  changes the action to `Restart`.

Automatic completion uses a dedicated `bulbComplete()` path. It marks the
state done before calling the shared shutter unlock, pauses both timers, and
cannot release again if a late callback or page transition arrives. The
`Restart` action calls the same `bulbStart()` path as the initial start and
keeps the saved duration. A mid-exposure `Stop`, back navigation, or disconnect
continues to use `bulbStop()` and releases immediately. In the done state,
navigation returns to `IDLE` after the existing timer cleanup and does not send
another shutter release.

The setup page has one persistent passive label:

```
Camera must be in B (bulb) mode
```

`UI::updateBulbModeHint()` is the named hook for replacing that static text
with a live warning later. The current implementation does not infer camera
mode from BLE.

## Implementation state

Implemented in this branch:

- `src/FurbleUI.cpp` owns the bulb transition helpers and the done/restart
  widgets.
- `include/FurbleUI.h` stores the bulb state, elapsed start tick, and label
  handles.
- The hint uses the existing 12 px LVGL font with compact spacing so the
  requested line fits the 135 x 240 Stick layout.

No vendor protocol or setting format changes are required. The change is
localized to the bulb UI and its plan index entry to reduce overlap with the
exposure preset work in PR 33.

## Verification

Build the requested target with the repository build variables:

```
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3
```

If the first run fails while installing TinyGPSPlus, retry the same command
once.

On the smallest 135 x 240 layout, confirm the hint is one line, remains on the
bulb setup page, and does not make the duration or `Start` controls unusable.
With a Fujifilm camera in `B` mode, verify a short exposure reaches `Done`,
shows a nonzero elapsed time, displays the saved duration for the next shot,
and changes the action to `Restart`. Press `Restart` and verify a new shutter
press starts without leaving the run page. Press `Stop` during an exposure and
verify the existing immediate cancel behavior. Navigate back, disconnect, and
leave the done page to verify that no path sends a second shutter release.

Pending hardware experiment: with an X100VI connected, switch the physical
drive dial into and out of `B` while recording all available BLE notifications
and readable camera state. Determine whether the dial state is exposed over
the protocol. If it is exposed, connect that result to
`updateBulbModeHint()` and show a live warning when the camera is not in `B`.
If it is not exposed, retain the passive reminder and record that limitation
for future camera support work.
