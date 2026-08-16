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
| `src/FurbleGPS.cpp` | `:28-41` `uart_config_t`, source clock at `:35-39` | `UART_SCLK_XTAL` on S3, keep `UART_SCLK_REF_TICK` elsewhere |
| `src/FurbleGPS.cpp` | `:42-48` driver install, pattern detect on `'\n'` at `:46` | Timestamp bursts, feed the window tracker |
| `src/FurbleGPS.cpp` | `:73-109` `task` | Acquire before the window, release after the burst |
| `src/FurbleGPS.cpp` | `:111-124` `enable`, `:120-123` S3 `setSleep(false)` | Remove the blanket disable |
| `src/FurbleGPS.cpp` | `:126-133` `disable`, `:130-132` S3 `setSleep(true)` | Remove the blanket enable, release the lock |
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

- Today `enable()` calls `Platform::getInstance().setSleep(false)` on the S3
  (`src/FurbleGPS.cpp:120-123`) and `disable()` restores it
  (`src/FurbleGPS.cpp:130-132`). PR06 replaces `setSleep` with named, counted
  `esp_pm` locks. This PR converts the GPS user to a `ESP_PM_NO_LIGHT_SLEEP`
  lock and then shrinks how long it is held.
- UART RX does not work during automatic light sleep. UART wakeup exists but is
  useless here: the ESP-IDF docs state that the character which triggers wakeup,
  and any characters before it, are not received after wakeup. An NMEA sentence
  cannot survive that, so furble must be awake before the burst starts, not woken
  by it.
- Clock source. On the S3 the current config uses `UART_SCLK_DEFAULT`
  (`src/FurbleGPS.cpp:35-39`), which derives from APB and therefore moves with
  dynamic frequency scaling. Change it to `UART_SCLK_XTAL`. The ESP-IDF UART
  guide is explicit: a source clock with a fixed frequency that stays active
  during sleep is required for a correct baud rate. Leave the non S3 path on
  `UART_SCLK_REF_TICK`, which already has that property.

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
  Some AT6668 firmware ignores it. It is not listed in the L76K protocol
  contents, so treat it as unverified until Experiment B confirms it.
- 5 V rail cycling uses the existing `M5.Power.setExtOutput` calls at
  `src/FurbleGPS.cpp:118` and `:128`. This removes the module draw completely but
  loses the ephemeris if the unit has no backup supply.
- Experiment B decides the recommended policy. The M5Stack Unit GPS v1.1 page
  lists no backup battery and no V_BCKP pin. If Experiment B confirms that, every
  rail cut forces a cold or warm start, the re-fix costs more energy than the
  saved idle draw, and standby is strictly better. In that case ship rail cycling
  as an expert option with a warning, and make standby the recommended non
  default.
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

## Dependencies

- PR06 for the named pm lock API. Hard dependency.
- PR14 for a known fix interval and for the `$PCAS` send path used by `$PCAS12`.
  Hard dependency.
- Experiment B for the recommended power policy default.
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
- `$PCAS12` may be ignored by the module firmware. The device would then behave
  as always on, wasting power but working correctly. Detect it during Experiment
  B by checking for NMEA silence after the command.
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
6. Enable rail cycling with 10 second duty. Record the re-fix time. This is the
   Experiment B measurement.
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
  text layer could not be extracted for automated verification, so `$PCAS12` is
  treated as unverified until Experiment B:
  http://www.espruino.com/files/CASIC_en.pdf
- Quectel L76K GNSS protocol specification v1.1, AT6558 based, documents PCAS02,
  PCAS03, PCAS04 and PCAS10 but not PCAS12:
  https://www.waveshare.net/w/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf
