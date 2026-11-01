# Power measurement plan

Status: plan only. No bench measurement in this document has been executed. The
register is `unknown` from top to bottom until a record ID and an accepted raw
trace exist. A simulator report is not a measurement. A datasheet number is not
a local measurement.

This plan merges the seven lane plans into one bench campaign. It keeps the
canonical state, register, and threshold IDs from `docs/power/measurement.md`
and adds the lane-specific states that did not have a home there. A row is
closed only by the evidence class it names, not by editing this document.

Camera parity is out of scope. The camera rows below measure the power of a
link state. They do not assert protocol parity, feature coverage, or vendor
support. The camera parity PR stays independent.

ID warning: `P01` to `P15` in this file are measurement register rows from
`docs/power/measurement.md`. They are not the opportunity IDs `P-001` to
`P-204` in `docs/power/opportunity-matrix.md`.

## 1. What this plan decides

For each opportunity in the matrix:

1. Which electrical boundary the claim applies to.
2. Which board, firmware, display, radio, and camera state must hold.
3. Which instrument and capture window can resolve the expected change.
4. What makes the run valid or invalid.
5. What evidence promotes a modeled saving to a measured saving, and what
   evidence is still missing before a default changes.

Out of scope: camera protocol parity, RF certification, safety certification,
production calibration, and any claim about a board that is not on the bench.

## 2. Evidence vocabulary

| Label | Definition | May support | May not support |
| --- | --- | --- | --- |
| `measured` | Accepted bench trace, at least three independent runs, record ID, raw trace hash, settings dump, instrument calibration ID. | A measured claim. With M3, a `measured-local` model entry and a reviewed default change. | A claim on another board, boundary, firmware, or camera family. |
| `modeled` | Simulator report with `model_valid: true`, a matching model digest, and the same accounting mode as its baseline. | Relative comparison and the CI power gate. | An absolute current, runtime, or hardware-parity claim. |
| `inferred` | Datasheet value, published third-party measurement, or arithmetic with stated inputs. | An expected band and a design hypothesis. | A local measurement claim. |
| `unknown` | No evidence, or evidence rejected under section 13. | Nothing. | Nothing. |

Rules:

- A label changes only when the evidence changes, never when a document is
  edited.
- A model report never becomes `measured` by relabeling. It must pass the
  promotion gate in section 12.
- One run is a sample, not repeatability.
- A delta without an uncertainty is not a saving.
- Values from different boundaries are never added.
- Every claim names board, firmware commit, state ID, boundary, instrument, and
  record ID.

## 3. Electrical boundaries

| Boundary | Node | What it includes | Use |
| --- | --- | --- | --- |
| B1 | Battery positive before the PMIC | Cell current, PMIC conversion, every rail, every peripheral | Board current, runtime, model calibration. The only boundary for a battery runtime claim. |
| B2 | Grove Port A 5 V pin | GPS unit regulator and receiver | Comparison with the M5Stack 31.64 mA figure. Never substituted for B1. |
| B3 | GPS module VCC at 3.3 V | AT6668 module, TCXO, flash, antenna LNA | Comparison with the 23 to 25 mA module figures. Never substituted for B1. |
| B4 | USB or 5 V input jack | Charger, boost, USB PHY, console bridge | Charging behavior, Waveshare, debug correlation. Not a battery measurement. |
| B5 | Ethernet jack or PoE input | W5500 PHY, magnetics, optional PoE HAT | Waveshare network states. No battery meaning. |

Boundary rules:

- B1 is the only boundary for a battery runtime claim or a light-sleep floor
  claim.
- B2 and B3 are diagnostics for the GPS term. Converting either to B1 requires
  the measured conversion efficiency for that board and state, recorded with the
  run.
- B4 with USB attached charges the cell and changes the PMIC state. It is not a
  battery measurement.
- B5 is mains only.

## 4. Instruments

| Code | Role | Minimum capability | Example | Used for |
| --- | --- | --- | --- | --- |
| I1 | Primary source and measure | 1 uA to 1 A, at least 16 bit, at least 100 kSa/s; 1 MSa/s for event work or pair with I4; source mode at 3.7 V and 5 V | Keysight N6705C with N6781A, or Joulescope JS220 | All steady states, event captures, discharge support |
| I2 | Secondary logger | 1 uA to 1 A, at least 10 kSa/s, long capture | Nordic PPK2 or Otii Ace | Repeat runs, 30 minute soaks |
| I3 | DC average reference | 6.5 digits, 1 to 10 Sa/s, 4-wire resistance | Keysight 34465A or Keithley DMM6500 | Gain check, shunt value, DC cross-check |
| I4 | Transient capture | DC to 20 MHz, differential probe, 1 MSa/s or faster | Oscilloscope with a differential probe across the shunt | BLE events, display flush, wake spikes, noise diagnosis |
| I5 | Battery discharge | Coulomb counting, at least 1 Sa/s, programmable cutoff | Electronic load with coulomb counter, or I1 in source mode | Capacity and runtime |
| I6 | Thermal | +/-0.5 degC, one ambient and one board contact channel | Calibrated thermocouple logger or thermal camera | Thermal drift and enclosure checks |
| I7 | 5 V input analyzer | 1 mA resolution, at least 1 kSa/s, USB or jack inline | Bench SMU or inline USB analyzer with logging | Waveshare, charging reference |
| I8 | BLE sniffer | Captures connection events, interval, PHY, payload length, retries | Nordic Sniffer, Ellisys, or Teledyne front-line | Link-parameter and airtime evidence. State evidence, not current. |
| I9 | State observer | Photo, video, or camera indicator | Phone camera or bench camera, phone radio off | Screen state, camera link, GPS fix. State evidence, not current. |

Instrument rules:

- I1 carries a calibration certificate no older than 12 months. Record the
  certificate ID and date.
- I3 is the traceable gain reference. I1 and I2 must agree on the same steady
  load before either is used for a claim.
- I4 is not an average-current instrument. Use it for event shape and noise, then
  integrate with I1 or I2.
- I8 and I9 are state evidence. A photo of the screen is not a current
  measurement.
- The BLE lane requires I1, I2, I4, I8, and I9. The GPS lane requires a second
  channel with a series shunt on Grove Port A 5 V (B2) to separate the unit.
- The display lane requires a photodiode on the panel for flicker and a scope or
  logic analyzer on the S3 backlight GPIO38, SPI clock and MOSI, and, where
  accessible, the panel SLPIN and SLPOUT commands.
- The board lane requires the analyzer to resolve the off state. A 14 uA claim
  from a 1 mA range is not a measurement. Record the instrument offset and
  resolution with any off-state or deep-sleep claim.

## 5. Bench setup and exclusions

### 5.1 Shunt and probe

- Preferred setup: a 4-terminal, 0.1 percent shunt in the battery positive lead,
  before the PMIC and before any charger node. Solder sense wires at the shunt
  terminals.
- Active states: 0.01 ohm. Sleep states: 10 ohm. Never both in series.
- Burden budget: below 20 mV active, below 100 mV sleep. Compute `V = I * R`,
  record it, and rerun on a different range if exceeded. Do not correct a
  clipped trace.
- Twist the sense pair, keep the loop area small, and keep the probe ground at
  the shunt, not at the USB connector.
- Fuse the battery lead at the cell rating. Keep the cell protection board in
  circuit.
- For event work, use a differential probe across the shunt, not a clamp probe.
  Zero it with the load off, DC coupling, 20 MHz bandwidth limit, and check a
  10 mA stable load against I3 to 1 percent.

### 5.2 USB, charging, and ground loops

- Remove USB for every B1 run. Tape the connector or use a data-only cable with
  the power pins removed for console correlation, never both at once.
- The StickS3 USB Serial/JTAG console needs VBUS and holds `NO_LIGHT_SLEEP` while
  a host is attached, so it cannot be used for a B1 run.
- Confirm charging is false. AXP192 boards report it. The S3 M5PM1 reports
  charge status but no current. The Plus2 has no charging telemetry, so the USB
  connector must be physically absent.
- Put the host laptop on battery or use a USB isolator. A grounded laptop and a
  grounded instrument create a loop through the shunt.
- The camera peer runs on its own battery. Do not power it from the DUT supply.
- Keep the phone and the AP at least 2 m away, or off, except during the Wi-Fi
  rows where the AP is the state under test. Record which radios were active.

### 5.3 Charge disable per PMIC

For a battery-boundary run, disable charging at the PMIC and record the register
before and after. Re-enable it after the run.

| Board | Action |
| --- | --- |
| AXP192 boards (StickC, Plus, Core2, Tough) | Clear the charge enable bit in `0x33` |
| IP5306 (Core) | Clear the charge enable bit in `0x00` |
| M5PM1 (StickS3) | Clear `PWR_CFG` bit 0 |
| Plus2 | No PMIC. USB must be physically absent. |

### 5.4 Firmware and logging constraints

The console distorts power:

- The ESP32 console holds `ESP_PM_APB_FREQ_MAX` for its lifetime and blocks
  light sleep.
- The S3 and headless consoles use USB Serial/JTAG, and an attached host holds
  `NO_LIGHT_SLEEP`.
- Debug builds add `CONFIG_PM_PROFILING`, FreeRTOS run-time stats, and the LVGL
  sysmon and perf monitor.

Therefore split the work:

1. Current runs on a release build with a fixed, scripted state sequence and no
   USB attached where the state allows it.
2. State and lock evidence on a debug build, reported separately and never mixed
   into the current table.
3. If a release run needs state markers, use a bench-only build that writes
   markers to RTC memory or NVS and is read after the run, or a spare GPIO
   toggled at each transition. Do not use the USB console for a light-sleep
   current run.

### 5.5 Board availability

Only the M5StickS3 is recorded as attached in this repository. Every other board
needs hardware before a row for it can move from `inferred` to `measured-local`.
A row that names a board with no hardware stays `unknown`. Do not substitute a
simulator report or another board's number.

## 6. Calibration

Run this at the start of every campaign and again after any instrument, shunt,
cable, fixture, or PMIC-state change.

| ID | Check | Expected | Acceptance | Instrument |
| --- | --- | --- | --- | --- |
| C01 | Instrument zero, open circuit | 0 A | +/-1 uA after 10 min | I1, I3 |
| C02 | Gain, 100 ohm 0.01 percent at 3.700 V | 37.000 mA | +/-0.2 percent against I3 | I1, I3 |
| C03 | Shunt resistance, 4-wire | 0.01000 or 10.000 ohm | +/-0.1 percent | I3 |
| C04 | Burden at maximum state current | Below 20 mV active, below 100 mV sleep | Hard limit; rerun on a lower shunt | I4 |
| C05 | Noise floor, 1 Mohm at 3.700 V (3.7 uA), 10 min | Below 2 uA RMS | At most 2 uA RMS, else find the source | I1 |
| C06 | Cross-instrument, same steady load | I1 and I2 agree | Within 2 percent above 5 mA; within 5 percent below 1 mA | I1, I2 |
| C07 | Onboard PMIC current versus I1 | AXP192 boards | Within 10 percent after correction; otherwise record the correction and do not use the PMIC for a claim | I1, `power log` |
| C08 | Timebase, 10 min marker | 600.0 s | Drift below 0.1 percent | I1, I4 |
| C09 | Temperature, ice point and room | 0.0 and 25.0 degC | +/-0.5 degC | I6 |
| C10 | Model digest and accounting | Report digest equals the YAML SHA-256; `model_valid` true; accounting mode matches the baseline | Exact match | `compare.py` |

Calibration procedure:

1. Power the instrument for 30 minutes. Zero it with the load open. Record the
   offset.
2. Measure the shunt with I3 in 4-wire mode. A 0.01 ohm shunt that reads 0.01012
   ohm is a 1.2 percent error and must be corrected or replaced.
3. Connect a 100 ohm, 0.01 percent load to a stable 3.700 V source. I3 and I1
   must agree within 0.2 percent.
4. Apply the highest expected state current on the active shunt, measure the
   shunt voltage with I4, and confirm the burden. Repeat for the sleep shunt.
5. Run a 10 minute capture on a 1 Mohm load at 3.700 V. Record RMS and
   peak-to-peak. Above 2 uA RMS, find the source before continuing.
6. Run the same steady load on I1 and I2. Above a 2 percent disagreement at or
   above 5 mA, stop and fix the setup.
7. Place the ambient sensor away from the DUT and the instrument exhaust. Place
   the board sensor on the PMIC or the panel edge, not on the cell.
8. Check the timebase with a 1 Hz marker for 10 minutes.
9. Soak the DUT for 20 minutes in the state before the first capture.

Model pre-flight:

```sh
shasum -a 256 tools/power-model/board-currents.yaml
```

Run one scenario, then check `estimate_kind`, `model_source`, `model_digest`,
`model_valid`, and the accounting mode. The digest must equal `sha256:` plus the
YAML SHA-256. The accounting mode must match the baseline, or `compare.py`
returns exit 2. Do not mix a legacy report with a synthetic-accounting baseline.

## 7. Board states and baseline settings

### 7.1 Baseline settings block

Apply with a `-debug` build, verify with `settings list`, then flash the release
build without erasing NVS. Do not run `erase_flash` between the two steps.

```text
settings set brightness 128
settings set inactivity 0
settings set display_off 0
settings set gps off
settings set gps_power 0
settings set gps_duty 0
settings set cpu_freq 160
settings set tx_power 0
settings set tx_adaptive off
settings set ir off
settings set fb_output off
settings set scan_mode 0
settings set scan_timeout 0
settings set recon_backoff off
settings set sleep_conn off
settings set conn_saver off
settings set batt_saver off
settings set auto_off 0
settings set low_batt 0
settings set companion off
settings set wifi off
settings set ntp off
settings set mqtt off
settings set sd_gpx off
settings set imu off
settings set imu_wake 0
settings set imu_trigger off
```

If a build rejects a setting, record the rejection in the session notes instead
of silently skipping it.

### 7.2 Release board matrix

| Board | Build env | MCU | PMIC | Panel | Cell capacity in firmware | Battery current sense | Sleep reachable | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| m5stick-c | `m5stick-c` | ESP32 | AXP192 | ST7735S 80x160 | 95 mAh | Yes, AXP192 | Light sleep, modem sleep | No hardware attached |
| m5stick-c-plus | `m5stick-c-plus` | ESP32 | AXP192 | ST7789V 135x240 | 120 mAh | Yes, AXP192 | Light sleep, modem sleep | No hardware attached |
| m5stick-c-plus2 | `m5stick-c-plus` | ESP32 | None | ST7789V 135x240 | 200 mAh | No | Light sleep | No hardware attached |
| m5stack-core | `m5stack-core` | ESP32 | IP5306 | ILI9342C 320x240 | 0, unknown | No | No steady state below 45 mA | No hardware attached |
| m5stack-core2 | `m5stack-core2` | ESP32 | AXP192 | ILI9342C 320x240 | 390 mAh | Yes, AXP192 | Light sleep, modem sleep | No hardware attached |
| m5stick-s3 | `m5stick-s3` | ESP32-S3 | M5PM1 | ST7789 135x240 | 250 mAh | No | Light sleep with `sleep_conn`; no deep sleep | Attached |
| waveshare-s3-eth | `waveshare-s3-eth` | ESP32-S3 | None | None | Mains | No | Tickless idle off; DFS on | No hardware attached |

