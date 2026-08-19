# PR14 - GPS receiver configuration with $PCAS

## Goal

Give furble a TX path to the GPS receiver so it can set the fix rate, prune the
NMEA sentences it does not use, and pick the constellation. This cuts UART
traffic and CPU wakeups, and it is the prerequisite for the burst windowed sleep
in PR15. Defaults send nothing, so behaviour is unchanged unless the user opts
in.

## Scope

In scope:

- NMEA checksum builder and a `sendCommand` helper on `Furble::GPS`.
- `$PCAS02` fix interval, `$PCAS03` sentence rates, `$PCAS04` constellation.
- GPS settings become a nested submenu instead of one flat page.
- Raw NMEA and satellite debug page under Diagnostics.

Out of scope:

- `$PCAS12` standby and 5 V rail cycling (PR15).
- `$PCAS00` save to flash. Never sent by default, see notes.
- Motion adaptive rates (PR18).
- Module GPS v2.1 support, still unsupported (`src/FurbleGPS.cpp:23`).

## Files to change

| File | Anchor | Change |
|---|---|---|
| `include/FurbleGPS.h` | `:35-38` constants, `:40-43` private methods, `:45` `m_UART = UART_NUM_2` | Add `sendCommand`, checksum helper, config apply |
| `src/FurbleGPS.cpp` | `:19-58` `getInstance` UART setup | Unchanged, TX pin is already configured at `:45` |
| `src/FurbleGPS.cpp` | `:111-124` `enable` | Apply the configuration after baud set and flush |
| `src/FurbleGPS.cpp` | `:136-143` `reloadSetting` | Re-apply when settings change |
| `src/FurbleGPS.cpp` | `:195-206` `serviceSerial` | Optional tap into a raw sentence ring buffer |
| `include/FurbleSettings.h` | `:16-29` enum, `:101-148` `storage_type` | Add `GPS_RATE`, `GPS_NMEA`, `GPS_CONSTEL` |
| `src/FurbleSettings.cpp` | `:11-24` table, `:186-227` default switch | Add rows and defaults |
| `include/FurbleUI.h` | `:184-185` `settings->gps` strings | Add page name strings |
| `src/FurbleUI.cpp` | `:53-76` `UI::m_Menu` grid map | Add the new GPS subpages |
| `src/FurbleUI.cpp` | `:1514-1604` `addGPSMenu` | Restructure into a submenu |

## New settings

| Enum | NVS key | Namespace | Type | Values | Default |
|---|---|---|---|---|---|
| `GPS_RATE` | `gps_rate` (8 chars) | `FURBLE_STR` | `uint8_t` | 0 do not send, 1 = 1000 ms, 2 = 500 ms, 3 = 200 ms, 4 = 100 ms | 0 |
| `GPS_NMEA` | `gps_nmea` (8 chars) | `FURBLE_STR` | `bool` | false do not send, true prune to RMC and GGA | false |
| `GPS_CONSTEL` | `gps_constel` (11 chars) | `FURBLE_STR` | `uint8_t` | 0 do not send, otherwise the raw `$PCAS04` value 1 to 7 | 0 |

Every default is "send nothing". A fresh NVS boot produces the exact same UART
traffic as master, which is receive only.

## Menu placement

Settings -> GPS becomes a submenu:

```
Settings
└─ GPS      Enable · Baud · Update rate · Sentences · Constellation · GPS Data
```

`addGPSMenu` at `src/FurbleUI.cpp:1514-1604` currently builds one flat page with
the Enable switch (`:1517`), the 115200 baud switch (`:1521-1546`) and the
GPS Data subpage (`:1548-1603`). Keep Enable and Baud in place, add three roller
subpages, and keep GPS Data where it is. Register the new page names in
`UI::m_Menu` at `src/FurbleUI.cpp:53-76` with `{col,row}` positions so the Core
grid layout still works.

