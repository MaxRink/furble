# PR04: bulb timer

## Goal

Add a Bulb page to the Connected menu that holds the shutter open for a set
duration, shows a countdown, and releases automatically. Reuses the existing
shutter lock and spinner code.

## Scope

In scope:

- New Bulb page on the Connected menu with a duration spinner and a start button.
- Countdown display while the exposure runs, and a stop button.
- Automatic shutter release when the duration expires.
- New `BULB` setting storing the duration.

Out of scope:

- Multiple bracketed exposures. The intervalometer already covers repetition.
- Any change to camera vendor code.

## Files to change

- `src/FurbleUI.cpp:506-538`, `shutterLock` and `shutterUnlock`. Bulb drives the
  shutter through these so the `m_ShutterLock` flag and the lock icon stay
  consistent with the Remote page.
- `src/FurbleUI.cpp:590-616`, `handleShutterLock`. Read only. Confirm the bulb
  path cannot fight the long press toggle.
- `src/FurbleUI.cpp:1169-1227`, `UI::intervalometer`. The state machine that
  sends `CMD_SHUTTER_PRESS` at line 1200 and `CMD_SHUTTER_RELEASE` at line 1207.
  Bulb is a simpler version of the same idea. Read it before writing the new
  timer.
- `src/FurbleUI.cpp:1835-1844`, `m_IntervalPageRefresh`. The 333 ms countdown
  timer that formats remaining time with `SpinValue::toHMS`. Copy this for the
  bulb countdown.
- `src/FurbleUI.cpp:1265-1279`, `addConnectedMenu`. Add the Bulb item. Lines
  1268-1275 hold the Core grid descriptor.
- `src/FurbleUI.cpp:53-76`, `UI::m_Menu`. Add the Bulb entry with its `{col,row}`.
- `src/FurbleUI.cpp:1624-1649` and `:1651-1770`, `addSpinItem` and
  `addSpinnerPage`. Reused as is for the duration control.
- `src/FurbleUIIntervalometer.cpp:20-52`, `Spinner::update`. Line 51 calls
  `m_Intervalometer->save()`. This needs the small refactor described below.
- `include/FurbleUI.h:77-126`, the `Intervalometer` class and its nested
  `Spinner`. Add the owner interface and the bulb members.
- `include/FurbleUI.h:169-173`, the connected string block. Add
  `m_RemoteBulb = "Bulb"`.
- `include/FurbleSettings.h:16-29` and `:145-148`, enum and `storage_type`.
- `src/FurbleSettings.cpp:11-24` and `:186-227`, table row and default.
- `src/FurbleSettings.cpp:67-88` and `:148-154`, the `interval_t` load and save
  specialisations. The `BULB` blob needs the same treatment.

## New settings

| Item | Value |
|---|---|
| Enum | `Settings::BULB` |
| Display name | `Bulb` |
| NVS key | `bulb` (4 chars) |
| NVS namespace | `FURBLE_STR` |
| Storage type | `SpinValue::nvs_t`, defined at `include/FurbleSpinValue.h:29-32` |
| Default | `{30, SpinValue::UNIT_SEC}` |

`SpinValue::nvs_t` is a packed struct, so it is stored as a blob exactly like
`interval_t`. Add `load<SpinValue::nvs_t>` and `save<SpinValue::nvs_t>`
specialisations next to the `interval_t` ones at `src/FurbleSettings.cpp:67-88`
and `:148-154`, including the length check that falls back to the default when
the stored blob is the wrong size.

The default does not change any existing behaviour. Nothing happens until the
user opens the page and presses start.

## Menu placement

```
Connected
+- Remote
+- Bulb        (new)
+- Interval
+- Settings    (PR03)
+- Disconnect
```

On Core and Core2 the Connected page uses the grid set at
`src/FurbleUI.cpp:1269-1274`, three columns by one row, with items at
`{0,0} {1,0} {2,0}` from `src/FurbleUI.cpp:72-74`. Adding Bulb needs a second
row. If PR03 has already landed it will have moved to a two by two layout, and
this PR extends that to three by two. Coordinate: both PRs edit the same
descriptor and the same `{col,row}` values. Whichever lands second rebases onto
the other's layout.

## Implementation notes

### Spinner ownership

`Spinner::update()` ends with `m_Intervalometer->save()`
(`src/FurbleUIIntervalometer.cpp:51`), and `Intervalometer::save()` writes the
whole `interval_t` (`src/FurbleUIIntervalometer.cpp:14-18`). A bulb spinner must
not write the intervalometer setting. The smallest correct change is to give
`Spinner` an owner that knows how to save itself:

- Add a tiny interface with a pure virtual `save()`.
- `Intervalometer` implements it with the existing body.
- A new small `Bulb` holder implements it by writing `Settings::BULB`.
- `Spinner`'s member becomes a pointer to that interface. The constructor
  signature at `include/FurbleUI.h:81-82` changes type but not shape.

Do not copy `addSpinnerPage`. It is 120 lines of per board layout work at
`src/FurbleUI.cpp:1651-1770` and duplicating it will rot.

### Exposure timer

One `lv_timer` in one shot form. On start:

1. `shutterLock(Control::getInstance())`, which sends `CMD_SHUTTER_PRESS` and
   sets `m_ShutterLock` (`src/FurbleUI.cpp:506-521`).
