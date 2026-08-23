# PR05: diagnostics scaffold

## Goal

Expand the About page and add a Settings -> Diagnostics submenu holding device
info, battery detail and a power state page. Gives later power and BLE PRs a
place to put live data instead of adding one off debug builds.

## Implementation state

The diagnostics UI is present on the fork. This follow-up adds simulator-only
queries for the rendered About, Device info, Battery, Power state, BLE and NMEA
labels, plus an end-to-end scenario that visits every diagnostics page. The
queries validate value shape and page selection without depending on timestamps
or host-specific formatting.

This is a fork testability addition. It does not change firmware behavior or
add settings, storage, or hardware requirements. About, Device info, Power
state and NMEA are intentionally allowed to report `ui.overflow` because their
content is scrollable on the modeled StickS3 panel. Battery and BLE fit without
overflow in the scenario.

## Scope

In scope:

- About gains build info, uptime, heap and reset reason.
- New Settings -> Diagnostics page.
- Diagnostics -> Device info: chip, revision, cores, flash size, free and minimum
  free heap, reset reason, uptime.
- Diagnostics -> Power state: `esp_pm` configuration read back, light sleep
  enabled or not, and a button that dumps the pm locks to the console.
- Diagnostics -> Battery: the detailed view from PR02, moved or linked here.

Out of scope:

- BLE, GPS and IMU diagnostic pages. Those land with PR10, PR14 and PR16 and
  hang off the page this PR creates.
- Any behaviour change. This PR only reads state.

## Files to change

- `src/FurbleUI.cpp:2042-2060`, `addAboutMenu`. Currently two labels: version
  from `FURBLE_VERSION` at line 2052 and the device ID from
  `Device::getStringID()` at line 2057. Add the new rows here.
- `src/FurbleUI.cpp:2062-2082`, `addSettingsMenu`. Add the `addDiagnosticsMenu`
  call next to the existing seven `add*Menu` calls at lines 2073-2079.
- `src/FurbleUI.cpp:53-76`, `UI::m_Menu`. Add entries for Diagnostics and its
  sub pages with their `{col,row}` values.
- `include/FurbleUI.h:176-182`, add `m_DiagnosticsStr = "Diagnostics"` and the
  sub page strings.
- `include/FurbleUI.h:337-343`, declare `addDiagnosticsMenu` next to the other
  page builders.
- `include/FurbleUI.h:217-220`, `m_GridLayoutColDsc` and `m_GridLayoutRowDsc`.
  See Menu placement.
- `include/FurblePlatform.h:16-41`, add read only accessors for uptime and the
  current `esp_pm` configuration so the UI does not include `esp_pm.h` directly.
  `Platform::tick()` at `src/FurblePlatform.cpp:51-53` already returns
  milliseconds since boot.
- `src/FurblePlatform.cpp:65-72`, `setSleep`. Add a getter that returns the last
  applied configuration, or call `esp_pm_get_configuration()` on demand.

No settings changes. No new NVS keys.

## New settings

None.

## Menu placement

```
Settings
+- Diagnostics
   +- Device info
   +- Battery
   +- Power state
+- About        (expanded, stays where it is)
```

The Core and Core2 settings page is a grid, `include/FurbleUI.h:217-220`, four
columns by two rows. From `src/FurbleUI.cpp:53-76` the eight slots are: Display
{0,0}, Features {1,0}, GPS {2,0}, Timer {3,0}, Theme {0,1}, TX Power {1,1},
About {2,1}, and {3,1} which PR01 takes for Power. The grid is full. Two options:

1. Grow `m_GridLayoutRowDsc` to three rows and put Diagnostics at {0,2}. Note
   that the same two descriptors are also used by the main menu page at
   `src/FurbleUI.cpp:862`, which only fills row 0 plus Off at {3,1}. A third row
   makes every main menu cell shorter. Check both pages on a Core2 before
   choosing this.
2. Give the settings page its own descriptors, separate from the main menu, and
   grow only that one. More code, no side effect on the main menu.

Option 2 is the safer change and is preferred. Option 1 is acceptable if the
main menu still looks right.

On the Stick boards the settings page is a scrolling list and needs no
placement work.

## Implementation notes

### About page rows

Keep the existing two rows and add:

- Build: `FURBLE_VERSION` is already shown. Add the build date and time from
  `__DATE__` and `__TIME__`, and the ESP-IDF version from `IDF_VER`.
- Uptime: `Platform::tick() / 1000`, formatted with `SpinValue::toHMS`
  (`include/FurbleSpinValue.h:46`), the same helper the intervalometer countdown
  uses at `src/FurbleUI.cpp:1840`.
- Heap: `esp_get_free_heap_size()` and `esp_get_minimum_free_heap_size()`.
- Reset reason: `esp_reset_reason()` mapped to a short string. The enum values
  that matter here are `ESP_RST_POWERON`, `ESP_RST_SW`, `ESP_RST_PANIC`,
  `ESP_RST_TASK_WDT`, `ESP_RST_BROWNOUT` and `ESP_RST_DEEPSLEEP`. Brownout and
  task watchdog are the two that indicate a real problem, and PR19 will care
  about deep sleep.

The About page is a flex column inside a single container
(`src/FurbleUI.cpp:2044-2047`). Adding rows is cheap. On the StickC the screen is
80x160, so the page must scroll. Confirm scrolling works there rather than
truncating.

### Live values

