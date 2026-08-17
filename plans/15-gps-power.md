# PR15 - GPS power duty cycling and light sleep compatible UART

## Goal

Stop GPS from blocking light sleep on the S3, and add optional receiver duty
cycling. Today enabling GPS disables automatic light sleep for the whole device,
which is the single largest power regression in the firmware. Replace that with a
short lock held only around each expected NMEA burst.

## Scope

In scope:

- Replace the blanket `setSleep(false)` with a burst windowed NO_LIGHT_SLEEP
  lock.
- Use `UART_SCLK_XTAL` on the S3 so the baud rate survives frequency scaling.
- Window tracking and resync driven by checksum failures.
- Receiver power policy: always on, `$PCAS12` standby, 5 V rail cycling.

Out of scope:

- Motion based rate adaptation (PR18).
- Deep sleep between intervalometer shots (PR19).
- Any change to what furble does with a fix.

## Files to change

| File | Anchor | Change |
|---|---|---|
| `src/FurbleGPS.cpp` | `:28-41` `uart_config_t`, source clock at `:35-39` | Preserve the master clock fix: `UART_SCLK_XTAL` on S3 and `UART_SCLK_REF_TICK` elsewhere |
| `src/FurbleGPS.cpp` | `:42-48` driver install, pattern detect on `'\n'` at `:46` | Timestamp bursts, feed the window tracker |
| `src/FurbleGPS.cpp` | `:73-109` `task` | Acquire before the window, release after the burst |
| `src/FurbleGPS.cpp` | `:111-124` `enable`, `:120-123` S3 lock | Keep the named lock during acquisition, then window it |
| `src/FurbleGPS.cpp` | `:126-133` `disable`, `:130-132` S3 lock | Release the named lock on disable |
| `src/FurbleGPS.cpp` | `:118` and `:128` `M5.Power.setExtOutput(..., m5::ext_PA)` | Rail cycling policy |
| `src/FurbleGPS.cpp` | `:195-206` `serviceSerial` | Burst end detection and checksum feedback |
| `include/FurbleGPS.h` | `:35-38` constants, `:40-43` private methods | Window state, policy state |
| `src/FurblePlatform.cpp` | `:65-72` `setSleep` | No longer called by GPS |
| `include/FurbleSettings.h` | `:16-29` enum, `:101-148` `storage_type` | Add `GPS_POWER`, `GPS_DUTY` |
| `src/FurbleSettings.cpp` | `:11-24` table, `:186-227` default switch | Add rows and defaults |
| `src/FurbleUI.cpp` | `:1514-1604` `addGPSMenu` | Add the power saving subpage |

## New settings

| Enum | NVS key | Namespace | Type | Values | Default |
|---|---|---|---|---|---|
| `GPS_POWER` | `gps_power` (9 chars) | `FURBLE_STR` | `uint8_t` | 0 always on, 1 `$PCAS12` standby, 2 5 V rail cycling | 0 |
| `GPS_DUTY` | `gps_duty` (8 chars) | `FURBLE_STR` | `uint8_t` | seconds asleep between fix windows, 0 = none | 0 |

Defaults keep the receiver permanently powered, which is current behaviour. The
light sleep change is not a setting. It is a correctness fix and applies always.

## Menu placement

Settings -> GPS -> Power saving, a subpage added by this PR to the nested GPS
menu created in PR14. Two rollers, "Receiver" and "Sleep between fixes". Hidden
when GPS is disabled, following the existing hide logic at
`src/FurbleUI.cpp:1550-1553`.

## Implementation notes

### Light sleep and the UART

- Master already gives GPS a named, counted `ESP_PM_NO_LIGHT_SLEEP` lock while
  it is enabled. This PR keeps that lock through acquisition and each receive
  burst, then releases it during the predicted gap.
- UART RX does not work during automatic light sleep. UART wakeup exists but is
  useless here: the ESP-IDF docs state that the character which triggers wakeup,
  and any characters before it, are not received after wakeup. An NMEA sentence
  cannot survive that, so furble must be awake before the burst starts, not woken
  by it.
