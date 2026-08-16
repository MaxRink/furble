# PR01: CPU frequency setting

## Goal

Resolve the mismatch between the 160 MHz maximum passed to `esp_pm_configure()`
and the 80 MHz default CPU frequency in the sdkconfig files, and expose the
maximum as a user setting. Creates the Settings -> Power submenu.

## Scope

In scope:

- Make the `esp_pm` maximum frequency a runtime value instead of a compile time
  constant.
- New `CPU_FREQ` setting, default 160, which is exactly what the code does today.
- New Settings -> Power page holding the frequency roller.
- Apply the setting at boot after NVS is up, and immediately when changed.

Out of scope:

- Minimum frequency. Stays at 40 MHz.
- Light sleep enable or disable policy. That is PR06 and PR07.
- Changing `board_build.f_cpu` or the committed sdkconfig defaults.

## The mismatch

- `include/FurblePlatform.h:46` `const int CPU_MAX_FREQ_MHZ = 160;`
- `include/FurblePlatform.h:47` `const int CPU_MIN_FREQ_MHZ = 40;`
- `src/FurblePlatform.cpp:65-72` `Platform::setSleep()` builds `esp_pm_config_t`
  from those two constants and calls `ESP_ERROR_CHECK(esp_pm_configure(...))`.
- `platformio.ini:10` `board_build.f_cpu = 80000000L`
- `sdkconfig.m5stick-s3:1395` `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=80`
- `sdkconfig.m5stick-c-plus:1214` `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=80`

The boot frequency is 80 MHz. `esp_pm_configure` then raises the ceiling to
160 MHz, so DFS scales up to 160 MHz under load. The build system and the runtime
disagree about the intended maximum. Making it a setting settles it and keeps the
current behaviour as the default.

## Files to change

- `include/FurbleSettings.h:16-29`, add `CPU_FREQ` to `type_t`.
- `include/FurbleSettings.h:145-148`, add the `storage_type<CPU_FREQ>` binding
  next to the existing `AUTOCONNECT` binding.
- `src/FurbleSettings.cpp:11-24`, add the table row.
- `src/FurbleSettings.cpp:186-227`, add a `case CPU_FREQ:` to the default switch
  in `Settings::init`.
- `include/FurblePlatform.h:46-47`, replace `CPU_MAX_FREQ_MHZ` with a member
  holding the current maximum, keep `CPU_MIN_FREQ_MHZ` as a constant, and declare
  `setCPUMaxFreq(uint8_t mhz)` and `uint8_t getCPUMaxFreq(void) const`.
- `src/FurblePlatform.cpp:65-72`, `setSleep()` reads the member. Add
  `setCPUMaxFreq()` which stores the value, then re-applies the current sleep
  state through the same `esp_pm_configure` path.
- `src/FurblePlatform.cpp:13-14`, note that `setSleep(true)` runs before
  `M5.begin` and before NVS exists. Leave it alone.
- `src/main.cpp:27-29`, after `Furble::Settings::init()` apply the stored value:
  `Platform::getInstance().setCPUMaxFreq(Settings::load<Settings::CPU_FREQ>())`.
- `include/FurbleUI.h:176-182`, add `m_PowerStr = "Power"` to the settings
  string block.
- `include/FurbleUI.h:337`, declare `void addPowerMenu(const menu_t &parent);`
  next to `addDisplayMenu`.
- `src/FurbleUI.cpp:53-76`, add `{m_PowerStr, {nullptr, nullptr, nullptr,
  nullptr, {3, 1}}}` to `UI::m_Menu`.
- `src/FurbleUI.cpp:1851-1950`, add `addPowerMenu` next to `addDisplayMenu` and
  copy its roller pattern.
- `src/FurbleUI.cpp:2073-2079`, call `addPowerMenu(menu)` from
  `addSettingsMenu`.

## New settings

| Item | Value |
|---|---|
| Enum | `Settings::CPU_FREQ` |
| Display name | `CPU Speed` |
| NVS key | `cpu_freq` (8 chars, under the 15 char limit) |
| NVS namespace | `FURBLE_STR`, same as the other furble settings |
| Storage type | `uint8_t`, the frequency in MHz |
| Values | 80, 160, 240 |
| Default | 160 |

160 reproduces `CPU_MAX_FREQ_MHZ = 160` exactly, so a fresh NVS boot behaves like
master.

## Menu placement

```
Settings
+- Power
   +- CPU speed
```

The Core and Core2 settings page is a grid. `include/FurbleUI.h:217-220` defines
4 columns by 2 rows, so 8 slots. Current occupancy from `src/FurbleUI.cpp:53-76`:
row 0 is Display {0,0}, Features {1,0}, GPS {2,0}, Timer {3,0}; row 1 is Theme
{0,1}, TX Power {1,1}, About {2,1}. Slot {3,1} is the only free one, so Power
takes it. The grid is only applied under `FURBLE_M5COREX`
(`src/FurbleUI.cpp:2065-2069`); the Stick boards use a scrolling list and need no
placement work. PR05 adds Diagnostics and must grow the grid to a third row.

