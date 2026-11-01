# Power research

This directory is the evidence base for the separate power PR. It must not be
mixed with camera parity work.

## Lanes

| Lane | File | Scope |
| --- | --- | --- |
| BLE | `ble.md` | Scan, advertise, connection parameters, PHY, reconnect, NimBLE |
| SoC and OS | `soc.md` | DFS, light sleep, CPU frequency, tasks, locks, logging, console |
| Display and UI | `display.md` | Brightness, timeout, refresh, backlight, LVGL |
| GPS and IMU | `gps-imu.md` | Fix rate, constellations, UBX and NMEA, UART, motion wake |
| Wi-Fi and coexistence | `wifi-coex.md` | AP and station power, transfer choice, coexistence |
| PMIC and board | `board-power.md` | Rails, charger, gauge, battery, leakage, datasheets |
| Measurement | `measurement.md` | Bench method, instruments, scenarios, thresholds |

## Rules

1. Every row is labeled measured, modeled, inferred, or unknown.
2. A model estimate is not a measurement.
3. A change without a test is not ready.
4. A change that weakens correctness or security is rejected.
5. The camera parity PR and the power PR stay independent.
6. Every datasheet claim cites part, document, revision, and relevant limit.

## Consolidation

The coordinator merges the lanes into:

- `opportunity-matrix.md`: ranked opportunities with evidence and savings.
- `measurement-plan.md`: bench scenarios and acceptance thresholds.
- `datasheet-register.md`: every part and document used.

A row is accepted only when it names its current behavior, proposed change,
evidence, estimated saving, risk, effort, and required test.
