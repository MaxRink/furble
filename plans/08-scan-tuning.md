# PR08: scan duty presets and scan timeout

## Goal

Replace the fixed 100 percent duty cycle BLE scan with three user selectable
presets, and add an optional scan timeout so an abandoned scan does not run
forever. Defaults reproduce today's behavior.

## Scope

- Scan interval and window become preset driven.
- Scan start gets an optional duration.
- New `Settings > Bluetooth` submenu holds the two new settings and the existing
  TX Power page.
- All boards. Nothing here is hardware specific.
- Out of scope: connection parameters (PR10), adaptive TX power (PR11).

## Files to change

| File | Anchor | Change |
|---|---|---|
| `lib/furble/Scan.cpp` | 14-21 | replace fixed interval and window with preset lookup |
| `lib/furble/Scan.cpp` | 38-45 | pass the timeout to `m_Scan->start()` instead of `0` |
| `lib/furble/Scan.cpp` | 47-50 | force full duty for the pairing rescan overload |
| `lib/furble/Scan.h` | 22-56 | add `Mode` enum, `applyMode()`, `onScanEnd()` override |
| `src/FurbleUI.cpp` | 928-948 | discovery scan start, handle a finite scan ending |
| `src/FurbleUI.cpp` | 2062-2082 | add `addBluetoothMenu()`, move `addTransmitPowerMenu()` under it |
| `src/FurbleUI.cpp` | 53-76 | add the Bluetooth grid slot, adjust the Settings page grid |
| `include/FurbleUI.h` | 176-182 | add `m_BluetoothStr` |
| `include/FurbleSettings.h` | 16-29 | add `SCAN_MODE`, `SCAN_TIMEOUT` |
| `include/FurbleSettings.h` | 145-148 | add both `storage_type` bindings |
| `src/FurbleSettings.cpp` | 11-24 | add two table rows |
| `src/FurbleSettings.cpp` | 186-227 | add both defaults |

Verified current state:

- `lib/furble/Scan.cpp:17-20` sets `setActiveScan(true)`, `setInterval(6553)` and
  `setWindow(6553)`. In esp-nimble-cpp 2.5.0 both take milliseconds and convert
  to 0.625 ms units internally, so interval equals window and the radio scans at
  100 percent duty.
- `lib/furble/Scan.cpp:44` calls `m_Scan->start(0, false)`. Duration 0 maps to
  `BLE_HS_FOREVER`, so discovery scans run until stopped.
- `lib/furble/Scan.cpp:47-50` is a second overload used only during pairing
  reconnect: `lib/furble/Nikon.cpp:82` and `lib/furble/FujifilmSecure.cpp:82`,
  both with `SCAN_TIME_MS = 60000` (`Nikon.h:58`, `FujifilmSecure.h:103`).
- `src/FurbleUI.cpp:939-947` starts the discovery scan when the Scan page loads.
  `src/FurbleUI.cpp:911` and `:957` stop it on the main and Connected pages.
- `src/FurbleUI.cpp:1253` stops it on disconnect.
- `src/FurbleUI.cpp:2073-2079` builds the Settings page children in this order:
  Display, Features, GPS, Timer, Theme, TX Power, About.

## New settings

| Enum | NVS key | Namespace | Type | Default |
|---|---|---|---|---|
| `SCAN_MODE` | `scan_mode` | `FURBLE_STR` | `uint8_t` | `0` (Full) |
| `SCAN_TIMEOUT` | `scan_timeout` | `FURBLE_STR` | `uint32_t` | `0` (no timeout) |

Keys are 9 and 12 characters, under the 15 character NVS limit.

Preset table:

| Value | Name | Window | Interval | Duty |
|---|---|---|---|---|
| 0 | Full | 6553 ms | 6553 ms | 100 percent |
| 1 | Balanced | 30 ms | 120 ms | 25 percent |
| 2 | Low | 50 ms | 1000 ms | 5 percent |

Full reproduces `lib/furble/Scan.cpp:19-20` exactly.

Scan timeout choices in the UI: 0 (off), 30 s, 60 s, 120 s, stored in seconds and
multiplied by 1000 before it reaches `NimBLEScan::start()`, which takes
milliseconds.

## Menu placement

- New `Settings > Bluetooth` submenu, created by this PR.
- Contents after this PR: `TX Power` (moved from the Settings root),
  `Scan mode` roller, `Scan timeout` roller.
- Move `addTransmitPowerMenu()` from the Settings page to the Bluetooth page.
  This is pure navigation and it frees a grid slot. The Settings page grid on
  Core and Core2 is 4 columns by 2 rows (`include/FurbleUI.h:217-220`), so it
  holds 8 items. Seven are used today. PR01 adds Power and PR05 adds
  Diagnostics, so without moving TX Power this PR overflows the grid. If the
  grid still overflows, extend `m_GridLayoutRowDsc` to three rows in the same
  change and note it in the PR body.

## Implementation notes

- Add a `Scan::Mode` enum and an `applyMode()` helper that calls `setInterval()`
  and `setWindow()`. Call it from `Scan::start()` rather than only in
  `getInstance()`, so a settings change takes effect on the next scan without a
  restart.
