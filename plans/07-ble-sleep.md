# PR07: BLE modem sleep and light sleep while connected

## Goal

Let the S3 sleep while a BLE connection is up by enabling Bluetooth controller
modem sleep and a valid low power clock, and expose a user toggle for sleeping
while connected on every board that supports it. Default off, so current behavior
is unchanged.

## Scope

- M5StickS3 sdkconfig: enable BT controller modem sleep, select the low power
  clock, enable PHY MAC and baseband power down.
- ESP32 boards: audit the existing BTDM modem sleep configuration, document the
  per-board floor, change nothing that already works.
- New setting that controls whether the `NoLightSleep` lock is held while a
  camera is connected.
- Out of scope: scan power (PR08), connection parameters (PR10), GPS power
  (PR15), deep sleep (PR19).

## Files to change

| File | Anchor | Change |
|---|---|---|
| `sdkconfig.m5stick-s3` | 853-857 | `# CONFIG_BT_CTRL_MODEM_SLEEP is not set` becomes `=y` plus `MODE_1` |
| `sdkconfig.m5stick-s3` | 859-860 | `BT_CTRL_SLEEP_MODE_EFF=0`, `BT_CTRL_SLEEP_CLOCK_EFF=0` regenerate |
| `sdkconfig.m5stick-s3` | 1344 | `# CONFIG_ESP_PHY_MAC_BB_PD is not set` becomes `=y` |
| `include/FurbleSettings.h` | 16-29 | add `SLEEP_CONN` to `type_t` |
| `include/FurbleSettings.h` | 145-148 | add `storage_type<SLEEP_CONN>` = `bool` |
| `src/FurbleSettings.cpp` | 11-24 | add table row |
| `src/FurbleSettings.cpp` | 209-215 | add `SLEEP_CONN` to the `save<bool>(false)` default group |
| `src/FurbleControl.cpp` | 115-118 | acquire or release the connected sleep lock on state change |
| `src/FurbleControl.cpp` | 222-244 | release the lock on disconnect |
| `src/FurbleUI.cpp` | 2062-2082 | add the toggle to the Power submenu created by PR01 |

Verified current state, M5StickS3:

- `sdkconfig.m5stick-s3:856` `# CONFIG_BT_CTRL_MODEM_SLEEP is not set`.
- `sdkconfig.m5stick-s3:859-860` `CONFIG_BT_CTRL_SLEEP_MODE_EFF=0` and
  `CONFIG_BT_CTRL_SLEEP_CLOCK_EFF=0`.
- `sdkconfig.m5stick-s3:1271` `CONFIG_RTC_CLK_SRC_INT_RC=y`.
- `sdkconfig.m5stick-s3:1297-1298` `CONFIG_XTAL_FREQ_40=y`.
- `sdkconfig.m5stick-s3:1344` `# CONFIG_ESP_PHY_MAC_BB_PD is not set`.
- `sdkconfig.m5stick-s3:1359-1368` power management is already on:
  `CONFIG_PM_ENABLE=y`, `CONFIG_PM_DFS_INIT_AUTO=y`, `CONFIG_PM_SLP_IRAM_OPT=y`,
  `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP=y`.
- `sdkconfig.m5stick-s3:1680` `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`.
- No `CONFIG_BT_CTRL_LPCLK_SEL_*` line exists yet because modem sleep is off.

Verified current state, the four ESP32 boards
(`sdkconfig.m5stick-c`, `sdkconfig.m5stick-c-plus`, `sdkconfig.m5stack-core`,
`sdkconfig.m5stack-core2`), identical line numbers in all four:

- `:655` `CONFIG_BTDM_CTRL_MODEM_SLEEP=y`
- `:656` `CONFIG_BTDM_CTRL_MODEM_SLEEP_MODE_ORIG=y`
- `:658` `CONFIG_BTDM_CTRL_LPCLK_SEL_MAIN_XTAL=y`
- `:1103` `CONFIG_RTC_CLK_SRC_INT_RC=y`
- `:1179-1180` `CONFIG_PM_ENABLE=y`, `CONFIG_PM_DFS_INIT_AUTO=y`
- `:1473` `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`

So the ESP32 boards already run modem sleep mode ORIG on the main crystal. This
is the correct configuration for them. PR07 changes no ESP32 sdkconfig.

## New settings

| Enum | NVS key | Namespace | Type | Default |
|---|---|---|---|---|
| `SLEEP_CONN` | `sleep_conn` | `FURBLE_STR` | `bool` | `false` |

Key is 10 characters, under the 15 character NVS limit. Default `false` keeps a
`NoLightSleep` lock held for the whole connection, which reproduces today's
behavior on boards where light sleep would otherwise engage.

## Menu placement

`Settings > Power > Sleep while connected`. The Power submenu is created by PR01.
Use `UI::addSettingItem()` (`src/FurbleUI.cpp:705`), which already renders a bool
setting as a labelled switch.

Hide the item at runtime where it cannot work. Use `M5.getBoard()` for the check,
matching the existing `m_PMICHack` pattern at `src/FurblePlatform.cpp:24-32`.

## Implementation notes

S3 sdkconfig additions:

```
CONFIG_BT_CTRL_MODEM_SLEEP=y
CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1=y
CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL=y
CONFIG_ESP_PHY_MAC_BB_PD=y
```

- `BT_CTRL_MODEM_SLEEP_MODE_1` is the only supported mode. The controller sleeps
  between BLE events.
- `BT_CTRL_LPCLK_SEL_MAIN_XTAL` is the correct choice here. The main crystal
  stays powered during light sleep, which costs current but meets the 500 ppm
  sleep clock accuracy that BLE connections require.
