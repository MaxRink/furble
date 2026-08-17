# PR11: RSSI based adaptive transmit power

## Goal

Step BLE transmit power down when the camera is close and back up when the link
weakens, never above the level the user selected. Default off, so current
behavior is unchanged.

## Scope

- Periodic RSSI sampling on connected clients.
- Power stepping with hysteresis between the lowest level and the user cap.
- New setting to enable the feature.
- All boards. The API is the same on ESP32 and ESP32-S3.
- Out of scope: connection parameters (PR10), scan power (PR08).

## Files to change

| File | Anchor | Change |
|---|---|---|
| `src/FurbleSettings.cpp` | 90-106 | keep the existing 0/1/2 to P3/P6/P9 mapping as the cap |
| `lib/furble/Camera.cpp` | 14-19 | on connect, apply the adaptive level instead of the raw cap |
| `lib/furble/Camera.h` | 116-138 | add `getRssi()` passthrough and current level accessor |
| `lib/furble/Camera.h` | 192 | `m_Power` stays the cap, add a separate current level member |
| `src/FurbleControl.cpp` | 135-187 | sample RSSI and step power from the control task |
| `src/FurbleControl.cpp` | 273-275 | `setPower()` becomes "set cap", reset the current level |
| `include/FurbleControl.h` | 126-127 | update the `setPower()` comment |
| `include/FurbleSettings.h` | 16-29 | add `TX_ADAPTIVE` |
| `include/FurbleSettings.h` | 145-148 | add `storage_type<TX_ADAPTIVE>` = `bool` |
| `src/FurbleSettings.cpp` | 11-24 | add table row |
| `src/FurbleSettings.cpp` | 209-215 | add to the `save<bool>(false)` default group |
| `src/FurbleUI.cpp` | 1997-2040 | add the toggle to the TX Power page |

Verified current state:

- `src/FurbleSettings.cpp:90-106` maps the stored `uint8_t` to
  `ESP_PWR_LVL_P3` (0), `ESP_PWR_LVL_P6` (1), `ESP_PWR_LVL_P9` (2), defaulting to
  `ESP_PWR_LVL_P3`.
- `src/FurbleSettings.cpp:196-198` defaults `TX_POWER` to 0, which is P3.
- `lib/furble/Device.cpp:42` calls `NimBLEDevice::setPower(power)` once at init,
  from `src/main.cpp:29`.
- `lib/furble/Camera.cpp:14-19` calls `NimBLEDevice::setPower(m_Power)` again on
  every connect, with the log line "Connected, adjusting transmit power to %d".
- `lib/furble/Camera.cpp:27-30` stores the level passed to `connect()`.
- `src/FurbleControl.cpp:106` passes `m_Power`, set by
  `Control::setPower()` at `src/FurbleControl.cpp:273-275` from the UI slider at
  `src/FurbleUI.cpp:2020-2023`.
- `src/FurbleUI.cpp:1997-2040` is the TX Power page: a slider with range 0 to 2
  that saves on `LV_EVENT_RELEASED`.
- No file in `src/`, `include/` or `lib/` calls `getRssi()` today. RSSI is only
  logged for advertisements at `lib/furble/Scan.cpp:31`.

## New settings

| Enum | NVS key | Namespace | Type | Default |
|---|---|---|---|---|
| `TX_ADAPTIVE` | `tx_adaptive` | `FURBLE_STR` | `bool` | `false` |

Key is 11 characters, under the 15 character NVS limit. Default `false` leaves
`lib/furble/Camera.cpp:17` applying the user cap directly, exactly as today.

## Menu placement

`Settings > TX Power > Adaptive`, added to the existing page at
`src/FurbleUI.cpp:1997-2040` as a switch above the slider. This keeps the PR
independent of PR08.

Once PR08 has landed, the TX Power page lives under `Settings > Bluetooth`, and
the toggle moves with it. No extra work.

Show the live level and RSSI on the PR05 Diagnostics BLE page if PR10 has already
added it.

## Implementation notes

- Sampling: `NimBLEClient::getRssi()` in esp-nimble-cpp 2.5.0 returns the value
  from `ble_gap_conn_rssi()` and returns 0 when the client is not connected.
  Treat 0 as "no sample" and skip that round.
- Sample every 5 seconds from the control task loop
  (`src/FurbleControl.cpp:135-187`), which already wakes every 50 ms. Do not add
  a new task.
- Smooth the samples. Use an exponentially weighted moving average with a small
  weight so a single bad packet does not step the power.
- Hysteresis, starting values to be tuned on device:
  - step down one level when the averaged RSSI is above -60 dBm for three
    consecutive samples,
  - step up one level when it is below -80 dBm for two consecutive samples,
  - never step above the cap from `TX_POWER`, never below `ESP_PWR_LVL_P3`.