Uptime and heap change while the page is open. Add one `lv_timer` at 1 s that
refreshes the labels, and pause it when the page is left. Copy the GPS Data
pattern exactly: the timer is created paused at `src/FurbleUI.cpp:1555-1590`,
resumed on button click at `src/FurbleUI.cpp:1593-1601`, and paused again by
`gpsDataStop` at `src/FurbleUI.cpp:1606-1611`. That pattern already solves the
leave the page problem, so reuse it rather than inventing another.

Do not add another always running timer. The 250 ms icon timer at
`src/FurbleUI.cpp:156` is enough background work already.

### Power state page

- Read back the live configuration with `esp_pm_get_configuration()`. Show
  `max_freq_mhz`, `min_freq_mhz` and `light_sleep_enable`. This makes the PR01
  setting verifiable on the device, and it will make PR06 and PR07 verifiable
  too.
- Add a button that calls `esp_pm_dump_locks(stdout)`. That function writes to a
  `FILE *`, so the output goes to the console, not the screen. Label the button
  so it is clear the output appears on the serial console. This is the main
  reason PR00 lands first.
- `CONFIG_PM_ENABLE=y` is already set (`sdkconfig.m5stick-s3:1359`,
  `sdkconfig.m5stick-c-plus:1179`), so the lock API is available. Per lock hold
  time statistics need `CONFIG_PM_PROFILING`, which is not enabled and should not
  be enabled in the shipping build. State that the dump lists locks without
  timing.
- Show whether tickless idle is compiled in.
  `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` on both families
  (`sdkconfig.m5stick-s3:1680`, `sdkconfig.m5stick-c-plus:1473`).

Avoid private headers. `esp_clk_cpu_freq()` lives under `esp_private`. Use
`esp_pm_get_configuration()` for the configured ceiling and floor, and say in the
UI that these are the configured values, not an instantaneous measurement.

### Battery detail

If PR02 has landed, link its Battery page from Diagnostics instead of building a
second one. Use `lv_menu_set_load_page_event` with the existing page object, the
same shared page trick used for the intervalometer at
`src/FurbleUI.cpp:1404-1405`. If PR02 has not landed, ship Diagnostics without
the battery entry and let PR02 add it.

## Dependencies

- PR00 for the console, since the pm lock dump only appears there.
- PR01 creates Settings -> Power and consumes the last free Core grid slot. This
  PR must handle the grid growth, so it should land after PR01.
- PR02 provides the battery page this one links to. Optional.
- Blocks nothing hard, but PR07, PR10, PR14 and PR16 all want to add pages under
  Diagnostics.

## Risks

- Growing the Core grid changes the look of the main menu if the shared
  descriptors are used. Option 2 above avoids it.
- More labels on About means more heap on small screens. Check free heap before
  and after on the StickC, which is the tightest target.
- `esp_pm_dump_locks` output can be long and is written from the calling context.
  Call it from a button, never from a timer.

## Verification

Build matrix:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device, M5StickS3 over USB:

1. Open About. Confirm version, ID, build date, uptime, heap and reset reason
   are all present and readable.
2. Confirm uptime increments while the page is open, and that the refresh timer
   stops when the page is left. Verify with a log line or by watching CPU load.
3. Trigger a software reset from the Theme page restart button
   (`src/FurbleUI.cpp:1990`). Confirm About then reports a software reset.
4. Power cycle. Confirm About reports a power on reset.
5. Open Diagnostics -> Power state. Confirm the reported max frequency matches
   what PR01's setting was set to, and that light sleep matches the expected
   state. With GPS enabled on the S3 light sleep is disabled
   (`src/FurbleGPS.cpp:120-123`), so toggling GPS must flip the reported value.
   This is a good end to end check of the whole page.
6. Press the lock dump button. Confirm output appears in `pio device monitor`.
7. Note the free heap before and after opening every diagnostics page twice.
   Growth across repeated visits means a leak.

On one AXP192 device, M5StickC Plus, repeat steps 1, 5 and 7. On a Core or Core2,
check the grid layout of both the settings page and the main menu.

Battery drain: the 1 s refresh timer only runs while a diagnostics page is open,
so steady state drain is unchanged. Confirm with a 30 minute unplugged connected
idle run against master using the PR02 harness, logging battery voltage every
30 s. Expect no measurable difference.

Camera coverage: Fujifilm only, and only to confirm that opening diagnostics
during a connection does not disturb it. No BLE code changes. State this in the
PR body.

## References

- ESP-IDF, Miscellaneous System APIs, `esp_reset_reason`, `esp_reset_reason_t`,
  `esp_get_free_heap_size`, `esp_get_minimum_free_heap_size`, `esp_restart`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/misc_system_api.html
- ESP-IDF, Power Management, `esp_pm_get_configuration`, `esp_pm_dump_locks`,
  `ESP_PM_NO_LIGHT_SLEEP`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/power_management.html
- ESP-IDF, Heap Memory Allocation, `heap_caps_get_free_size` and the capability
  based API:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/mem_alloc.html
- ESP-IDF, `esp_timer_get_time`, the source of `Platform::tick`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/esp_timer.html
- ESP-IDF, Sleep Modes, ESP32-S3:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/sleep_modes.html
- LVGL 9.4, Menu widget:
  https://lvgl.io/docs/open/9.4/details/widgets/menu
- M5Unified Power_Class, for the battery rows:
  https://docs.m5stack.com/en/arduino/m5unified/power_class
- PlatformIO, device monitor:
  https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html