2. Record the end time as `tick() + duration_ms` using
   `SpinValue::toMilliseconds()` and `UI::tick()` (`src/FurbleUI.cpp:380`).
3. Start a 333 ms refresh timer that renders the remaining time with
   `SpinValue::toHMS`, matching `src/FurbleUI.cpp:1840-1841`.
4. When the remaining time reaches zero, `shutterUnlock(...)`, which sends
   `CMD_SHUTTER_RELEASE`, and pause both timers.

Using `shutterLock` rather than raw `sendCommand` keeps one source of truth for
the lock state. Note the side effect: on touch boards `shutterLock` disables
`m_OK` and `m_Right` (`src/FurbleUI.cpp:512-515`). Those are the Remote page
buttons created in `addConnectedMenu`, so disabling them during a bulb exposure
is correct. Verify they are re-enabled after release.

### Stop and safety

- A stop button must release the shutter immediately. Copy the intervalometer
  stop button at `src/FurbleUI.cpp:1815-1833`, which sends
  `CMD_SHUTTER_RELEASE` and then clicks the back button.
- Leaving the page while running must not strand a held shutter. The Connected
  page click handler already calls `shutterUnlock` at `src/FurbleUI.cpp:995`.
  Confirm that path covers the bulb page too, and add an explicit release on page
  exit if it does not.
- A disconnect during an exposure must cancel the timer. `doDisconnect` is at
  `src/FurbleUI.cpp:1251-1263`. Add the timer pause there, next to the existing
  `lv_timer_pause(m_ConnectTimer)`.
- Long exposures interact with the inactivity timer at
  `src/FurbleUI.cpp:2094-2112`, which only dims the display today. Dimming during
  a bulb exposure is acceptable. Note it and leave it to PR12.

### Relationship to the intervalometer

The intervalometer can already hold the shutter for its Shutter duration
(`src/FurbleUI.cpp:1197-1203`), so a bulb exposure is a single shot with count 1.
The value of a separate page is a direct control that does not disturb the saved
interval settings, and a countdown that reads as an exposure rather than a
sequence. Say this in the PR body so the overlap is not mistaken for
duplication.

### Duration range

`SpinValue` holds a `uint16_t` value plus a unit, so the maximum is 999 minutes
with the three digit rollers at `src/FurbleUI.cpp:1706-1720`. That is far beyond
any sensor's useful exposure. No range work needed. Do not offer `UNIT_INF` for
bulb; pass `infinite = false` to the `Spinner` constructor, which hides the
infinite row at `src/FurbleUI.cpp:1674-1676`.

## Dependencies

- Independent of PR01, PR02 and PR05.
- Overlaps PR03 in the Connected page grid only. See Menu placement.

## Risks

- A held shutter that is never released drains the camera battery and can lock up
  some bodies. Every exit path must release. This is the main risk and the main
  focus of verification.
- Reusing `Spinner` changes a constructor signature used by four existing members
  (`src/FurbleUIIntervalometer.cpp:9-12`). A mechanical change, but it touches the
  intervalometer, so re-verify the intervalometer after the refactor.
- Long exposures with light sleep enabled later, PR07, must not sleep through the
  release. Note the interaction for PR07 rather than solving it here.

## Verification

Build matrix:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device, M5StickS3 over USB, connected to the Fujifilm camera in bulb or manual
mode:

1. Fresh NVS boot. Confirm Bulb shows 30 secs.
2. Set 5 secs. Start. Confirm the camera opens the shutter, the countdown runs,
   and the shutter closes at zero. Check the resulting frame on the camera.
3. Start a 60 s exposure and press stop after 10 s. Confirm the shutter closes
   immediately.
4. Start an exposure and press back. Confirm the shutter closes.
5. Start an exposure and power off the camera. Confirm the UI recovers, the
   timer stops, and no crash appears in the console log.
6. Start an exposure and let the display dim. Confirm the exposure still ends
   correctly.
7. Run the intervalometer afterwards and confirm its saved settings are
   unchanged. This proves the spinner ownership refactor did not cross wires.
8. Power cycle. Confirm the bulb duration persisted and the interval settings
   persisted separately.

On Core2, verify the Connected page layout with the extra item and confirm touch
targets do not overlap.

Battery drain: not a target of this PR, but a long bulb exposure keeps the link
active. Run one 10 minute exposure unplugged and log battery voltage every 30 s
to confirm nothing unexpected, using the PR02 harness if it has landed.

Camera coverage: Fujifilm only. The bulb path sends the same
`CMD_SHUTTER_PRESS` and `CMD_SHUTTER_RELEASE` commands the Remote page already
sends, so vendor code is untouched. FauxNY can exercise the state machine
without a camera. State clearly in the PR body that only Fujifilm was tested on
hardware, and that bulb behaviour depends on the camera being in a mode that
honours a held shutter.

## References

- LVGL 9.4, Menu widget, `lv_menu_set_load_page_event`:
  https://lvgl.io/docs/open/9.4/details/widgets/menu
- LVGL 9.4, Roller widget, used by the spinner:
  https://lvgl.io/docs/open/9.4/details/widgets/roller
- ESP-IDF, `esp_timer_get_time`, the source of `UI::tick`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/esp_timer.html
- M5Stack StickS3 product page:
  https://docs.m5stack.com/en/core/StickS3
- PlatformIO, device monitor:
  https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html