- Clock source. Master already uses `UART_SCLK_XTAL` on the S3 and
  `UART_SCLK_REF_TICK` elsewhere. This PR does not change the UART clock
  configuration.

### Burst window

- With a known fix interval from PR14 `GPS_RATE`, bursts are periodic. Predict
  the next burst start, acquire the lock `W` ms early, and release it once the
  burst is complete.
- Burst complete means either the RMC sentence has been parsed or the UART has
  been idle for more than a gap threshold. Pattern detection on `'\n'` is already
  configured at `src/FurbleGPS.cpp:46` and gives a per sentence event, so the
  gap is easy to measure in `task` (`src/FurbleGPS.cpp:73-109`).
- Start with `W = 50` ms. On a checksum failure, widen `W` by 25 ms up to a cap
  of half the fix interval. After 10 consecutive clean bursts, shrink by 10 ms
  down to a floor of 20 ms. This is a slow control loop, not a tight one.
- Resync. Track the measured burst start against the prediction. If the drift
  exceeds the window, or if `failedChecksum()` rises for several bursts in a row,
  hold the lock continuously for one full interval to re-observe the timing, then
  resume windowing.
- Unknown interval. If `GPS_RATE` is 0, furble has not configured the receiver
  and the interval is whatever the module defaults to. Measure it: hold the lock
  for about 5 seconds, record burst start times, take the median period. If the
  period is not stable, fall back to holding the lock permanently, which is
  exactly today's behaviour and therefore always safe.
- The lock must be released on every exit path, including `disable()`, GPS
  setting reloads and task shutdown. Use an RAII helper from PR06 so a missed
  release is not possible.

### Receiver power policy

- Always on, the default. The module draws 31.64 mA at 5 V per the M5Stack
  specification. That is the dominant GPS cost once light sleep works.
- `$PCAS12,<seconds>` puts the receiver into standby for a number of seconds.
  Experiment B confirmed that the AT6668 stops NMEA output and resumes on its
  own after the configured delay.
- 5 V rail cycling uses the existing `M5.Power.setExtOutput` calls at
  `src/FurbleGPS.cpp:118` and `:128`. This removes the module draw completely but
  loses the ephemeris if the unit has no backup supply.
- The M5Stack Unit GPS v1.1 has no backup supply. Experiment B measured a
  107.7 s cold refix after a 60 s rail cut. Rail cycling is therefore retained
  only as an explicitly discouraged expert option. Always on remains the
  default, even though timed standby is available and verified.
- Both duty cycled policies must respect `MAX_AGE_MS`, 30 seconds
  (`include/FurbleGPS.h:38`). `GPS_DUTY` above roughly 20 seconds will make the
  fix stale between windows and geotagging will silently stop. Clamp the roller
  options to 5, 10 and 15 seconds and say why.

### Failure behaviour

- No fix already fails safe. `GPS::update` (`src/FurbleGPS.cpp:161-192`) checks
  age and validity, clears `m_HasFix`, updates the status icon and simply does
  not call `Control::updateGPS`. Nothing downstream breaks.
- Dropped bytes appear as TinyGPS++ checksum failures, not as wrong coordinates,
  because the library validates each sentence. That is what makes the adaptive
  window safe.

## Implementation status

Implemented on `feat/15-gps-power`.

Rebase notes:

- `GPS_POWER` is wire_id 25 and `GPS_DUTY` is wire_id 26, continuing after
  `DISPLAY_OFF` (24) from PR 26.
- `src/FurbleCompanion.cpp` settingType and settingValue treat both as uint8
  settings. The companion write path does not trigger a GPS reload; that
  matches the existing `GPS` and `GPS_BAUD` behaviour on master.

- Added `GPS_POWER` with `0` as always on, `1` as timed `$PCAS12` standby, and
  `2` as an explicitly discouraged 5 V rail cycling option. Added `GPS_DUTY`
  with a default of `0` and menu values of 5, 10, and 15 seconds. Untouched
  settings therefore keep the receiver rail on and preserve the existing
  always-on receiver behavior.