The Enable switch already hides the dependent widgets when GPS is off
(`src/FurbleUI.cpp:1550-1553`). Extend that list with the new subpage buttons.

Raw NMEA and satellite page goes under Settings -> Diagnostics, created by PR05.

## Implementation notes

- Framing. A `$PCAS` command is plain NMEA: `$` + payload + `*` + two uppercase
  hex digits + CRLF. The checksum is the XOR of every character between `$` and
  `*`, both excluded. Example: `$PCAS02,1000*2E`.
- Write path. The TX pin is already assigned at `src/FurbleGPS.cpp:45` via
  `uart_set_pin` with `M5.getPin(m5::port_a_pin2)`. Nothing in furble has used
  it so far. Use `uart_write_bytes` on `m_UART` (`include/FurbleGPS.h:45`,
  `UART_NUM_2`), then `uart_wait_tx_done` with a short timeout.
- Ordering in `enable()` (`src/FurbleGPS.cpp:111-124`): set the baud rate, flush,
  wait for the receiver to settle, then send the configuration. Sending before
  the flush risks the command landing at the wrong baud.
- Commands used:
  - `$PCAS02,<ms>` sets the fix interval. Officially 100 to 1000 ms. Below
    500 ms the receiver cannot keep up unless unused sentences are disabled
    first, so send `$PCAS03` before `$PCAS02` when both are enabled.
  - `$PCAS03,<GGA>,<GLL>,<GSA>,<GSV>,<RMC>,<VTG>,<ZDA>,<ANT>,...` sets a per
    sentence output rate in fixes. 0 disables. furble needs GGA for altitude,
    satellite count and fix quality, and RMC for date, time and position, which
    is what `GPS::update` consumes at `src/FurbleGPS.cpp:166-184`. So the pruned
    form is `$PCAS03,1,0,0,0,1,0,0,0`.
  - `$PCAS04,<n>` selects the constellation. 1 GPS, 2 BDS, 3 GPS and BDS,
    4 GLONASS, 5 GPS and GLONASS, 6 BDS and GLONASS, 7 GPS, BDS and GLONASS.
  - `$PCAS10,<n>` restarts the receiver, 0 hot, 1 warm, 2 cold. Not wired to a
    setting. Expose it as a button on the diagnostics page only.
  - `$PCAS01,<n>` sets the baud rate, index 5 is 115200. Not used by this PR.
    furble changes its own UART baud and expects the module default of 115200
    for Unit GPS v1.1. Confirm the index table on device before any future use.
- `$PCAS00` saves the configuration to the receiver flash. Do not send it. It
  makes furble's settings outlive furble, survives a factory reset of the
  device, and wears the module flash. If it is ever added it belongs behind an
  explicit confirmation dialog.
- No acknowledgement. The receiver does not reliably answer `$PCAS` commands.
  Verification is observational: watch the sentence mix and the arrival period
  on the raw NMEA page.
- Re-apply on change. `reloadSetting` (`src/FurbleGPS.cpp:136-143`) already runs
  on every settings change through the UI callbacks. Route the new settings
  through the same call so the receiver is reconfigured immediately.
- Power on ordering. `enable()` calls `M5.Power.setExtOutput(true, m5::ext_PA)`
  at `src/FurbleGPS.cpp:118`. The receiver needs time after power up before it
  accepts commands. Add a bounded wait for the first valid sentence, then send.
  Do not block the UI task; this runs in the GPS task
  (`src/FurbleGPS.cpp:73-109`).
- Raw NMEA debug page. Add a small fixed ring buffer of the last 8 sentences,
  filled from `serviceSerial` (`src/FurbleGPS.cpp:195-206`) only while the page
  is open. Show it with satellite count, HDOP, fix age and the TinyGPS++
  counters `charsProcessed()`, `passedChecksum()` and `failedChecksum()`. Those
  counters are what PR15 needs to tune its sleep window.
- The GPS Data and Raw NMEA pages also show the current TinyGPS++ speed in km/h.
- The GPS Data page displays latitude and longitude to five decimal places.
- The `gps-speed-coords` sim end-to-end scenario asserts the rendered speed and
  five decimal place coordinates against a known fake fix (42.0 km/h, 48.11730 N,
  11.51667 E).
- Keep it board neutral. Nothing here is S3 specific.

## Dependencies

- PR05 for the Settings -> Diagnostics submenu. Soft dependency. Without it the
  raw NMEA page can hang off Settings -> GPS temporarily.
- PR14 is a hard prerequisite for PR15, which needs a known fix interval, and for
  PR18.

## Risks

- A wrong checksum is silently ignored by the receiver, so a bug looks like
  "nothing happened". Unit test the checksum builder against known good strings
  such as `$PCAS02,1000*2E` before touching hardware.
- Pruning sentences can starve TinyGPS++ of something it needs. GGA and RMC are
  enough for `GPS::update`, but confirm satellite count and altitude still
  populate on the GPS Data page.
- Some AT6668 firmware revisions ignore or partly implement `$PCAS` commands.
  Everything defaults to off, and a wrong rate is recoverable by setting the
  rate back to "do not send" and power cycling the unit.
- Higher fix rates increase UART traffic and CPU wakeups, which is the opposite
  of the project goal. The menu should describe rate as a precision and power
  tradeoff, not a speed feature.
- Documentation quality for CASIC is poor. Treat every value not confirmed on
  device as provisional.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Host side check before flashing: verify the checksum builder produces `2E` for
`PCAS02,1000` and matches the published example strings.

On device, M5StickS3 with GPS Unit v1.1 on Port A, over USB:

1. Fresh NVS boot. Confirm the GPS menu still works, that the device sends
   nothing on TX, and that fix behaviour matches master.
2. Enable GPS at 115200. Confirm a fix, then open the raw NMEA page and record
   the baseline sentence mix and period.
3. Set Sentences to pruned. Confirm only GGA and RMC arrive and that latitude,
   longitude, altitude and satellite count still update on GPS Data.
4. Set Update rate to 500 ms and then 200 ms. Confirm the arrival period changes
   and that `failedChecksum()` stays flat.
5. Set Constellation to GPS only, then back to GPS, BDS and GLONASS. Confirm the
   satellite count changes and a fix is retained.
6. Power cycle the unit. Confirm the receiver returns to its own defaults, which
   proves `$PCAS00` was never sent.
7. Connect a Fujifilm camera with GEOTAG. Confirm the geodata request path still
   gets a fresh fix within the 30 second `MAX_AGE_MS` budget
   (`include/FurbleGPS.h:38`).

Battery drain runs, unplugged, on board instrumentation only:

- 30 to 60 minutes of connected plus GPS, logging battery percent and voltage
  every 30 s. Compare default rate against pruned sentences at 1000 ms. Expect a
  small win from reduced UART and CPU work. The large win comes in PR15.

Cameras: Fujifilm only. GEOTAG is the only vendor path that consumes GPS data in
this PR. State that in the PR body.

## References

- CASIC multimode satellite navigation receiver protocol specification, the
  primary `$PCAS` source. The link resolves and serves the PDF, but its text
  layer could not be extracted for automated verification, so the values above
  are cross checked against the two sources below:
  http://www.espruino.com/files/CASIC_en.pdf
- Quectel L76K GNSS protocol specification v1.1. The L76K is AT6558 based and
  documents PCAS02, PCAS03, PCAS04 and PCAS10:
  https://www.waveshare.net/w/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf
- Espruino Bangle.js 2 technical notes, working `$PCAS00`, `$PCAS02`, `$PCAS03`
  and `$PCAS04` usage against an AT6558, including the note that rates below
  500 ms require disabling unused sentences:
  https://www.espruino.com/Bangle.js2+Technical
- M5Stack Unit GPS v1.1, ATGM336H at AT6668, UART default 115200 8N1, DC 5 V at
  31.64 mA:
  https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1