The cell capacities are firmware constants used for the runtime estimate, not
measured cell capacity. A discharge test measures the actual cell.

Board abbreviations: SC = m5stick-c, SP = m5stick-c-plus, S2 =
m5stick-c-plus2, CO = m5stack-core, C2 = m5stack-core2, S3 = m5stick-s3, WAV =
waveshare-s3-eth.

### 7.3 Board-state coverage

`R` required, `C` conditional on hardware or build, `NA` not applicable.

| State | SC | SP | S2 | CO | C2 | S3 | WAV |
| --- | --- | --- | --- | --- | --- | --- | --- |
| S01 boot | R | R | R | R | R | R | R |
| S02 menu 128 | R | R | R | R | R | R | NA |
| S03 menu 240, CPU 80 | R | R | R | R | R | R | NA |
| S04 dim | R | R | R | R | R | R | NA |
| S05 screen off | R | R | R | R | R | R | NA |
| S06 scan | R | R | R | R | R | R | C |
| S07 handshake | R | R | R | R | R | R | C |
| S08 connected fast, screen on | R | R | R | R | R | R | C |
| S09 connected saver, screen on | R | R | R | R | R | R | C |
| S10 connected, screen off | R | R | R | R | R | R | NA |
| S11 connected, screen off, sleep on | NA | NA | NA | NA | NA | R | NA |
| S12 shutter | R | R | R | R | R | R | C |
| S13 intervalometer | R | R | R | R | R | R | C |
| S14 IMU gesture | C | C | C | C | C | C | NA |
| S15 multi-connect | C | C | C | C | C | C | C |
| S16 GPS off | R | R | C | R | R | R | NA |
| S17 GPS acquiring | R | R | C | R | R | R | NA |
| S18 GPS tracking | R | R | C | R | R | R | NA |
| S19 GPS standby | R | R | C | R | R | R | NA |
| S20 GPS rail cycle | R | R | C | R | R | R | NA |
| S21 GPS degraded | C | C | C | C | C | R | NA |
| S22 GPS fix hold | C | C | C | C | C | R | NA |
| S23 light sleep, disconnected | R | R | R | C | R | R | NA |
| S24 power off | R | R | NA | R | R | R | NA |
| S25 Core cutoff | NA | NA | NA | R | NA | NA | NA |
| S26 deep sleep | NA | NA | NA | NA | NA | NA | NA |
| S27 Wi-Fi disabled | C | C | C | C | C | R | C |
| S28 Wi-Fi connected | C | C | C | C | C | R | C |
| S29 NTP sync | C | C | C | C | C | C | C |
| S30 Wi-Fi reconnect | C | C | C | C | C | R | NA |
| S31 Ethernet down | NA | NA | NA | NA | NA | NA | R |
| S32 Ethernet up | NA | NA | NA | NA | NA | NA | R |
| S33 MQTT idle | NA | NA | NA | NA | C | C | R |
| S34 MQTT publish | NA | NA | NA | NA | C | C | R |
| S35 companion BLE | C | C | C | C | C | R | C |
| S36 camera power off | R | R | R | R | R | R | C |
| S37 charging | R | R | R | R | R | R | NA |

`C` for Wi-Fi means the build must be checked with `settings list` first. A
release image that rejects the setting is not a Wi-Fi measurement.

### 7.4 Canonical state definitions

These are the entry conditions for the state IDs used in section 9. A run that
does not match the entry condition is not that state, even if the current looks
plausible.

| ID | State | Entry condition | Minimum window |
| --- | --- | --- | --- |
| S01 | Boot to idle | Cold boot, no USB, splash complete, main page visible | 30 s |
| S02 | Menu idle, brightness 128 | Baseline block, main page, no camera, no GPS, no Wi-Fi | 300 s |
| S03 | Menu idle, brightness 240, CPU 80 | Baseline block plus brightness 240 and CPU 80 | 300 s |
| S04 | Screen dim | Baseline block, `inactivity 1`, `display_off 0`, 30 s idle | 300 s |
| S05 | Screen off, disconnected | Baseline block, `inactivity 1`, `display_off 1`, 30 s plus dwell | 300 s |
| S06 | Scan Full, Balanced, Low | Baseline block, `scan_mode 0/1/2`, Scan page open | 60 s per mode |
| S07 | Connect handshake | Saved camera, camera on, connect from cold boot | 1 event |
| S08 | Connected idle, fast, screen on | Camera connected, 60 s quiet, `conn_saver off` | 600 s |
| S09 | Connected idle, saver, screen on | Camera connected, `conn_saver on`, wait 15 s | 600 s |
| S10 | Connected, screen off, sleep off | Camera connected, `sleep_conn off`, screen off | 600 s |
| S11 | Connected, screen off, sleep on | S3 only, `sleep_conn on` before connect, screen off | 600 s |
| S12 | Shutter single | S10 or S11 plus one shutter press | 10 s per press |
| S13 | Intervalometer, 5 frames | Connected, interval 2 s, count 5, shutter 30 ms | 15 s |
| S14 | IMU gesture poll, screen off | Screen off, `imu on`, `imu_wake 3`, `imu_trigger on` | 300 s |
| S15 | Multi-connect, 2 cameras, screen off | Two saved cameras, both connected | 600 s |
| S16 | GPS off | Baseline block, `gps off` | 300 s |
| S17 | GPS acquiring, cold | `gps on`, no backup supply, first fix not yet | 180 s or until fix |
| S18 | GPS tracking | Fix acquired, 30 min settle, antenna at the window | 1800 s |
| S19 | GPS standby 5, 10, 15 s | `gps_power 1`, `gps_duty 5/10/15` | 1800 s per duty |
| S20 | GPS rail cycle | `gps_power 2`, duty 15, warning accepted | 1800 s or 5 cycles |
| S21 | GPS degraded | Block sky view or remove the antenna | 900 s |
| S22 | GPS fix hold | `gps_hold 10`, fix acquired, then lose the receiver | 600 s |
| S23 | Disconnected light sleep | S05, no camera, no GPS, 300 s settle | 1800 s |
| S24 | Power off | Power menu off or the documented long press | 600 s |
| S25 | Core IP5306 cutoff | Core, any state below 45 mA, 32 s | 120 s |
| S26 | Deep sleep | Not implemented; no `esp_deep_sleep` call | Not measurable |
| S27 | Wi-Fi disabled | `wifi off`, no driver | 300 s |
| S28 | Wi-Fi connected idle | SSID and PSK set, IP present, NTP off | 600 s |
| S29 | Wi-Fi NTP sync | S28 plus `ntp on`, `ntp sync` | 30 s per sync |
| S30 | Wi-Fi reconnect | S28, AP power cycled | 300 s |
| S31 | Ethernet link down | Waveshare, cable removed | 300 s |
| S32 | Ethernet link up idle | Waveshare, cable present, IP present, no MQTT | 600 s |
| S33 | MQTT connected idle | Broker reachable, `mqtt connect` | 600 s |
| S34 | MQTT publish | S33 plus one discovery or status publish | 30 s per event |
| S35 | Companion BLE idle | `companion on`, phone connected, no camera | 600 s |
| S36 | Camera powered off while linked | Connected camera, then camera power off | 60 s |
| S37 | Charging, reference only | USB attached, charging true where reported | 300 s |

S37 is a reference state only. It is never used for a B1 battery claim.

## 8. Camera states

The camera family matrix is power evidence, not a support claim. Only Fujifilm
has hardware test coverage today. The other families are code-reviewed and
FauxNY-tested. A family without hardware on the bench stays `unknown`; do not
substitute FauxNY, a code review, or another vendor's camera.

