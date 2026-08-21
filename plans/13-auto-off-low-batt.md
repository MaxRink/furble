# PR13 - Auto power off and low battery policy

## Implementation status

State: Implemented on `feat/13-auto-off-low-batt`.

Rebase notes:

- `AUTO_OFF` is assigned wire_id 37 and `LOW_BATT` wire_id 38, continuing
  after `DISPLAY_MODE` (36) from PR 31.
- `src/FurbleCompanion.cpp` settingType and settingValue cover both as
  SETTING_U8.
- The branch's console coverage (uint8 in all four switches, both apply
  immediately) merged onto master's GPS_POWER/GPS_DUTY lists as unions.

AUTO_OFF and LOW_BATT use the planned settings and NVS paths. The Power page
has both rollers on boards with a reliable software power-off path. The M5Stack
Core hides both policy rollers because its IP5306 cannot provide true software
power off. Manual Off and both automatic paths share `UI::doPowerOff()`.

Evaluation model:

- The one-second housekeeping timer calls `processAutoOff` and
  `processLowBattery` directly, independent of the `processInactivity`
  early-return paths. INACTIVITY defaults to never, so the policies must not
  hang off the inactivity decision.
- Both settings are cached in UI members. The rollers and the console (via
  `Request::POWER_RELOAD`) refresh the cache, and any policy change resets the
  warn latch and all hysteresis counters. The `serviceRequests` switch handles
  `POWER_RELOAD` as its own case. The `AUDIT` case above it needs an explicit
  `break;` so an audit request does not fall through and trigger an unintended
  policy reload.
- Auto off compares the LVGL idle clock against the setting while
  `Control::STATE_IDLE` holds and no discovery scan is active. `STATE_IDLE`
  also covers an active scan, so the scan check keeps auto off from cutting a
  scan short.
- Low battery hysteresis counts consecutive qualifying battery samples, six
  samples at the 5 s refresh is 30 s. Any non-qualifying or dropped sample
  resets the count. The comparison uses the smoothed `displayLevel`. A raw
  level of zero is only trusted when a plausible low pack voltage confirms
  it: no voltage capability, zero millivolts (a total M5PM1 failure returns
  no reading, not 0 mV), or a voltage above 3300 mV all mark the sample as a
  failed-read clamp and drop it.
- Power off (policy 2) is gated on `caps.charging`. Boards without a charging
  measurement (StickC Plus2, fallback boards) cannot distinguish USB power
  from discharge, so they warn but never power off.

Warning surface:

- The warning is an LVGL message box with an OK button. A passive header-only
  treatment was rejected because the battery style setting can hide the
  header label, and a dismissible box is the cheapest honest surface.
- The box wakes the panel through the display state machine (`wakeDisplay`
  plus the DIM brightness restore), never through raw M5GFX calls.
  `processInactivity` holds the panel awake only while a power-off countdown
  is pending. The warn-only box rides the normal dim/sleep path, otherwise a
  battery stuck at 10 percent would pin the backlight until flat and invert
  the feature. The LVGL idle clock is not touched, so a warning does not
  postpone auto off and does not stall the power-off countdown.
- Dismissing closes the box and cancels a pending power off, the press proves
  a user is present. The policy re-arms after another qualifying 30 seconds.
  The box captures the focused object before focusing its OK button and
  restores it on close, the flat input group has no other modal restore.
- On recovery (charging seen, or the level rising back over a threshold) the
  box downgrades from the countdown text to the warn text, or closes.

Power off ordering in `UI::doPowerOff`:

1. Disable the S3 watchdog, the BLE teardown can outlast a feed period.
2. Quiesce the intervalometer, releasing a mid-exposure shutter with a real
   `CMD_SHUTTER_RELEASE`.
3. Release the shutter lock, disconnect.
4. `Platform::powerOff()`, which retries the M5PM1 shutdown through the
   standard retry helper and returns false if the PMIC refused. On failure
   the UI logs a warning, re-arms the watchdog from the setting, and clears
   the power-off latch so the device keeps working.