- Step up faster than down. Losing a link costs more than a small power saving.
- Reset to the cap on connect, on disconnect and whenever the user moves the
  slider. Reconnects should not inherit a stale low level.
- `NimBLEDevice::setPower()` is global, not per connection. Its 2.5.0 signature
  is `static bool setPower(int8_t dbm, NimBLETxPowerType type = NimBLETxPowerType::All)`,
  where the type can be `Advertise`, `Scan`, `Connection` or `All`. With
  multi-connect, drive the level from the weakest link across all connected
  cameras, and consider scoping the call to `NimBLETxPowerType::Connection` so
  scanning is not affected.
- Confirm the units before shipping. `lib/furble/Camera.cpp:17` passes an
  `esp_power_level_t` enum into an `int8_t dbm` parameter. Read the applied value
  back with `NimBLEDevice::getPower()` on device and log both, so the mapping is
  proven rather than assumed. If the enum does not equal dBm, fix the call site
  and say so in the PR body, because it also affects the existing non adaptive
  path.
- Keep the whole feature behind the setting check so the default path is byte for
  byte the current behavior.

## Dependencies

- Independent. Does not need PR06, PR07, PR08 or PR10.
- The Diagnostics BLE page from PR05 and PR10 makes verification easier but is
  not required.

## Risks

- Dropped connections at range. Stepping down too eagerly on a marginal link
  causes disconnects that look random. Mitigated by the asymmetric hysteresis and
  by defaulting to off.
- RSSI is noisy and antenna orientation dependent. A hand moving over the device
  changes it by 10 dB. The averaging window matters more than the thresholds.
- Global power. On a multi-connect setup the level applies to every link, so a
  strong link and a weak link cannot be served differently. Always use the
  weakest.
- The existing enum versus dBm question above. Resolve it before tuning
  thresholds, otherwise the tuning is meaningless.
- The power saving is small compared with PR07 and PR10. Do not oversell it.
  The value is range robustness as much as battery.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Fresh NVS boot. Confirm `TX_ADAPTIVE` defaults to false and that the connect
   log at `lib/furble/Camera.cpp:15` reports the same level as master.
3. Log `NimBLEDevice::getPower()` right after `setPower()` and record whether the
   applied dBm matches the intended level. Put the result in the PR body.
4. Enable the setting. With the camera at 0.5 m, confirm the level steps down and
   settles. Walk to 10 m and confirm it steps back up before the link degrades.
5. Shutter latency and reliability at 0.5 m, 5 m and 10 m, 20 presses each, with
   the setting on and off. No missed commands.
6. 60 minute connected soak with the setting on and the camera stationary at 5 m.
   Zero disconnects. Confirm the level is not oscillating in the log.
7. Move the TX Power slider while connected. Confirm the cap takes effect
   immediately and the adaptive level is clamped to it.
8. Repeat steps 2, 4 and 6 on one AXP192 board.

Battery drain, on-board instrumentation only, no external power meter:

- Unplugged 60 minute connected idle runs on M5StickS3 at 0.5 m: setting off with
  the cap at P9, and setting on with the cap at P9. Log battery voltage and
  percent every 30 s and report percent per hour. Expect a small difference. Say
  so honestly if it is inside the noise.

Camera testing:

- Only Fujifilm cameras are available. Run every step above on Fujifilm.
- FauxNY does not use a radio and reports no RSSI, so it only proves the code does
  not crash when `getRssi()` returns 0. Include that as a test.
- Sony, Nikon, Canon and Ricoh are not hardware tested. The change is in shared
  code, and TX power is global, so state plainly in the PR body which vendors were
  tested and that the setting defaults to off.

## References

- [esp-nimble-cpp 2.5.0 NimBLEDevice.h](https://raw.githubusercontent.com/h2zero/esp-nimble-cpp/2.5.0/src/NimBLEDevice.h)
  for `setPower(int8_t dbm, NimBLETxPowerType type)` and `getPower()`, and for
  power being global rather than per connection.
- [esp-nimble-cpp 2.5.0 NimBLEClient.cpp](https://raw.githubusercontent.com/h2zero/esp-nimble-cpp/2.5.0/src/NimBLEClient.cpp)
  for `getRssi()` returning `ble_gap_conn_rssi()` and 0 when not connected.
- [Espressif nimble power_save example](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md)
  for the scale of radio current, which sets expectations for how much transmit
  power stepping can save.

## Hardware verification, 2026-08-17

Verified on the combined image. With `tx_adaptive` on and `tx_power 0` (P3),
connecting to the X100VI logged
`Connected, transmit power requested 3 dBm (level 9), set ok`. Level 9 is
ESP_PWR_LVL_P3 in the IDF 5.4 enum, so the boot transmit power now matches the
stored setting. The old code passed the enum value as dBm which pinned P3
incorrectly, the fix holds on hardware.