The S3 is the radio reference because it is the only board with the GPS power
lock and the only board where the UI offers connected light sleep. StickC Plus
is the AXP192 cross-check for Fujifilm Basic. Expand a family to other boards
only after an S3 anomaly.

| Code | Family | Class | Hardware on bench | Shutter shape | Geotag | Link-loss signal |
| --- | --- | --- | --- | --- | --- | --- |
| FB | Fujifilm Basic | `FujifilmBasic` | Yes, X100VI | Two write-with-response operations | 10 s sync interval | Supervision timeout |
| FS | Fujifilm Secure | `FujifilmSecure` | Yes, X100VI | Two write-with-response operations | 10 s sync interval written at setup | Supervision timeout |
| CS | Canon EOS Smart | `CanonEOSSmart` | No | Write sequence | Yes | Supervision timeout |
| CR | Canon EOS Remote | `CanonEOSRemote` | No | Write sequence | No | Supervision timeout |
| NK | Nikon | `Nikon` | No | Write sequence | No in remote mode | Supervision timeout |
| SO | Sony | `Sony` | No | Write sequence | Yes, when enabled | Supervision timeout |
| RI | Ricoh | `Ricoh` | No | Single write, capture with autofocus | Yes | Announced power state |
| LX | Panasonic Lumix | `Lumix` | No | Write sequence | 16 byte payload | Supervision timeout |
| DJ | DJI Osmo | `DJIOsmo` | No | Write sequence, record toggle | No | Supervision timeout |
| FX | FauxNY | `FauxNY` | Simulator only | Simulated | Simulated | Not a bench camera |

Camera state suffixes:

| Suffix | State | Expected band | Acceptance | Instrument |
| --- | --- | --- | --- | --- |
| 01 | Cold connect handshake | 2 to 30 s, 0.5 to 5 J; Secure 5 to 40 s, 1 to 8 J | T-state and T-event | I1, I8 |
| 02 | Connected idle, screen off, fast then saver | Fast 30 to 45 mA; saver 27 to 44 mA | T-mid | I1 |
| 03 | Shutter single, screen off | 0.2 to 2 mC per press | T-event | I1, I8 |
| 04 | Geotag sync, screen off | 0.02 to 0.2 mC per write; under 0.5 mA average | T-event | I1, I8 |
| 05 | Camera power off while linked | Disconnect in 5 to 10 s; charge under 2x idle | T-state | I1, I8 |

The 30 to 45 mA fast band is the S3 screen-off connected state with
`sleep_conn` off. It is not family-specific. The family claim is the difference
between families and the charge per event, not the absolute floor.

Nikon rows use remote mode. Smart mode is not a valid bench state because it
does not pair. Focus is a sub-case of the shutter event and is measured only
where a family uses a distinct focus write with a measurable charge. Reconnect
is the handshake row plus the BLE reconnect backoff and the link-loss row.
Secure pairing is part of the handshake row, not a separate state.

## 9. Scenario matrix

Each row names the state, the board, the settings, the instrument, the minimum
window, and what it decides. Windows assume a 60 s settle after reaching the
state unless stated otherwise.

### Phase 0: bench qualification

Rows C01 to C10. No DUT state is measured until all calibration rows pass. A
failed calibration row blocks the states that depend on it.

### Phase 1: board floors, display, and UI

| ID | State | Board | Key settings | Instrument | Window | Decides |
| --- | --- | --- | --- | --- | --- | --- |
| P01 | Menu idle, brightness 128, CPU 160 | Battery boards | Baseline block | I1 | 300 s | Screen-on floor at the product default |
| P02 | Menu idle, brightness 240, CPU 80 | Battery boards | Baseline block plus brightness and CPU | I1 | 300 s | Model parity point; compare with P01 only in the same column |
| P03 | Screen dim, minimum brightness, CPU 160 | Battery boards | `inactivity 1`, `display_off 0` | I1 | 300 s | Dim floor and the model's 32/255 assumption |
| P04 | Screen off, disconnected | Battery boards | `inactivity 1`, `display_off 1`, 30 s plus dwell | I1 | 300 s | State entry and the screen-off floor; if above 20 mA, record a state failure |
| P05 | Disconnected light sleep | Battery boards | S05 plus 300 s settle | I1 | 1800 s | Floor stability from minute 5 to minute 30 |
| P06 | Connected idle, fast, screen on | Battery boards | Camera connected, 60 s quiet, `conn_saver off` | I1 | 600 s | Fast-profile floor |
| P07 | Connected idle, saver, screen on | Battery boards | `conn_saver on`, wait 15 s | I1 | 600 s | Saver floor and interval effect |
| P08 | Connected, screen off, sleep off | Battery boards | `sleep_conn off`, screen off | I1 | 600 s | The 43.6 mA what-if |
| P09 | Connected, screen off, sleep on | S3 | `sleep_conn on` before connect, screen off | I1 | 600 s | The 3.3 to 4.0 mA band and the real floor |
| P10 | Power off | SC, SP, CO, C2, S3 | Power menu off or documented long press | I1 | 600 s | 27 uA and 14.02 uA vendor figures |
| P11 | CPU sweep 80/160/240 at brightness 128 | Battery boards | Baseline block plus CPU | I1 | 300 s per point | Frequency slope; each step at least 5 percent above the previous |
| P12 | IMU gesture poll, screen off | Battery boards | `imu on`, `imu_wake 3`, `imu_trigger on` | I1 | 300 s | Gesture poll floor |
| P13 | Boot to idle | Battery boards | Cold boot, no USB, splash complete | I1 | 30 s | Boot transient and PMIC cutoff check |
| P14 | Scan Full, Balanced, Low | Battery boards | `scan_mode 0/1/2`, Scan page open | I1 | 60 s per mode | Scan duty and monotonicity |
| P15 | Multi-connect, 2 cameras, screen off | Battery boards | Two saved cameras, both connected | I1 | 600 s | Two floors; at most 2x the single-camera radio term |
| D01 to D06 | Brightness sweep, 5 points | One per board | Minimum, 64, 128, 192, 240 | I1 | 5 min per point, 2 repeats | Backlight linearity; S3 runs at CPU 160 and CPU 80 |
| E13 | Brightness sweep cross-check | S3 | Same points, fixed menu page | I1, I9 | 5 min per point | Display lane M-02; slope and the 4.4 mA per 32 counts inference |
| E14 | APB lock A/B, non-S3 | SP, C2, CO | Lock held versus released build | I1, I4 | 10 min each | P-071; SPI clock, panel, touch, at least 5 percent whole-device reduction |
| E15 | APB and LEDC A/B, S3 | S3 | Current build versus RC_FAST `KEEP_ALIVE` build | I1, I4, I9 | 10 min each plus 24 h | P-072; flicker, duty stability, light-sleep residency |
| E16 | UI loop sweep | S3 | Screen off, 5/10/20/50 ms delay | I1 | 10 min each | P-070; wake count, current, input latency |
| E17 | Panel frame rate | S3 | Fixed image, 60/40/30 Hz | I1 | 5 min each | P-075; reject below 1 mA |
| E19 | LEDC timer running versus stopped | S3 | Panel off, timer running versus stopped | I1 | 10 min each | P-019; reject below 0.05 mA |
| E20 | Sleep and wake cycles | S3, SP | 100 off/on cycles by button and by IMU | I1, I4, I9 | 100 cycles | SLPIN/SLPOUT dwell, wake latency, energy per transition |
| E21 | Settings page idle | S3 | Settings > Power or Display, no input | I1 | 10 min | P-017 scroll animation and P-015 hidden label redraw |

### Phase 2: sleep, off, and PMIC states