`prepareRestart` on feat/68 wants the same quiesce ordering. A future shared
helper should absorb both, marked with a TODO in `doPowerOff`.

Verification completed:

- clang-format 21 passes for all changed C++ and header files.
- `git diff --check` passes.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` and
  `-e m5stick-s3-debug` pass.
- Hardware testing is pending. No boards have been tested. The hardware run
  must also confirm:
  - dismissing the warning returns focus to the page the user was on, not an
    arbitrary object in the flat group.
  - the warn-only box lets the screen dim and sleep normally, only the
    power-off countdown holds the panel awake.

Deviations:

- This branch already has PR02 battery instrumentation with a 5-second cached
  sample. The low battery policy uses `m_Status.battery` instead of adding a
  second I2C reader.
- Fork master does carry the PR12/#26 display state machine. An earlier
  revision claimed otherwise and bypassed it with raw M5GFX calls, which broke
  the SLPIN/SLPOUT dwell and desynced the APB lock. The warning now goes
  through `wakeDisplay()`.
- `lv_display_trigger_activity` moved out of `wakeDisplay` to the input path
  that saw the real press, so programmatic wakes are not counted as user
  activity.

## Goal

Stop the device draining a flat battery when it is left on by accident. Add an
auto power off after N minutes of no input while disconnected, and a low battery
policy of none, warn, or graceful power off.

## Scope

In scope:

- Auto power off timer, gated on idle input and no camera connection.
- Low battery detection with hysteresis and a charging guard.
- Warning dialog and graceful power off path.
- Reuse of the existing `Platform::powerOff` path.

Out of scope:

- Deep sleep between intervalometer shots (PR19).
- Timed wake. The power off path here is final and there is no self wake.
- Battery percent display and runtime estimate (PR02).

## Files to change

| File | Anchor | Change |
|---|---|---|
| `include/FurbleSettings.h` | `:16-29` enum, `:101-148` `storage_type` | Add `AUTO_OFF`, `LOW_BATT` |
| `src/FurbleSettings.cpp` | `:11-24` table, `:186-227` default switch | Add rows and defaults 0 |
| `include/FurbleUI.h` | `:30-34`, `:254` `m_InactivityTimeout` | Add idle and battery check helpers |
| `src/FurbleUI.cpp` | `:112-117` 1000 ms inactivity timer | Also drive the auto off and battery checks |
| `src/FurbleUI.cpp` | `:156-190` icon timer, battery level read at `:170` | Share the sampled level, do not add a second I2C reader |
| `src/FurbleUI.cpp` | `:875-885` Off menu entry calling `Platform::powerOff()` | Reuse, factor the shutdown into one helper |
| `src/FurbleUI.cpp` | `:2062-2082` `addSettingsMenu` | Register the new rollers under Power |
| `src/FurblePlatform.cpp` | `:74-80` `powerOff` | Unchanged, called from the new paths |
| `include/FurbleControl.h` | `:24-37` `state_t`, `:124` `getState` | Read connection state |

## New settings

| Enum | NVS key | Namespace | Type | Values | Default |
|---|---|---|---|---|---|
| `AUTO_OFF` | `auto_off` (8 chars) | `FURBLE_STR` | `uint8_t` | minutes, 0 = never | 0 |
| `LOW_BATT` | `low_batt` (8 chars) | `FURBLE_STR` | `uint8_t` | 0 none, 1 warn, 2 warn then power off | 0 |

Both defaults reproduce current behaviour exactly. Nothing happens unless the
user opts in.

Auto off roller index to stored minutes:

| Index | Label | Stored value |
|---|---|---|
| 0 | Never | 0 |
| 1 | 5 mins | 5 |
| 2 | 10 mins | 10 |
| 3 | 30 mins | 30 |
| 4 | 60 mins | 60 |

## Menu placement

Settings -> Power, created by PR01. Two entries: "Auto off" roller and "Low
battery" roller. If PR13 lands before PR01, it creates the Power page itself
using the same `addMenu` pattern and adds the `{col,row}` entry to `UI::m_Menu`
at `src/FurbleUI.cpp:53-76`. Only one of the two PRs creates the page.

## Implementation notes

- Reuse the existing 1000 ms LVGL timer at `src/FurbleUI.cpp:112-117`. Adding a
  second timer for this is not worth the RAM.
- Idle is `lv_disp_get_inactive_time(m_Display)`, the same source already used at
  `src/FurbleUI.cpp:2098`. Auto off compares it against `AUTO_OFF * 60000`.
- Disconnected is `Control::getInstance().getState() == Control::STATE_IDLE`
  (`include/FurbleControl.h:24-37`, `:124`).
- Important interaction. With Infinite-ReConnect enabled, the control task keeps
  returning `STATE_CONNECT` from the reconnect loop
  (`src/FurbleControl.cpp:124-131`), so the state never returns to `STATE_IDLE`
  and auto off never fires. That is the correct behaviour, since the user asked
  for endless reconnects. Say so in the settings help text and in the PR body.
- Battery sampling. The header icon timer already reads
  `M5.Power.getBatteryLevel()` every 250 ms at `src/FurbleUI.cpp:170`. Store the
  last value in `m_Status` and have the low battery check read that, so there is
  no extra I2C traffic and no extra PMIC wake on the S3.
- Hysteresis. Act only after the level stays below the threshold for 30
  consecutive seconds. Battery level readings jump under BLE TX bursts.
- Charging guard. Skip the whole policy when
  `M5.Power.isCharging() == Power_Class::is_charging`.
- Thresholds are fixed constants, not settings: warn at 10 percent, power off at
  5 percent. Two rollers are already enough surface area.
- Warn shows an LVGL message box, restores brightness, and wakes the display if
  PR12 put it to sleep. It shows once per discharge cycle, tracked with a latch
  that clears on charge.
- Graceful power off: warn, wait 30 seconds, release the shutter lock if held
  (`src/FurbleUI.cpp:523-538`), disconnect cleanly, then call
  `Platform::getInstance().powerOff()`.
- Power off behaviour differs per board and must be documented in the PR body:
  - StickS3: `M5PM1::shutdown()` (`src/FurblePlatform.cpp:75-77`). True off.
  - StickC Plus2: `M5.Power.powerOff()` drops the power hold pin, which is
    GPIO4 in the M5Unified pin table. Full off, no self wake.
  - StickC and StickC Plus, Core2, Tough: AXP192 `powerOff()`.
  - M5Stack Core: IP5306 has no true off. M5Unified falls back to
    `esp_deep_sleep_start()` at the end of `Power_Class::_powerOff`. The device
    looks off but the boost converter may keep running. Note this limitation
    rather than trying to work around it.
- The Core specific `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)`
  call at `src/FurbleUI.cpp:880-882` must also run on the new path. Factor the
  whole shutdown sequence into a single `UI::doPowerOff()` used by the Off menu
  button and by both new triggers.

## Dependencies

- PR01 for the Settings -> Power submenu. Soft dependency, described above.
- PR12 for the display state, so the warning can wake a sleeping screen. Soft
  dependency, guard the call.
- PR02 is not required but its battery instrumentation makes verification
  easier.

## Risks

- Unexpected shutdown during a long intervalometer run. Auto off only fires when
  disconnected, and the intervalometer requires a connection, so the run is
  protected by construction. Confirm this on device anyway.
- IP5306 boards cannot really power off. Documented, not fixed.
- Battery level on the S3 comes from the M5PM1 and can read low right after an
  I2C wake. The 30 second hysteresis absorbs that.
- A false low battery power off is worse than a flat battery. Default is none.
- The device cannot wake itself after power off. Users must press the power
  button. State this in the settings help text.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device, M5StickS3 over USB:

1. Fresh NVS boot. Confirm no auto off and no battery dialog, same as master.
2. Set auto off to 5 minutes. Leave the device disconnected and untouched.
   Confirm it powers off at 5 minutes and that any input restarts the countdown.
3. Connect to a Fujifilm camera and idle past the timeout. Confirm it does not
   power off.
4. Enable Infinite-ReConnect, disconnect the camera, idle past the timeout.
   Confirm it does not power off, and that the log shows `STATE_CONNECT`.
5. Low battery warn. Run the battery down or temporarily lower the constants in
   a debug build. Confirm one dialog, and confirm nothing is shown while
   charging.
6. Low battery off. Confirm warn, 30 second delay, clean disconnect, then off.
7. Repeat 2, 5 and 6 on one AXP192 board because the power off path differs.

Battery drain runs, unplugged, on board instrumentation only:

- Log level and voltage every 30 s. Confirm the auto off point matches the log
  timestamps.
- No power measurement is needed for this PR. It changes when the device stops,
  not how much it draws while running.

Cameras: Fujifilm only, used for the connected and disconnect cases. No vendor
specific code is touched. State that in the PR body.

## References

- M5Unified `Power_Class`, `powerOff`, `getBatteryLevel`, `isCharging`,
  `getBatteryVoltage`, `getBatteryCurrent`:
  https://raw.githubusercontent.com/m5stack/M5Unified/master/src/utility/Power_Class.hpp
- M5Unified `Power_Class::_powerOff`, showing the power hold pin pulse and the
  `esp_deep_sleep_start()` fallback:
  https://raw.githubusercontent.com/m5stack/M5Unified/master/src/utility/Power_Class.cpp
- M5Unified pin tables, StickC Plus2 power hold on GPIO4:
  https://raw.githubusercontent.com/m5stack/M5Unified/master/src/M5Unified.cpp
- M5PM1 driver, `shutdown()` and battery read:
  https://raw.githubusercontent.com/m5stack/M5PM1/main/src/M5PM1.h

## Hardware verification, pass 3, 2026-08-18

Verdict: PARTIAL. Tested on the combined image (version `hwv3`, app
`v3.9.1-159-g138dd80`) on the M5StickS3 over USB.

Console evidence:

- `settings set auto_off 5` then `settings get auto_off` returns `value: 5`,
  name `Auto off`, `type: uint8`, `applies: immediately`. 5 is the shortest non
  zero option. Wire id 37.
- `settings set low_batt 1` then `settings get low_batt` returns `value: 1`,
  name `Low battery`, `type: uint8`, `applies: immediately`. 1 is warn. Wire id
  38.
- Both survive a reboot (`auto_off` reads 5 and `low_batt` reads 1 after a power
  cycle).
- `applies: immediately` confirms both policies are read live by the running 1 s
  timer, no reboot needed. The `status` command is the power and battery
  readout: it returns live `battery`, `voltage` and `current` (for example
  `battery: 100 voltage: 4110 current: 0`), which is the same sampled level the
  low battery check consumes. During the connected and idle test window the
  device did not spuriously power off.

Not reachable over console, on the user checklist:

- The real auto power off fire needs the device disconnected and untouched for
  the full 5 minutes, which powers the device off and ends console access. The
  code logs `ESP_LOGI("ui", "Auto power off after %u minutes idle.")` at fire
  time. Run this on the bench and confirm the log line and the power off.
- The low battery warning box needs a genuine low battery. It cannot be forced
  from the console. Drain the battery below 10 percent and confirm one warning
  box appears, that nothing shows while charging, and, for the modal focus fix,
  that dismissing the box restores focus to the page underneath rather than
  leaving the UI unfocused.

Combined image caveat: this build tripped a one shot boot task watchdog and a
disconnect during connect hang. See the cross cutting note in plans/25. Neither
is attributed to this PR's policy code.