- `CONFIG_BT_CTRL_LPCLK_SEL_RTC_SLOW` (the internal 136 kHz RC oscillator) is
  ruled out. Its accuracy is far worse than 500 ppm and it only supports
  advertising and scanning, not connections.
- `CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP` is only meaningful when the
  low power clock is the external 32 kHz crystal. Do not add it on the main
  crystal path. It belongs to the Experiment A stretch path below.
- Regenerate the sdkconfig with `idf.py menuconfig` rather than hand editing, so
  the derived `_EFF` values at lines 859-860 stay consistent.

Stretch path, gated by Experiment A (`plans/00-hardware-experiments.md`): if the
StickS3 turns out to have an external 32 kHz crystal, switch to
`CONFIG_BT_CTRL_LPCLK_SEL_EXT_32K_XTAL=y` plus
`CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP=y` and change
`CONFIG_RTC_CLK_SRC_INT_RC` to `CONFIG_RTC_CLK_SRC_EXT_CRYS`. Do not ship this
unless BLE init succeeds and a 30 minute connection holds. If BLE init fails the
crystal is absent and the main crystal path is final.

Runtime side:

- Add `Power::LockType::NoLightSleep` acquire and release around the connected
  state. Acquire when `Control` enters `STATE_ACTIVE`
  (`src/FurbleControl.cpp:115-118`) and release in `Control::disconnect()`
  (`src/FurbleControl.cpp:222-244`) and when the state leaves `STATE_ACTIVE`.
- When `SLEEP_CONN` is true, skip the acquire. When it is false, hold the lock.
  That is the whole feature.
- Read the setting once per connect, not per loop iteration.
- Do not call `esp_light_sleep_start()`. Manual light sleep drops the BLE
  connection. Only esp_pm automatic light sleep is valid here.

Expected floors, from the Espressif nimble power_save measurements:

| Chip | Max current | Modem sleep | Light sleep, main XTAL | Light sleep, 32 kHz XTAL |
|---|---|---|---|---|
| ESP32 | 231 mA | 14.1 mA | not supported | 1.9 mA |
| ESP32-S3 | 240 mA | 17.9 mA | 3.3 mA | 230 uA |

Board floors that limit the achievable win:

- AXP192 boards (StickC, StickC Plus, Core2) have roughly 2 mA of PMIC quiescent
  draw. Light sleep cannot go below that.
- ESP32 light sleep with BLE connected needs an external 32 kHz crystal, which
  none of the four ESP32 sdkconfigs select (`CONFIG_RTC_CLK_SRC_INT_RC=y` at
  `:1103`). On those boards the realistic target stays modem sleep, so the toggle
  should stay hidden or be marked as no effect unless a board is confirmed to
  have the crystal.

## Dependencies

- Requires PR06 for the named lock.
- Requires PR01 for the Power submenu and the resolved CPU frequency.
- Uses PR02 battery instrumentation as the measurement harness.
- Experiment A gates the stretch 230 uA path only.
- Blocks PR15 and PR19.

## Risks

- Connection instability. Modem sleep plus light sleep adds wake latency. A
  camera with a short supervision timeout may drop. Mitigated by the 30 minute
  soak test and by the default off toggle.
- Shutter latency. First command after an idle period may be slower. Measure it.
- Console output over USB CDC can be lost across light sleep on the S3. Log to a
  buffer and dump, or keep the drain runs unplugged and read NVS afterwards.
- The sdkconfig diff is large because menuconfig regenerates derived symbols.
  Review the diff and keep it to the intended options.
- Flash and PSRAM wake timing. `CONFIG_PM_SLP_IRAM_OPT=y` is already set, which
  helps, but confirm no new watchdog resets appear.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Fresh NVS boot. Confirm `SLEEP_CONN` defaults to false and behavior matches
   master exactly.
3. Connect to a Fujifilm camera. Fire the shutter 20 times with a 30 s idle gap
   before each. Record the delay from button press to shutter. Compare against
   master. Repeat with `SLEEP_CONN` on.
4. 30 minute connected soak with `SLEEP_CONN` on. No disconnects, no reconnect
   log entries, shutter still works at the end.
5. Repeat step 2 and 4 on one AXP192 board to confirm the ESP32 path is
   unchanged.

Battery drain, on-board instrumentation only, no external power meter:

- Unplugged 60 minute runs on M5StickS3, logging battery voltage and percent
  every 30 s. Four cases: master connected idle, this branch `SLEEP_CONN` off,
  this branch `SLEEP_CONN` on, this branch `SLEEP_CONN` on with the display off.
- Expect no change in the first two, and a large drop in the third. Report the
  measured percent per hour, not an estimated milliamp figure.
- Repeat the connected idle case on StickC Plus to record the AXP192 floor.

Camera testing:

- Only Fujifilm cameras are available. Run the full matrix on Fujifilm: connect,
  shutter latency, GEOTAG flow, 30 minute stability.
- FauxNY covers the non-BLE control path. Sony, Nikon, Canon and Ricoh are not
  hardware tested. Say so plainly in the PR body. Their code paths are untouched
  by this PR, the change is sdkconfig plus a lock, so the risk is uniform across
  vendors rather than vendor specific.

## References

- [Espressif nimble power_save example](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md)
  for the measured current table quoted above.
- [ESP-IDF BLE low power mode guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-ble.html)
  for the 500 ppm sleep clock accuracy requirement and the statement that the
  136 kHz RC oscillator cannot support connections.
- [ESP-IDF BLE controller Kconfig](https://raw.githubusercontent.com/espressif/esp-idf/master/components/bt/controller/esp32c3/Kconfig.in)
  for the exact option names and help text, including
  `BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP` applying to the external 32 kHz path.
- [ESP-IDF sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html)
  for automatic light sleep versus manual `esp_light_sleep_start()`.
- [ESP-IDF power management API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html)
  for the lock semantics used by the toggle.