| ID | State | Board | Key settings | Instrument | Window | Decides |
| --- | --- | --- | --- | --- | --- | --- |
| P04, P05, P08 to P10 | See phase 1 | See phase 1 | See phase 1 | I1 | See phase 1 | Sleep and off classification |
| L01 | Core IP5306 cutoff, load below 45 mA | CO | Screen off or any sub-45 mA state | I1 | 120 s | Boost off 32 to 40 s after the load drops; post-cutoff 0.05 mA |
| L02 | Deep sleep | All | Not implemented; no `esp_deep_sleep` call | None | Not measurable | Do not invent a number |
| E18 | Touch monitor versus idle | C2, Tough | Panel off, touch idle versus monitor or hibernate mode | I1, I9 | 10 min each | P-069; current, tap wake, calibration retained |
| E22 | PMIC off, L1, L2, and light sleep | S3 | `shutdown()`, PM1 L1, PM1 L2, light sleep | I1 | 10 min each | P-097; current, wake latency, retained state, recovery after a failed wake |
| E23 | Core boost keep-on | CO | IP5306 bit 1 set versus clear | I1, I6 | 30 min each | P-068; no power cut, pack current, board temperature |
| E24 | AXP192 timed deep sleep | SC, SP, C2 | ESP32 deep sleep, RTC timer, panel asleep | I1 | 5 min plus 1 h interval | P-066; floor, panel state, reconnect |
| E25 | Plus2 off path | S2 | Manual off and auto-off | I1, I9 | 10 repeats | P-098; PWR click, double click, USB wake, HOLD state |
| E26 | Core deep sleep with keep-on | CO | ESP32 deep sleep, timer wake, boost held | I1, I6 | 5 min, 10 repeats | P-067; current, temperature, wake, reconnect |
| E53 | USB attached versus detached | S3 | Release build, battery only, USB host detached versus attached | I1, I9 | 10 min each | P-205 and the USJ lock; the attached state is not a battery state |

### Phase 3: BLE

| ID | State | Board | Key settings | Instrument | Window | Decides |
| --- | --- | --- | --- | --- | --- | --- |
| P06, P07, P09 | Connected floors | See phase 1 | See phase 1 | I1 | 600 s | Fast, saver, and sleep floors |
| E01 | Scan Full, no advertiser | S3 | `scan_mode 0`, `scan_timeout 0`, Scan page, camera off and out of range | I1, I8 | 60 s | Full scan plateau |
| E02 | Scan Full, one camera | S3 | Same, camera advertising at 1 m | I1, I8 | 60 s | Full scan with one advertiser and duplicate filtering |
| E03 | Scan Balanced | S3 | `scan_mode 1` | I1, I8 | 60 s | 25 percent preset plateau |
| E04 | Scan Low | S3 | `scan_mode 2` | I1, I8 | 60 s | 5 percent preset plateau |
| E05 | Scan page, panel off | S3 | E02, `inactivity 1`, wait for panel off | I1, I9 | 300 s | P-002; scan still active on current code |
| E06 | Targeted reconnect, camera off | S3 | Saved Fujifilm Secure, camera off, infinite reconnect, backoff off | I1, I8 | 600 s | P-045 and P-046; 60 s scan then 5 s wait, repeating |
| E07 | Targeted scan, passive versus active | S3 | Debug build with a passive switch, or a temporary build | I1, I8 | 60 s | P-047; sniffer confirms no scan requests in passive |
| E08 | Connect initiation, camera advertising | S3 | Saved camera, camera on, from idle | I1, I8 | 1 event, 20 repeats | P-003; separate initiator from handshake |
| E09 | Connect initiation, camera unresponsive | S3 | Camera advertises but refuses, or stale address | I1, I8 | 1 event, 10 repeats | P-003; initiator runs to the connect timeout |
| E10 | Companion pairing window | S3 | `companion on`, pairing window open, phone away | I1, I8 | 120 s | P-004; advertising at 100 ms, window closes |
| E11 | Companion reconnect advertising | S3 | Bonded companion, phone away, companion on | I1, I8 | 600 s | P-052; advertising at 1000 ms, no timeout |
| E12 | Companion connected idle | S3 | `companion on`, phone connected, no camera | I1, I8 | 600 s | P-053; live interval from the phone side if possible |
| E27 | 2M PHY event current | S3 | Shutter quiet, 1M versus 2M | I1, I8 | 10 s or 100 events | P-044; event charge, retries, range |
| K-FB-01 to K-FB-05 | Fujifilm Basic family states | S3 and SP | Camera on bench | I1, I8 | Per state | Family handshake, idle, shutter, geotag, link loss |
| K-FS-01 to K-FS-05 | Fujifilm Secure family states | S3 | Camera on bench | I1, I8 | Per state | Family handshake, idle, shutter, geotag, link loss |

Other K families are not measurable in this campaign. Their rows stay `unknown`.

### Phase 4: GPS and IMU

| ID | State | Board | Key settings | Instrument | Window | Decides |
| --- | --- | --- | --- | --- | --- | --- |
| G01 | GPS off, screen on 128 | SC, SP, CO, C2, S3 | Baseline block | I1 | 300 s | GPS off baseline |
| G02 | GPS acquiring, cold | SC, SP, CO, C2, S3 | `gps on`, no backup supply, first fix not yet | I1, I9 | 180 s or until fix | Cold acquisition term at B1 |
| G03 | GPS tracking, B1 | SC, SP, CO, C2, S3 | Fix acquired, 30 min settle | I1 | 1800 s | Tracking term at B1 |
| G04 | GPS tracking, B2 and B3 | S3 and SP | Fix acquired | I1, I4 | 600 s | 31.64 mA at B2; 23 to 25 mA at B3 |
| G05 | GPS standby 5/10/15/60 s | S3 and SP | `gps_power 1`, `gps_duty` | I1 | 1800 s per duty | In-circuit standby, duty saving, active and standby residency |
| G06 | GPS rail cycle, 5 cycles | S3 | `gps_power 2`, duty 15 | I1 | 5 cycles | Re-fix 90 to 130 s and charge per cycle within 20 percent of the sum |
| G07 | GPS degraded backoff | S3 | Block sky view or remove the antenna | I1, I9 | 900 s | Lock released, 0.5 to 5 mA screen off |
| G08 | GPS fix hold, 10 min | S3 | `gps_hold 10`, fix acquired, receiver lost | I1 | 600 s | Rail state; delta is geotag traffic |
| E28 | GPS assist, cold boot | S3 | `gps_assist 0/1/2` | I1, I9 | 5 repeats each | P-087 and P-088; TTFF and cache rejection |
| E29 | GPS baud | S3 | `gps_baud 9600/115200/0` | I1 | 5 repeats each | P-091; fix with no user action, absent unit reaches absent |
| E30 | GPS constellation | S3 | `gps_constel 0/1/7` | I1, I9 | 600 s each | P-089; current, TTFF, satellites, C/N0 |
| E31 | GPS rate | S3 | `gps_nmea on`, `gps_rate 1/2/3` | I1 | 600 s each | P-090; current, checksum failures, fix age |
| E32 | GPS pruned versus full | S3 | `gps_nmea on/off` | I1 | 600 s each | P-092; character rate, parser work |
| E33 | GPS no receiver, fixed baud | S3 | `gps on`, `gps_baud 9600`, no unit | I1 | 600 s | P-011; lock held then released, rail off, retry |
| E34 | IMU off, on software, on hardware | S3 and C2 | `imu on`, `hw_motion 1/2` | I1 | 600 s each | P-078, P-081; backend overhead and suspend |
| E35 | IMU gesture, still and shaken | S3 | `imu_wake 2` | I1 | 300 s each | P-079, P-080; poll cost and misses |
| E36 | IMU false wake | S3 | `imu_wake 2`, tripod in wind, bag, dashboard | I1, I9 | 300 s each | P-082; wake count and display-on seconds |
| E37 | Level page open versus closed | S3 and C2 | `imu on`, Level page | I1 | 300 s each | Gyro and level cost; P-078 |
| E38 | Motion source status | S3 and C2 | Any hardware backend | I1, I9 | 300 s | Plan 20 gate step 1; pin and edges while still |

