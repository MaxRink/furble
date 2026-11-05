# Lowest-power GPS profile

Status: recommendation and library audit. Evidence labels follow
`docs/power/README.md`. No bench current measurement was made for this file.
Every number is datasheet, vendor, modeled, inferred, or unknown.

## 1. Receiver and code inventory

| Element | What the tree uses | Evidence |
| --- | --- | --- |
| Module | M5Stack Unit GPS v1.1, ATGM336H module with an AT6668-class receiver | `official-semantic` vendor page, `public-source` |
| Link | One UART, 8N1, XTAL clock source so DFS does not shift the baud | `code` `src/FurbleGPS.cpp` |
| Baud | Setting `GPS_BAUD`: Auto, 9600, or 115200. Auto tries 115200, 9600, 38400, 57600, 19200, 4800, 1200 ms per step, two checksum-passing sentences to lock | `code` `lib/furble/protocol/GpsCasic.h` |
| Protocol | CASIC `BA CE` binary plus NMEA `$PCAS` command mode | `public-source` CASIC protocol and Quectel L76K v1.1 |
| Sentences | `PCAS03` selects the set. The tree already writes the minimal `1,0,0,0,1,0,0,0` (RMC and GGA), a mid set `1,0,1,1,1,0,0,0`, and the full set | `code` `src/FurbleGPS.cpp` |
| Rate | `GPS_RATE` maps to 0, 1000, 500, 200, 100 ms intervals | `code` `include/FurbleGPS.h` `RATE_MS` |
| Constellation | `GPS_CONSTEL` through `PCAS04` / `CFG-NAVX` | `code`, `public-source` L76K |
| Power policy | `GPS_POWER`: always on, standby (`PCAS12`), rail cycle | `code` `src/FurbleGPS.cpp` |
| Duty | `GPS_DUTY`: 0, 5, 10, 15 s | `code` `include/FurbleGPS.h` `DUTY_SECONDS` |
| Assist | `GPS_ASSIST` for `AID-INI`; `GPS_PLATFORM` chooses the CASIC configuration path | `code` |
| Motion | `GPS_MOTION` uses the IMU to back the receiver off when stationary | `code`, `plans/18-gps-motion.md` |
| Parser | TinyGPSPlus fork `gkoh/TinyGPSPlus#92ac8c21b8179201c2c8c534e46af3320b76d3e7`; snapshot under the GPS mutex | `code`, pinned library in the datasheet register |
| PPS | Not read. No GPIO ISR and no timestamp capture | `code` |
| I2C, SPI | Not used by the GPS path. The IMU shares I2C but the GPS does not | `code` |

## 2. Recommended profile

This is the lowest-power configuration the current tree can reach without new
hardware or a protocol guess. The first four rows are code and schedule changes;
the rest are settings.

| Rank | Choice | Why | Evidence | Test |
| --- | --- | --- | --- | --- |
| 1 | One fix per second (1000 ms) unless the GPS data page is open | Geotagging needs position and UTC once per second at most. 5 to 10 Hz buys nothing for a camera burst and multiplies UART and parser work | `inferred` from `RATE_MS` and the camera update path | Host rate vector; simulator `gps.rate` |
| 2 | Minimal sentence set `PCAS03,1,0,0,0,1,0,0,0` while no diagnostics page is open; add GSV/GSA only while the satellite page is visible | Fewer UART bytes at 9600 means less line time, and GSV is the largest sentence group | `inferred`, `code` already writes all three sets | Simulator sentence counter; host command test |
| 3 | Auto once, then persist the detected rate. Do not force 9600: the Unit GPS v1.1 ships at 115200, and `GPS::enable()` changes only the host UART, not the receiver | Skips the 7.2 s autobaud ladder on later boots without losing reception on a 115200 unit. No receiver current change; saves boot time and avoids a hold on `NO_LIGHT_SLEEP` while probing | `inferred` from `GpsCasic::Autobaud`; vendor rate for the module | Host ladder test; sim boot trace |
| 4 | No-receiver path must sleep until the retry, never hold `NO_LIGHT_SLEEP` | A silent fixed-baud receiver used to hold the power lock forever; the 30 s acquisition deadline now bounds it | `code` G-03, `inferred` up to 37 mA in the fault case | Sim: enable GPS with no peer, assert the lock is released and the task sleeps |
| 5 | Standby after a valid fix, not rail cycle | Standby keeps the receiver's ephemeris and RTC; a rail cut costs the measured 107.7 s re-fix | `inferred` G-04, G-16 arithmetic on the published re-fix time | Bench G-04, G-06 |
| 6 | Choose `GPS_DUTY` from the G-06 tradeoff, not a fixed 5 s rejection | `GPS_DUTY` sets the sleep interval after an accepted fix, not an acquisition window, so the 107.7 s cold-start time does not show that a 5 s duty prevents fixes. A longer sleep lowers receiver current but leaves the position older; the right value comes from the measured fix-freshness and current comparison | `code` `DUTY_SECONDS`; `inferred` from the published cold re-fix | Bench G-06 |
| 7 | GPS plus BDS only | CASIC receivers track BDS natively. Adding GLONASS costs a second correlator set and is not documented as free | `inferred`; G-09 is the deciding measurement | Bench G-09 |
| 8 | SBAS and QZSS off | No documented control and no evidence of a saving | `rejected` G-18 | none |
| 9 | UART wake disabled | Wake-on-UART loses the triggering byte and everything before it, so NMEA cannot survive | `rejected` G-19 | none |
| 10 | Keep TinyGPSPlus | A custom parser is rejected for correctness risk under 0.1 mA | `rejected` G-15 | none |
| 11 | Motion backoff gated on a measured resting magnitude | False backoff loses fixes; false run costs current | `inferred` G-05, I-07 | Bench G-05, I-07 |
| 12 | Deep sleep between intervalometer shots is out of scope | It conflicts with the camera session and is a product change | `out of scope` plan 19 | none |

