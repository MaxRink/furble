# PR12 - Extended inactivity, true display off, blind remote

## Goal

Cut display power during idle. Today the screen only dims to a per-board floor and
stays lit forever. Add longer inactivity choices, a real display off state, and a
blind remote mode where the screen is off but the shutter still fires.

## Implementation status

State: Implemented on `feat/12-display-off`.

The inactivity roller now offers 2, 5 and 10 minutes. `DISPLAY_OFF` uses the
standard Settings and NVS mechanics. The panel sleeps and wakes through M5GFX.
The APB lock is released only while the panel is off. It is reacquired before
wakeup. Blind remote passes shutter and focus input through on non-touch remote
pages. Other input wakes the display and is swallowed.

Deviations:

- The remote-active option is hidden on touch boards. Mode 2 behaves like off on
  those boards.
- `src/FurbleConsole.cpp` includes the new uint8 setting in console mechanics.
- PlatformIO could not complete in the original environment. After the rebase
  onto current master, `m5stick-s3` and `m5stick-s3-debug` build clean.
  Hardware verification remains pending on the M5StickS3.

Rebase notes:

- `DISPLAY_OFF` is assigned wire_id 24, continuing after `WATCHDOG` (23).
- `src/FurbleCompanion.cpp` settingType and settingValue treat `DISPLAY_OFF`
  as a uint8 setting, matching `INACTIVITY`.
- Console `appliesImmediately` stays false for `DISPLAY_OFF`: the UI caches
  the mode at startup and only its own roller updates the cache live.

## Scope

In scope:

- Extend the inactivity roller beyond 60 seconds.
- Add a screen off behaviour setting: dim only, off, off with remote active.
- Put the panel to sleep and the backlight off on timeout.
- Wake on any input. Swallow the waking press so it does not activate a widget.
- Blind remote: while the connected Remote page is active, shutter and focus
  buttons still work with the screen off.

Out of scope:

- Auto power off and low battery policy (PR13).
- Wake by IMU gesture (PR17).
- CPU frequency and light sleep (PR01, PR06, PR07).

## Files to change

| File | Anchor | Change |
|---|---|---|
| `include/FurbleSettings.h` | `:16-29` enum, `:101-148` `storage_type` | Add `DISPLAY_OFF` |
| `src/FurbleSettings.cpp` | `:11-24` table, `:186-227` default switch | Add row and default 0 |
| `include/FurbleUI.h` | `:30-34` inactivity declarations, `:227` `m_MinimumBrightness`, `:254` `m_InactivityTimeout` | Add display off state and helpers |
| `src/FurbleUI.cpp` | `:90-92` startup brightness and inactivity load | Load new setting |
| `src/FurbleUI.cpp` | `:95-109` per board `m_MinimumBrightness` (32, 48 on StickC and StickC Plus) | Unchanged, still used by dim mode |
| `src/FurbleUI.cpp` | `:112-117` 1000 ms inactivity timer | Unchanged period, new state machine |
| `src/FurbleUI.cpp` | `:313-369` `buttonPWRRead`, `buttonPEKRead`, `buttonARead`, `buttonBRead`, `buttonCRead`, `touchRead` | Wake and swallow the waking press |
| `src/FurbleUI.cpp` | `:1019-1043` `configureControl`, `:1045-1078` `configShutterControl` | Read `m_ControlMode` to decide blind pass through |
| `src/FurbleUI.cpp` | `:1851-1950` `addDisplayMenu`, roller at `:1917-1933` | Extend roller, add screen off roller |
| `src/FurbleUI.cpp` | `:2090-2092` `setInactivityTimeout` | Map roller index to seconds via table |
| `src/FurbleUI.cpp` | `:2094-2112` `processInactivity` | Dim or sleep depending on setting |

## New settings

| Enum | NVS key | Namespace | Type | Values | Default |
|---|---|---|---|---|---|
| `DISPLAY_OFF` | `display_off` (11 chars) | `FURBLE_STR` | `uint8_t` | 0 dim only, 1 off, 2 off with remote active | 0 |

`INACTIVITY` is unchanged. It stays `uint8_t` and still means multiples of 30
seconds (`src/FurbleUI.cpp:2090-2092`). Stored values 0, 1 and 2 keep their
current meaning. New roller entries map to larger multiples, so old NVS content
stays valid.

Roller index to stored value table:

| Index | Label | Stored value | Timeout |
|---|---|---|---|
| 0 | Never | 0 | disabled |
| 1 | 30 secs | 1 | 30 s |
| 2 | 60 secs | 2 | 60 s |
| 3 | 2 mins | 4 | 120 s |
| 4 | 5 mins | 10 | 300 s |
| 5 | 10 mins | 20 | 600 s |

The current code writes the roller index straight into NVS
(`src/FurbleUI.cpp:1929-1931`). That must become an index to value lookup, plus
the reverse lookup when the page is built. Unknown stored values select the
closest entry.

## Menu placement

Settings -> Display, under the existing Inactivity timeout roller
(`src/FurbleUI.cpp:1911-1933`). New roller labelled "Screen off" with options
`Dim\nOff\nOff, remote on`. No new menu page and no change to the Core grid map
at `src/FurbleUI.cpp:53-76`.

## Implementation notes

- `processInactivity` becomes a three state machine: active, dim, off. On
  timeout, `DISPLAY_OFF == 0` dims to `m_MinimumBrightness` as today. Values 1
  and 2 call `M5.Display.sleep()`.
- `M5GFX` already does the right thing. `LGFX_Device::sleep()` is
  `_panel->setBrightness(0); _panel->setSleep(true);` and `wakeup()` restores the
  saved brightness. No manual brightness call is needed on either side.
- Correction to the earlier assumption about AXP192 boards. M5GFX already cuts
  the backlight rail when brightness reaches 0: Core2 disables AXP192 DC3, Tough
  disables LDO3, StickC and StickC Plus disable LDO2. So `M5.Display.sleep()` is
  enough on those boards and no PMIC specific code is required. The remaining
  unknown is the StickS3 backlight path, which is not AXP192 driven. Measure it.
- Correction on touch. M5Unified does not expose a touch monitor or low power
  mode. `M5.Touch` has only `isEnabled()`. Leave the touch controller running so
  a tap wakes the screen on Core2 and Tough. Its idle draw is small compared to
  the backlight.
- Wake and swallow. Add `bool m_DisplayOff`. The indev read callbacks at
  `src/FurbleUI.cpp:313-369` check the flag first. If set, call
  `M5.Display.wakeup()`, clear the flag, call `lv_display_trigger_activity()`,
  and report `LV_INDEV_STATE_RELEASED` until the button reports released again.
  This prevents the waking press from clicking whatever widget has focus.
- `buttonPEKRead` (`src/FurbleUI.cpp:323-330`) reads a click count from
  `Platform::getPWRClickCount()`. Swallowing there means consuming the count so
  it is not replayed.
- Blind remote. With `DISPLAY_OFF == 2` and `m_ControlMode == ControlMode::SHUTTER`
  (`include/FurbleUI.h:24`, set in `src/FurbleUI.cpp:1027-1032`), the shutter and
  focus buttons are not swallowed and the display stays asleep. They go straight
  to the existing handlers (`src/FurbleUI.cpp:540-616`). Every other input wakes
  the screen and is swallowed. Shutter lock keeps working because it runs off the
  same event path (`src/FurbleUI.cpp:506-538`, `:590-616`).
- Blind remote does not change the touch boards. On Core2 and Tough there is no
  physical shutter button, so mode 2 behaves like mode 1 there. Hide or grey the
  third roller entry when `M5.Touch.isEnabled()`.
- The header icon timer at `src/FurbleUI.cpp:156-190` keeps running while the
  panel sleeps. Pause it when the display is off to avoid pointless LVGL redraws,
  and resume it on wake.
- Screen lock handling (`src/FurbleUI.cpp:2114-2121`) is untouched.

## Dependencies

None hard. PR13 and PR17 build on the display off state added here. If PR01 has
landed, the Settings -> Power submenu already exists, but this setting stays
under Display.

## Risks

- Users mistake an off screen for a dead device. Mitigate by keeping the default
  at dim only and by waking on any input.
- Blind remote can fire the shutter from a pocket press. It is opt in and only
  active on the Remote page.
- A panel that does not support `setSleep` would show a white or garbage screen.
  Verify on every board that gets hardware time.
- Swallowing input can drop a legitimate press if the state flag is wrong.
  Keep the flag write in one place.
- LVGL inactivity is measured per display with `lv_disp_get_inactive_time`
  (`src/FurbleUI.cpp:2098`). Waking without calling `lv_display_trigger_activity`
  would put the screen straight back to sleep.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device, M5StickS3 over USB (`pio run -e m5stick-s3 -t upload`,
`pio device monitor`):

1. Fresh NVS boot. Confirm dim only behaviour at 30 and 60 seconds is identical
   to master.
2. Set 2, 5 and 10 minutes. Confirm the timeout matches and survives a reboot.
3. Set Screen off. Confirm the panel goes dark, then that BtnA wakes it without
   activating the focused menu item.
4. Connect to a Fujifilm camera. Open Remote. Set Off, remote on. Confirm the
   screen sleeps and the shutter still fires. Confirm BtnPWR wakes the screen.
5. Confirm shutter lock still engages and releases with the screen off.

Battery drain runs, unplugged, on board instrumentation only, no external power
meter:

- Log battery percent and voltage every 30 s to the console for 30 to 60 minutes
  per state. On AXP192 boards also log `M5.Power.getBatteryCurrent()` with the
  same EWMA used by the debug path at `src/FurbleUI.cpp:160-167`.
- States: menu idle dim, menu idle off, connected idle dim, connected idle off.
- Compare against the same runs on master. USB power reflects charging, so runs
  happen on battery and the log is read afterwards.

One AXP device (StickC Plus or Core2) gets the same smoke test because the
backlight rail path differs.

Cameras: Fujifilm only. No BLE behaviour changes here, so other vendors are
unaffected by construction. State that in the PR body.

## References

- M5GFX `LGFX_Device::sleep`, `wakeup`, `powerSave`, `setBrightness`:
  https://raw.githubusercontent.com/m5stack/M5GFX/master/src/lgfx/v1/LGFXBase.hpp
- M5Unified `Power_Class` API used for battery logging:
  https://raw.githubusercontent.com/m5stack/M5Unified/master/src/utility/Power_Class.hpp
- ESP-IDF power management, for the CPU and light sleep interaction this PR must
  not disturb:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html