### Phase 5: network

| ID | State | Board | Key settings | Instrument | Window | Decides |
| --- | --- | --- | --- | --- | --- | --- |
| N01 | Wi-Fi disabled | S3 | Baseline block | I1 | 300 s | Delta under 0.5 mA against P04 |
| N02 | Wi-Fi connected idle, PS min modem | S3, C2 | SSID and PSK set, IP present, NTP off | I1 | 600 s | First band for a station |
| N03 | NTP sync event | S3 | N02 plus `ntp on`, `ntp sync` | I1, I8 | 30 s per sync | Sync burst charge |
| N04 | Wi-Fi reconnect after AP loss | S3 | N02, AP power cycled | I1 | 300 s | Backoff and reconnect within 120 s |
| N05 | Ethernet link down | WAV | Cable removed | I7 | 300 s | First band |
| N06 | Ethernet link up idle | WAV | Cable present, IP present, no MQTT | I7 | 600 s | First band |
| N07 | MQTT connected idle | S3, C2, WAV | Broker reachable, `mqtt connect` | I1, I7 | 600 s | First band |
| N08 | MQTT publish event | WAV | One discovery or status publish | I7, I8 | 30 s per event | Event charge |
| N09 | Companion BLE idle | S3 | `companion on`, phone connected, no camera | I1, I8 | 600 s | 3 to 15 mA above the same state without companion |
| E39 | Station baseline M0 | S3 | Wi-Fi off, camera connected, screen off | I1 | 30 min | Baseline for every Wi-Fi delta |
| E40 | Station only M1 | S3 | M0 plus station up, no MQTT, NTP synced | I1 | 30 min | Isolates the station |
| E41 | MQTT idle M2 | S3 | M1 plus MQTT connected, no traffic | I1 | 30 min | Keepalive and owner loop |
| E42 | Loaded hub M3 | S3 | M2 plus one publish per 10 s and a shutter every 2 s | I1, I8 | 10 min | Loaded hub |
| E43 | WebUI M4 | S3, branch | M3 plus a visible tab polling at 3 s | I1 | 30 min | P-114 and P-115 |
| E44 | Coexistence M5 | S3 | M3 plus 3 and 5 cameras | I1, I8 | 30 min each | P-042, P-107, P-108 |
| E45 | Station with sleep M6 | S3 | M1 with `SLEEP_CONN` on | I1 | 30 min | What the station does to the 3.3 mA floor |
| E46 | Last consumer M7 | S3 | Wi-Fi enabled, MQTT and WebUI off | I1 | 30 min | P-099 |
| E47 | Broker outage M8 | S3 | M2 with the broker down 30 min then up | I1 | 30 min plus recovery | P-112 |
| E48 | Wrong passphrase M9 | S3 | Wrong PSK | I1 | 30 min | P-028 |
| E49 | Join cycles M10 | S3 | 20 `wifi connect` cycles, AP off, camera connected | I1, I8 | 20 cycles | P-109 and P-110 |
| E50 | Auth TX cap M11 | S3 | M3 at the far edge of range, TX cap on and off | I1, I8 | 30 min each | P-105; RSSI, retries, publish success |
| E51 | Listen interval M12 | S3 | `WIFI_PS_MAX_MODEM`, listen interval 1 and 3 | I1, I8 | 30 min each | P-101; p50/p95/p99 |

### Phase 6: discharge and thermal

| ID | State | Board | Key settings | Instrument | Window | Decides |
| --- | --- | --- | --- | --- | --- | --- |
| B01 to B06 | Mixed 4 h profile then full discharge | One per board | S02 10 min, S08 10 min, S10 10 min, S18 10 min, repeat | I5 | 4 h or cutoff | Usable capacity and runtime |
| T01 to T05 | Thermal matrix | S3 | S02 at 0/25/40 degC; S05 at 25 degC; brightness 240 versus 32 | I1, I6 | 30 min each | Whether a 5 degC swing can explain a delta |
| E52 | Cell qualification | Bench | 0.5 C charge to 4.20 V, 60 min rest, 0.2 C discharge to 3.30 V | I5 | Per cell | Usable capacity within 5 percent across two runs |

### Phase 7: model promotion

Model-only work, no bench row:

- Verify the report digest and accounting mode (C10).
- Regenerate baselines only with an accepted bench record and a stated reason.
- Compare bench and model with the same state, boundary, brightness, CPU
  setting, sleep policy, camera family, and link parameters.

## 10. Acceptance thresholds

### 10.1 Repeatability classes

| Code | Applies to | Threshold |
| --- | --- | --- |
| T-active | Steady state above 20 mA | Median of 3 runs within +/-5 percent; CV at most 2 percent |
| T-mid | Steady state 1 to 20 mA | Median within +/-8 percent; CV at most 3 percent |
| T-low | Steady state 0.1 to 1 mA | Median within +/-15 percent or +/-20 uA, whichever is larger; CV at most 5 percent |
| T-floor | Below 100 uA | Median within +/-20 percent or +/-10 uA, whichever is larger; CV at most 10 percent |
| T-event | One event or a short burst | Charge within +/-10 percent; at least 100 events or 10 repetitions |
| T-delta | A claimed saving | Delta at least the larger of 3 sigma, 5 percent of baseline, or twice the noise floor; model and bench sign agree |
| T-discharge | Capacity and runtime | Integrated mAh within +/-10 percent; cell capacity repeat within +/-5 percent |
| T-state | State or timing only | The named state or timing is observed; no absolute current claim |
| T-display | Brightness sweep | Monotonic; R squared at least 0.98; full-scale slope within 30 percent of the inferred backlight |

The expected value is a sanity band, not the acceptance criterion. A valid
measurement that differs from the model is a model correction, not a failed
measurement.

### 10.2 Lane decision thresholds

These are the numeric targets the lanes proposed. They are subordinate to
T-delta: a lane target that is smaller than the combined repeat noise or the
instrument noise floor cannot be resolved at the named boundary and stays
`unknown`.