- The pairing rescan overload at `lib/furble/Scan.cpp:47-50` must force Full
  duty. `FujifilmSecure` and `Nikon` wait up to 60 s for a saved camera to
  advertise. A 5 percent duty there would turn a fast reconnect into a timeout.
  Set full duty on entry and restore the user preset on exit, or take a
  `Mode` argument with a Full default.
- Scan timeout: pass `SCAN_TIMEOUT * 1000` to `m_Scan->start()`. Zero keeps the
  current `BLE_HS_FOREVER` behavior.
- Handle scan end in the UI. `NimBLEScanCallbacks` in esp-nimble-cpp 2.5.0
  declares `onScanEnd(const NimBLEScanResults &results, int reason)`.
  `Furble::Scan` already derives from `NimBLEScanCallbacks`
  (`lib/furble/Scan.h`), so override it and surface a "scan finished" state on
  the Scan page with a way to restart. Do not leave the page looking like it is
  still scanning.
- The controller already filters duplicate advertisements
  (`CONFIG_BT_CTRL_BLE_SCAN_DUPL=y`, `sdkconfig.m5stick-s3:841`), so a device
  that advertised once during a missed window is not re-reported immediately.
  Lower duty therefore costs discovery latency, not just power.
- Advertising interval per the Bluetooth specification ranges from 20 ms to
  10.24 s. The scan window must be long enough to overlap a camera's advertising
  events often enough to be usable. Fujifilm's advertising interval is not
  documented. Measure it during verification and adjust the Balanced and Low
  numbers if discovery is unreliable.

## Dependencies

- Independent of PR06 and PR07.
- Creates the Bluetooth submenu that PR09, PR10 and PR11 expect.
- Should land after PR01 and PR05 so the Settings grid pressure is understood.

## Risks

- Discovery regression. Lower duty makes cameras take longer to appear. Mitigated
  by defaulting to Full and by measuring the Fujifilm advertising interval.
- Pairing reconnect regression for `FujifilmSecure` and `Nikon` if the preset
  leaks into the second `start()` overload. This is the main correctness risk in
  the PR. Cover it with an explicit test.
- Moving the TX Power page changes muscle memory for existing users. It matches
  the agreed final settings tree, so accept it and mention it in the PR body.
- A finite scan that ends silently looks like a hang. Handle `onScanEnd`.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Fresh NVS boot. Confirm `SCAN_MODE` is Full and `SCAN_TIMEOUT` is 0, and that
   the Scan page behaves exactly as on master.
3. Measure the Fujifilm advertising interval. Log the timestamp of each
   `onResult` for the camera from `lib/furble/Scan.cpp:29-36` at Full duty over
   60 s and take the median gap. Record it in the PR body.
4. For each preset, measure time from opening the Scan page to the camera
   appearing. Five trials each, camera at 1 m, then repeat at 5 m. Report medians.
5. Set the scan timeout to 30 s with no camera present. Confirm the scan stops,
   the UI shows the finished state and a restart works.
6. Pairing rescan test: with the preset set to Low, connect to a saved
   `FujifilmSecure` camera. It must reconnect at the same speed as on master.
   This proves the Full duty override on the second overload works.
7. Repeat steps 2, 4 and 6 on one AXP192 board.

Battery drain, on-board instrumentation only, no external power meter:

- Unplugged 30 minute runs on M5StickS3 parked on the Scan page with no camera
  present, one run per preset. Log battery voltage and percent every 30 s.
  Report percent per hour for Full, Balanced and Low.

Camera testing:

- Only Fujifilm cameras are available. Run steps 3, 4 and 6 on Fujifilm.
- FauxNY (`Settings > Features > FauxNY`) injects a fake camera into the Scan
  list at `src/FurbleUI.cpp:933-936` and exercises the list and connect paths
  without a radio.
- Sony, Nikon, Canon and Ricoh are not hardware tested. Nikon shares the pairing
  rescan path with `FujifilmSecure`, so the Fujifilm result in step 6 is the
  closest available evidence for it. State this plainly in the PR body.

## References

- [esp-nimble-cpp 2.5.0 NimBLEScan.h](https://raw.githubusercontent.com/h2zero/esp-nimble-cpp/2.5.0/src/NimBLEScan.h)
  for `setInterval(uint16_t intervalMs)`, `setWindow(uint16_t windowMs)`,
  `start(uint32_t duration, bool isContinue, bool restart)` and the
  `NimBLEScanCallbacks::onScanEnd(const NimBLEScanResults &, int)` signature.
- [esp-nimble-cpp 2.5.0 NimBLEScan.cpp](https://raw.githubusercontent.com/h2zero/esp-nimble-cpp/2.5.0/src/NimBLEScan.cpp)
  for the millisecond to 0.625 ms unit conversion and for duration 0 mapping to
  `BLE_HS_FOREVER`.
- [ESP-IDF BLE device discovery guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/ble/get-started/ble-device-discovery.html)
  for scan window and scan interval semantics and the 20 ms to 10.24 s
  advertising interval range.