## Implementation notes

- Init order matters. `src/main.cpp:27-28` calls `Platform::init()` before
  `Settings::init()`, and `Settings::init()` is what calls `nvs_flash_init()`
  (`src/FurbleSettings.cpp:171`). So `Platform` cannot read the setting during
  its own construction. Apply the value from `app_main` after `Settings::init()`
  returns. Do not reorder init in this PR. PR16 handles the reorder with its own
  verification.
- `esp_pm_configure` rejects a `max_freq_mhz` the chip cannot produce and returns
  `ESP_ERR_INVALID_ARG`. The current call is wrapped in `ESP_ERROR_CHECK`, which
  aborts. Validate the value from NVS against a small allow list before calling,
  and fall back to 160 on anything unexpected. Keep `ESP_ERROR_CHECK` for the
  genuinely impossible case.
- Keep `min_freq_mhz` at 40. `CONFIG_XTAL_FREQ=40` on both chip families
  (`sdkconfig.m5stick-s3:1298`, `sdkconfig.m5stick-c-plus:1123`), so 40 MHz is the
  XTAL frequency, which is the correct minimum for light sleep.
- 240 MHz may not be selectable on every board with the committed sdkconfig. Call
  `esp_pm_configure` with each candidate once during bring up and drop any value
  that returns an error from the roller. Prefer offering fewer options to
  shipping a setting that aborts.
- Changing the frequency takes effect immediately. There is no restart button,
  unlike the Theme page at `src/FurbleUI.cpp:1980-1992`.
- Use the roller pattern from the inactivity control at
  `src/FurbleUI.cpp:1917-1933`: `lv_roller_set_options`,
  `lv_roller_set_selected`, save on `LV_EVENT_VALUE_CHANGED`. The roller index is
  not the stored value, so map index to MHz explicitly.

## Dependencies

- PR00 for on-device logs. Not strictly required.
- Blocks PR06, which moves this `esp_pm` configuration into `FurblePower`.

## Risks

- Lowering the maximum to 80 MHz may slow LVGL rendering enough to be visible,
  and may affect BLE connection setup timing. The default is unchanged, so only
  users who opt in are exposed.
- Raising the maximum to 240 MHz increases peak current and heat. Offer it only
  if it configures cleanly.
- A stale or corrupt NVS value could abort the boot through `ESP_ERROR_CHECK`.
  The allow list above removes that path.

## Verification

Build matrix:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device, M5StickS3 over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor -e m5stick-s3`.
2. Erase NVS or use a fresh device. Confirm the Power page shows 160 MHz and
   that the UI behaves as on master.
3. Select 80 MHz. Confirm no reboot, no error log, and the menus still respond.
4. Select 240 MHz. If `esp_pm_configure` errors, remove the option.
5. Power cycle. Confirm the selection persists.
6. Connect to the Fujifilm camera at 80 MHz and at 160 MHz. Confirm connect
   works and the shutter fires. Note any latency difference by feel and in the
   log timestamps around `UI::handleShutter` (`src/FurbleUI.cpp:540`).

One AXP192 device, M5StickC Plus, gets the same steps 1 to 5. The frequency path
is shared code, not board switched, so a smoke test is enough.

Battery drain. There is no external power meter, so measure on battery with the
device unplugged. For each of 80 MHz and 160 MHz, run 30 minutes of connected
idle and log `M5.Power.getBatteryLevel()` and `M5.Power.getBatteryVoltage()`
every 30 s to the console. Compare the voltage slope. Expect a small difference
at idle, because DFS already drops to the minimum when nothing runs. A null
result is a valid result and should be stated in the PR body. PR02 lands the
proper measurement harness; until then a temporary logging timer in
`src/FurbleUI.cpp:156-201` is acceptable for the measurement and must not be
committed.

Camera coverage: Fujifilm only. Other vendors are untouched by this PR. Say so
in the PR body.

## References

- ESP-IDF, Power Management, `esp_pm_configure`, `esp_pm_config_t`,
  `max_freq_mhz`, `min_freq_mhz`, `light_sleep_enable`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/power_management.html
- ESP-IDF, Sleep Modes, ESP32-S3:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/sleep_modes.html
- PlatformIO, ESP-IDF framework and sdkconfig handling:
  https://docs.platformio.org/en/latest/frameworks/espidf.html
- LVGL 9.4, Roller widget, `lv_roller_set_options`, `lv_roller_set_selected`,
  `LV_EVENT_VALUE_CHANGED`:
  https://lvgl.io/docs/open/9.4/details/widgets/roller
- M5Stack StickS3 product page:
  https://docs.m5stack.com/en/core/StickS3