| Opportunity | Threshold from the lane | Boundary |
| --- | --- | --- |
| P-001, P-002 | Scan phase is separable from the wait phase; a 25 percent preset draws about a quarter of the Full plateau | B1 |
| P-003 | Connect setup must still succeed; record setup time increase, bounded by the connect timeout | B1, I8 |
| P-042, P-043, P-041 | If current does not change measurably between 30 ms and 300 ms intervals, the event share is below noise and those rows close | B1 |
| P-047 | If the match fields are only in the scan response, passive scan is not viable regardless of current | I8 first |
| P-052, P-053 | If advertising charge is below the noise floor, do not spend effort on the companion rows | B1 |
| P-070 | Screen-off average lower than the 5 ms baseline; wake below 50 ms; watchdog fed; intervalometer jitter within tolerance | B1 |
| P-071 | At least 5 percent whole-device reduction on StickC Plus menu idle; no SPI error, touch failure, or visible artifact | B1, I4 |
| P-072 | At least 10 mA reduction at screen-on idle; light-sleep residency above 50 percent; PWM within 1 percent frequency and 2 percent duty; no flicker; no lost BLE event over 30 min. The Display lane proposes this threshold; the Board lane estimates 5 to 15 mA, so 10 mA may be too high (X-22) | B1, I4, I8 |
| P-073 | After the timeout the current equals the Off state; no unexpected Core power-off beyond the documented IP5306 behavior | B1 |
| P-075 | At least 1 mA panel current reduction, otherwise reject | B1 or panel rail |
| P-069 | At least 0.5 mA reduction with tap wake working, otherwise reject | B1 |
| P-019 | At least 0.05 mA reduction with reliable backlight restore, otherwise reject. A 0.05 mA target is below a 1 mA-range instrument; use a dedicated low range or close as unresolved | B1 |
| P-018 | Steady off current within 0.2 mA of the existing Off mode; one SLPIN per timeout; wake below 150 ms; no double sleep | B1 |
| P-014 | Zero flushed pixels while `display_ms.off` covers the window; wake screen pixel-correct | Report counter |
| P-015 | No more than three invalidation events in a 1 s steady menu or battery page window | Report counter |
| P-016 | Icon state visible within 1.2 s; no invalidation increase | Report counter |
| P-084, P-085, P-086 | Standby above about 5 mA in circuit makes fix-aware backoff and motion gating lose most of their value; wake-to-fix after 10 s standby longer than 10 s keeps the duty ceiling at 15 s | B2, B1 |
| P-086 | Re-fix stays within 90 to 130 s; charge per cycle within 20 percent of the sum of tracking and cold-start windows | B1, I1 |
| P-087, P-088 | If assist does not shorten cold boot measurably, leave the default at 0 and add no NVS writes | B1, I9 |
| P-089 | If GPS-only current is within noise of the full constellation, do not ship a constellation default | B1 |
| P-028 | Station stops; `wifi status` explains the reason; fixed passphrase then `wifi connect` succeeds | I9, console |
| P-101 | Reject if p99 shutter latency grows by more than 100 ms | I1, I8 |
| P-105 | Reject if retries rise more than 20 percent | I8, broker |
| P-099, P-100 | Station removal must be shown as a delta at B1, not inferred from the 40.1 mA whole-chip figure | B1 |
| P-040 | Stable P09 for at least 30 min, no disconnects, acceptable first shutter after a 30 s gap, on every vendor claimed | B1 |
| P-068 | Core stays alive 30 min below 45 mA with the boost held; manual off still removes power | B1, I6 |
| P-065, P-066, P-067 | Overnight frame count matches the configured count; EXIF spacing within tolerance; analyzer offset and resolution reported | B1 |
| P-020 | 40 to 150 uA (Board) or 0.4 to 0.8 mA (SoC) must be resolved against the instrument; below the floor, close as rejected | B1 |

## 11. Repeatability and uncertainty

### 11.1 Run count and order

- Three runs minimum per state. Five for a promoted model entry.
- Each run starts from a cold boot and a fresh connection.
- Alternate baseline and changed states: A, B, A, B, A, B. Do not run all A
  then all B.
- Randomize board order across a session.
- Repeat reference state S02 at the start and end of every session. If the two
  references differ by more than 5 percent, invalidate the session.
- Network states need three runs on two different days because AP and broker
  behavior varies more than the DUT.

### 11.2 Uncertainty budget

| Source | Typical contribution | How to bound it |
| --- | --- | --- |
| Instrument gain | 0.2 to 0.5 percent | Calibration certificate and C02 |
| Shunt tolerance | 0.1 percent | 4-wire measurement, C03 |
| Burden and lead resistance | 0.2 percent | C04 and short leads |
| Timebase | 0.1 percent | C08 |
| Temperature | 1 to 3 percent | T01 to T04; reject over 2 degC board drift |
| State repeatability | 2 to 10 percent | Three runs, CV |
| Camera or AP variability | 5 to 20 percent | Fixed peer, fixed channel, two days |

Report the combined U95 for each state. Target 5 percent or better for active
states and 15 percent or better for sleep states. If U95 is worse than the
T-class, the state is not resolved and the row stays `unknown`.

### 11.3 Environmental controls

- Same room, same bench, same camera distance (1.0 m), same orientation.
- Camera battery above 50 percent. Record the level.
- Phone off or in airplane mode. AP on a fixed channel 2 m away, or off, except
  during the Wi-Fi rows.
- No USB charger on the bench during a B1 run.
- Same enclosure and strap.
- Record firmware commit, build flags, and settings SHA-256.

## 12. Promotion from modeled to measured

### 12.1 Model agreement tiers

| Tier | Criterion | What it supports |
| --- | --- | --- |
| M0 | Model only, no bench | Relative ranking, CI regression |
| M1 | Bench measured, model not compared | A measured state, no model promotion |
| M2 | Bench and model sign agree; magnitude within 30 percent or within the YAML min/max band | A reviewed model correction, still not a default change |
| M3 | M2 plus three runs on two days, two boards where applicable, and a second instrument | `measured-local` model entry and a default-change proposal |

### 12.2 Saving gate

A saving is a difference, not a state. Use a paired design: baseline and changed
state on the same board, same camera, same firmware, same ambient, in alternating
order.

```text
Delta_model = model_baseline_mA - model_changed_mA
Delta_bench = median_baseline_mA - median_changed_mA
sigma_delta = sqrt(sigma_baseline^2 + sigma_changed^2)
noise_floor = 3 * RMS noise of the instrument on a stable load
```

A saving is promoted only when all five conditions hold:

1. `Delta_bench > max(3 * sigma_delta, 0.05 * median_baseline, 2 * noise_floor)`.
2. `sign(Delta_bench) == sign(Delta_model)`.
3. `abs(Delta_bench - Delta_model) <= max(0.30 * abs(Delta_model), model_band)`,
   where `model_band` is the YAML `max - min` for the changed component. If the
   component has no band, use 30 percent.
4. State definitions are identical, including brightness, CPU setting, sleep
   policy, camera family, GPS policy, and link parameters.
5. The record has three accepted runs and a raw trace hash.

If condition 1 fails, the saving is below the noise floor. Report it as
`unknown`, not as a small saving.
If condition 2 fails, the model or the state definition is wrong. Do not change
the default; file a model correction with the trace.
If condition 3 fails but the sign agrees, the model magnitude is wrong. Add the
measurement to the record and correct the YAML entry in a separate review.

A promoted saving gets the label `measured` only for the exact board, state,
firmware, and camera family in the record. A second board needs its own run.
This is M2. A `measured-local` YAML entry or a default change still needs M3.

### 12.3 What cannot be promoted

- A single run.
- A run with USB attached.
- A run that mixes B1 with B2 or B3 without a recorded conversion.
- A run on a different firmware commit or a different camera firmware.
- A run with a different brightness, CPU setting, or sleep policy.
- A FauxNY result presented as a camera family result.
- A model report with a missing or mismatched digest.
- A run that failed validity but produced a plausible number.
- A delta smaller than the instrument noise floor.
- A default change supported only by the S3 when the setting is compiled for all
  boards.

## 13. Failure protocol

| Class | Detection | Action | Rerun rule | Record |
| --- | --- | --- | --- | --- |
| F1 Noise, mains | 50 or 60 Hz peak above 3 sigma | Differential probe, integer mains cycles, single ground point | Up to 3 attempts | Keep the rejected trace |
| F2 Noise, broadband | RMS above the T-class noise budget | Check camera, phone, AP, USB, ground loop | Up to 3 attempts | Keep the rejected trace |
| F3 Drift | Start and end reference differ by more than 5 percent | Rest, re-soak, check temperature | New session | Mark the session invalid |
| F4 Charging or USB | Charging true, or 5 V present at the connector | Remove USB, wait for the charger to settle | Rerun | Mark the run invalid |
| F5 Wrong state | Camera not connected, panel not off, no GPS fix, lock still held | Fix the state, verify with I9 or the debug console | Rerun | Do not average |
| F6 Burden or clipping | Shunt voltage over budget, instrument range limit | Change shunt or range | Rerun | Keep the clipped trace as rejected |
| F7 Thermal | Board drift over 2 degC, ambient over 3 degC | Soak, move the sensor, lower the load | Rerun in a chamber if needed | Record both temperatures |
| F8 Model mismatch | Digest mismatch, `model_valid` false, accounting mode mismatch | Fix the model path or regenerate the baseline | Do not compare | `compare.py` exit 2 |
| F9 Simulator nondeterminism | Two runs differ beyond the threshold | Record host load and scenario; rerun isolated | Do not use for a gate | Open a simulator issue |
| F10 Cell or PMIC cutoff | Core cuts off early, cell below 3.3 V | Record the cutoff time; do not report a steady state | New charge cycle | Mark the state as cutoff, not sleep |

Decision order:

1. Is the state correct? If no, F5, fix, rerun.
2. Is the boundary correct? If no, F4 or F6, fix, rerun.
3. Is the temperature stable? If no, F7, rerun.
4. Is the noise above budget? If yes, F1 or F2, fix, up to three attempts.
5. Is the model digest correct? If no, F8, fix, do not compare.
6. Otherwise the measurement stands; compare with the expected band.

If three attempts fail, or two instruments disagree beyond their budgets, mark
the row `unknown`, preserve the raw trace and session record, open an issue with
state ID, board, instrument, trace hash, and failure class, and change no
default, baseline, or model entry while the row is `unknown`.

## 14. Execution order and exit criteria

| Phase | Work | Depends on | Exit criterion |
| --- | --- | --- | --- |
| 0 | C01 to C10 | Instruments and fixtures | All calibration rows pass; a failed row blocks the affected states |
| 1 | P01 to P15, D01 to D06, E13 to E17 and E19 to E21 | Phase 0 | Board floors, scan, multi-connect, display slope, APB lock, UI loop, and sleep/wake transitions recorded; Core cutoff recorded |
| 2 | P04, P05, P08 to P10, L01, E18, E22 to E26, E53 | Phase 1 | Sleep and off states classified; no unresolved state ambiguity; deep sleep still absent and marked not measurable |
| 3 | P06, P07, E01 to E12, E27, K-FB, K-FS | Phase 1 | BLE bands, scan duty, initiator, companion, and the two Fujifilm families recorded |
| 4 | G01 to G08, E28 to E38 | Phase 1 | GPS B1, B2, B3 separated; cold start, standby, and IMU modes measured |
| 5 | N01 to N09, E39 to E51 | Phase 1 | Network bands established; no model claim made |
| 6 | B01 to B06, T01 to T05, E52 | Phase 2 | Capacity, runtime, and thermal sensitivity recorded |
| 7 | Model promotion and doc update | Phases 1 to 6 | M2 or M3 evidence recorded; YAML changes reviewed separately |

Phase 1 is the minimum useful campaign. It answers whether the screen, MCU, and
PMIC expectations are close, and it finds the states where the model and the
board disagree before the expensive camera and discharge work.

The first three measurements to run, in order:

1. P09 at B1 with USB detached, three runs. It decides P-040 and the 3.3 mA
   anchor.
2. P01, P02, and D06 at CPU 160 and CPU 80. They decide the display and APB
   lock ranking.
3. G05 at B2 and B1. It decides every GPS duty row.

## 15. Crosswalk from the lane plans

| Lane plan ID | Merged ID | Note |
| --- | --- | --- |
| BLE S06a to S06g | E01 to E07 | Scan and targeted-scan states |
| BLE S07a, S07b | E08, E09 | Connect initiation with and without a responsive peer |
| BLE S35a to S35c | E10 to E12 | Companion advertising and connected idle |
| Display M-01 to M-16 | P01 to P04, D01 to D06, E13 to E21 | M-05 maps to P08/P09; M-10 to E20; M-11 to E14; M-12 to E15; M-13 to E16; M-14 to E17; M-15 to E18; M-16 to E19 |
| GPS scenario 1 to 15 | G01 to G08, E28 to E38 | Scenario 1 maps to G01; 2 to E33; 3 to G03; 4 to G05; 5 to E32; 6 to E31; 7 to G07; 8 to E28; 9 to E29; 10 to E30; 11 to E34; 12 to E35; 13 to E36; 14 to E37; 15 to E38 |
| Wi-Fi M0 to M10 | E39 to E49 | M11 is not a run; coexistence has no runtime switch |
| Board B1 to B16 | P01 to P10, L01, E18, E22 to E26, E53 | B12 maps to E23; B15 maps to E53 |
| SoC candidate A/B rows | P01 to P15, E14 to E16 | The SoC A/B list maps onto the board and display states |
| Measurement C01 to C10 | C01 to C10 | Unchanged |

## 16. Open limits and disagreements that affect this plan

1. Only the StickS3 is attached. Every other board row is blocked on hardware.
2. Only Fujifilm has camera hardware. The other K families stay `unknown`.
3. Deep sleep does not exist in the tree. L02 stays not measurable, and P-065 to
   P-067 cannot be promoted before the implementation lands.
4. The Core cannot hold a steady state below 45 mA. Its idle behavior is a PMIC
   cutoff, not a firmware sleep state.
5. Wi-Fi, Ethernet, MQTT, and companion have no model terms. Their rows
   establish bands; they do not validate a model.
6. The model has no scan state, no brightness term, no wake cost, no PSRAM term,
   no PMIC term, and no peripheral term. A bench result that differs is a model
   correction first.
7. The 3.3 mA S3 connected floor is a reported observation, not a trace in
   `plans/00-hardware-experiments.md`. P09 either reproduces it or replaces it.
8. The committed `screen-off-30s` baseline is 0.510703 mA, not the 0.25 mA cited
   in plan 98 and in three lanes. Do not mix the two.
9. The 14.1 mA ESP32 modem-sleep figure is a published example, not a local
   measurement. P08 measures the actual floor with `sleep_conn` off.
10. The 40.1 mA Wi-Fi station figure is a published whole-chip average, not a
    delta. P-099 and P-101 must be measured as differences at B1.
11. The AXP192 whole-chip operating quiescent is not a datasheet number. Plan 98
    says about 2 mA; the Board lane's block sum is about 316 uA. Measure the
    board with the rails furble actually enables.
12. A 0.05 mA target (P-019) is below the resolution of a 1 mA-range instrument.
    Use a dedicated low range or close the row as unresolved, not as a small
    saving.
13. A null result is a valid result. If the delta is not visible, close the row
    as rejected and record the null.
14. The display default saving is about 64 mA inferred in the Display lane
    and about 40 mA in the Board lane (X-23). P01 and P02 settle the
    absolute current; do not quote the delta without it.
15. Camera parity, RF certification, safety certification, and production
    calibration are out of scope.

## 17. Record template

Record ID: `PWR-<YYYYMMDD>-<board>-<state>-R<run>`.

Session header fields: record ID, date, operator, board, board serial, firmware
version, git commit, build env, PMIC, panel, cell serial, cell capacity, camera
family, camera model, camera firmware, GPS unit, GPS serial, instrument,
instrument serial, calibration date, shunt ohms, shunt tolerance, sample rate,
range, ambient temperature, board temperature, USB state, charging state, settle
seconds, capture seconds, state ID, settings SHA-256, raw trace path, raw trace
SHA-256, model report, model digest, notes.

Per-run fields: run ID, record ID, run index, start UTC, duration, mean, median,
p95, RMS noise, charge, energy, temperature start and end, observed state,
valid flag, invalid reason.

Summary fields: state ID, board, run count, median, minimum, maximum, CV, U95,
model current, model delta percent, expected band, verdict, evidence label.

Artifact layout:

```text
docs/power/records/<record_id>/
  session.csv
  runs.csv
  summary.csv
  settings.txt
  model-report.json
  trace.sha256
  notes.md
```

Raw traces stay in the approved lab storage, not in git. Commit the summary, the
settings dump, the model report, the trace hash, and the notes. The record
directory does not exist yet; the first accepted run creates it.