### 2.1 What the profile does not claim

- No current number is local. The receiver figures in this directory are
  datasheet or vendor values: below 25 mA typical at 3.3 V for the ATGM336H
  family, 31.64 mA at the 5 V Grove input for the Unit GPS v1.1, and below
  10 uA standby for the chip. The module rails and antenna feed are not
  separately measured.
- Fix-aware backoff and 10 to 15 s duty are the largest plausible savings, and
  both depend on the in-circuit standby current and wake-to-fix time. Those are
  bench measurements G-04 and G-06.
- The antenna type is not recorded. An active antenna would add 2.4 to 3.6 mA
  at the feed; the tree has no control and no record of whether the unit uses
  one.

## 3. Library audit

| Library or path | What it does | Cost or risk | Recommendation |
| --- | --- | --- | --- |
| TinyGPSPlus `#92ac8c21b8179201c2c8c534e46af3320b76d3e7` | Parses NMEA one byte at a time under the GPS mutex; about 30 accessors per snapshot | Negligible at 1 Hz; the CPU cost is not the lever, receiver on-time is | Keep. No custom parser |
| `FurbleGPS.cpp` task | 10 Hz wake even when idle; per-second log lines; `NO_LIGHT_SLEEP` held while a receiver is expected | G-01, G-02, G-03, G-13. The fault path is the largest | Deadline-driven wait; stop the timer when GPS is off; release the lock for an absent receiver; rate-limit logs |
| `GpsCasic` helpers | Autobaud, PCAS framing, CASIC checksum, `CFG-NAVX`, `MON-HW` | Correct and host-tested; the checksum id-first quirk is documented | Keep |
| I2C | Not used by GPS | None | No change |
| SPI | Not used by GPS | None | No change |
| UART | One port, XTAL clock, pattern-detect interrupt on newline | The pattern interrupt is cheap; the baud ladder is the only avoidable boot cost | Persist the detected baud |
| GPIO | No PPS and no reset line; no dedicated GPS-only power-enable GPIO | The module rail is switched through the shared Port A output: `setRailPower()` calls `M5.Power.setExtOutput(..., m5::ext_PA)`, the same control the absent-receiver and rail-cycle paths use | Keep the shared Port A control; a GPS-only switch would need a board change |
| IMU | Feeds `GPS_MOTION` and the level page | I2C polling and the motion engine are audited in the GPS/IMU lane | Keep behind the existing setting |

## 4. Bench-gated remainder

| Item | What it decides | Measurement |
| --- | --- | --- |
| In-circuit standby current and wake-to-fix | Whether standby backoff is a real saving and what duty/window is safe | G-04, G-06 |
| Resting magnitude and detector accuracy | Whether motion backoff can run without losing fixes | G-05, I-07 |
| Multi-constellation current | Whether GPS plus BDS plus GLONASS is worth the extra correlators | G-09 |
| Fix-interval response | Whether the receiver current changes at all with `PCAS02` | G-10 |
| `PCAS03` acceptance and line time | Whether the minimal set is honored on this unit | G-12 |
| Assist and ephemeris replay | Whether `AID-INI` shortens the 107.7 s re-fix | G-07, G-08 |

These rows are bench-blocked. The profile above is the configuration to test,
not a measured result.