- Reused the existing `GPS::sendCommand()` PCAS framing and checksum path for
  `$PCAS12,<GPS_DUTY>`. The command is queued only after a fresh valid fix has
  been pushed to the application.
- The GPS task now uses these states: `ACQUIRING` holds the lock while the first
  burst is found, `MEASURING` learns an unknown interval for five seconds,
  `BURST` holds the lock while NMEA data is received, `WAITING` releases the
  lock until the next burst window, `STANDBY` releases it during timed
  `$PCAS12` sleep, `RAIL_OFF` releases it while the optional rail is cut,
  `RESYNC` holds it for one full interval after timing drift or repeated
  checksum failures, and `PERMANENT_LOCK` is the safe fallback for unstable or
  missing timing.
- The receive lock starts 50 ms before the predicted burst, releases after a
  75 ms UART idle gap, widens by 25 ms on a checksum-failed burst up to half the
  interval, and shrinks by 10 ms after ten clean bursts down to 20 ms. A burst
  start that drifts beyond the current window, or three failed bursts in a row,
  enters `RESYNC` and re-anchors the next prediction to observed timing.
- The experiment-driven default is always on. Experiment B in
  `plans/00-hardware-experiments.md` verified that `$PCAS12` standby works, but
  also found that a 60 s VCC cut caused a 107.7 s cold refix because the GPS
  unit has no backup supply. Rail cycling is therefore not the default and is
  marked NOT recommended in the menu.
- Hardware verification still pending: standby-cycle current draw and fix
  freshness on the M5StickC Plus S3 with GPS unit v1.1, to be confirmed via the
  USB console.

Review fixes:

- The burst idle check is hoisted out of the state switch in `serviceCycle()`
  and runs in every state. The per state early returns in `finishBurst()`
  decide what happens next. This makes `MEASURING` and `RESYNC` exit again:
  before the fix both were terminal once `beginBurst()` set `m_BurstActive`,
  which parked the state machine, kept the NO_LIGHT_SLEEP lock forever in
  `RESYNC`, and let a stale `m_MeasureDeadline` turn `cycleWait()` into a zero
  wait busy spin that starved the UI, console and idle tasks. `cycleWait()` now
  mirrors the hoist with a burst gap term valid in every state and clamps the
  result to a 10 ms floor so a stale deadline can never spin the task loop.
- `enable()` now parks the GPS task first by clearing `m_Enabled`, does the
  UART and PMIC work with no mutex held, resets all cycle state under the new
  `m_CycleMutex`, and only then sets `m_Enabled` and acquires the power lock.
  `serviceCycle()` and `serviceSerial()` take `m_CycleMutex` with a try lock
  and skip the pass while a reset is in flight. The `ACQUIRING` case re-asserts
  the power lock each pass, so a release raced by `enable()` can no longer
  leave the receiver deaf under light sleep.
- Rail cycling PMIC calls go through `setRailPower()`, which reads the rail
  state back and retries once, matching the documented M5PM1 behaviour where
  the first access after idle sleep only wakes the chip and fails. Rail
  cycling remains experimental: GPS unit v1.1 has no
  backup supply, a rail cut costs a ~108 s cold refix which the 5 s wake budget
  can never cover, so the mode degrades to a permanent lock (always on). The
  menu help text says so.
- `finishMeasurement()` sizes its scratch array with
  `decltype(m_PeriodSamples)`, the console rejects `GPS_DUTY` values outside
  0, 5, 10 and 15, and the `cycleWait()` base wait stays at 100 ms, so the GPS
  task still wakes 10 times per second during `STANDBY`.

## Dependencies

- PR06 for the named pm lock API. Hard dependency.
- PR14 for a known fix interval and for the `$PCAS` send path used by `$PCAS12`.
  Hard dependency.
- Experiment B results in `plans/00-hardware-experiments.md` for the power
  policy decision.
- PR01 and PR07 interact with the same `esp_pm` configuration but do not block
  this work.

## Risks

- Highest risk in the GPS phase. A window that is too narrow drops the start of
  every burst and geotagging degrades quietly. Mitigate with the checksum driven
  widening, the permanent hold fallback, and a visible failure counter on the
  diagnostics page from PR14.
- Frequency scaling corrupting the baud rate if the clock source change is
  missed on any board. Only the S3 path changes; the others already use a fixed
  clock.
- `$PCAS12` support is verified on the tested AT6668 unit. If a different module
  ignores it, the missing burst timing must fall back to the permanent lock so
  the device keeps working safely.
- Rail cycling on a module with no backup supply can make fixes take tens of
  seconds. Not the default.
- The S3 light sleep floor depends on PR07. If PR07 has not landed, this PR still
  removes a hard blocker but the measured win will be smaller.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device, M5StickS3 with GPS Unit v1.1 on Port A, over USB:

1. Fresh NVS boot with GPS enabled at 115200. Confirm a fix and confirm the
   device now enters light sleep with GPS running. Check the pm lock dump on the
   power state page from PR05.
2. Log `charsProcessed()`, `passedChecksum()` and `failedChecksum()` for 10
   minutes. The failure rate must be no worse than master, which never sleeps.
3. Set the fix rate to 500 ms and then 200 ms from PR14. Confirm the window
   tracker follows and that the failure rate stays flat.
4. Set the rate back to "do not send" so the interval is unknown. Confirm the
   measurement path finds the period, or that it falls back to a permanent lock
   without dropping sentences.
5. Enable `$PCAS12` standby with 10 second duty. Confirm NMEA silence during
   standby and a fix within the 30 second age budget after each wake.
6. Leave rail cycling disabled for normal use. The recorded Experiment B result
   is a 107.7 s cold refix after a 60 s rail cut, so the menu labels this mode
   NOT recommended.
7. Cover the antenna to force fix loss. Confirm the icon changes, geodata stops
   and nothing crashes or spins.
8. Connect a Fujifilm camera with GEOTAG. Confirm the on request geodata path is
   still served across sleep cycles, for all three policies.

Battery drain runs, unplugged, on board instrumentation only, no external power
meter:

- 30 to 60 minutes per state, logging battery percent and voltage every 30 s to
  the console, dumped after the run. On AXP192 boards also log the
  `M5.Power.getBatteryCurrent()` EWMA.
- States: connected plus GPS on master, connected plus GPS with windowed sleep,
  connected plus GPS with standby duty cycling, connected plus GPS with rail
  cycling.
- The expected ordering is master worst, then windowed always on, then windowed
  with duty cycling. If duty cycling does not beat always on, ship it disabled
  and say so.

Cameras: Fujifilm only. GEOTAG is the only vendor path affected. Other vendors
are untouched. State that in the PR body.

## References

- ESP-IDF UART API guide, wakeup behaviour and the requirement for a fixed
  frequency source clock that stays active during sleep:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/uart.html
- ESP-IDF sleep modes, UART wakeup is light sleep only and the triggering
  character plus everything before it is lost:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html
- ESP-IDF power management, `esp_pm_configure`, `ESP_PM_NO_LIGHT_SLEEP`,
  `esp_pm_lock_create`, `esp_pm_lock_acquire` and `esp_pm_lock_release`, and the
  recursive lock semantics used by the burst window:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html
- M5Stack Unit GPS v1.1, ATGM336H at AT6668, DC 5 V at 31.64 mA, no backup
  battery or V_BCKP listed:
  https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1
- M5Unified `Power_Class::setExtOutput`, used for 5 V rail cycling:
  https://raw.githubusercontent.com/m5stack/M5Unified/master/src/utility/Power_Class.hpp
- CASIC protocol specification, source for `$PCAS12`. The link resolves but its
  text layer could not be extracted for automated verification. Experiment B
  provides the hardware verification for the tested AT6668 unit:
  http://www.espruino.com/files/CASIC_en.pdf
- Quectel L76K GNSS protocol specification v1.1, AT6558 based, documents PCAS02,
  PCAS03, PCAS04 and PCAS10 but not PCAS12:
  https://www.waveshare.net/w/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf
