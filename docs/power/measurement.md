# Bench measurement plan

Status: plan only. No bench measurement described in this document has been
executed. Every row in the measurement register carries the evidence label
`unknown` until a record ID with an accepted raw trace exists. A simulator
report is not a measurement, and a datasheet number is not a local
measurement.

This is the measurement lane named in `docs/power/README.md`. It turns the
opportunities in `plans/98-power-optimization-audit.md`, the follow-up in
`plans/98b-power-followup.md`, and the model limits in
`tools/power-model/README.md` into bench states, instruments, thresholds, and
records. It does not change firmware, defaults, the model, or any baseline.

## 1. What this plan decides

The plan answers four questions for each power opportunity:

1. Which electrical boundary and which board state does the claim apply to.
2. Which instrument and capture window can resolve the expected change.
3. What measurement is valid, and what makes it invalid.
4. What evidence is required before a modeled saving may be called a measured
   saving, or before a default or a model entry changes.

Out of scope: camera protocol parity, RF certification, safety certification,
production calibration, and any claim about a board that is not on the bench.

## 2. Evidence vocabulary

The labels match `docs/power/README.md` so the lane reports consolidate without
translation.

| Label | Definition | May support | May not support |
| --- | --- | --- | --- |
| `measured` | Accepted bench trace, at least three independent runs, record ID, raw trace hash, settings dump, and instrument calibration ID. | A measured claim. With M3, a `measured-local` entry in `tools/power-model/board-currents.yaml` and a reviewed default change. | A claim on a different board, boundary, firmware, or camera family. |
| `modeled` | A simulator report with `model_valid: true`, a matching model digest, and the same accounting mode as its baseline. | Relative comparisons and the CI power gate. | An absolute current, runtime, or hardware-parity claim. |
| `inferred` | Datasheet value, published third-party measurement, or arithmetic derivation. | An expected band and a design hypothesis. | A local measurement claim. |
| `unknown` | No evidence, or evidence rejected under section 15. | Nothing. | Nothing. |

Rules that apply to every row:

- A label changes only when the evidence changes, never when a document is
  edited.
- A model report never becomes `measured` by relabeling it. It becomes
  `measured` only after the promotion gate in section 10.4 passes.
- One run is a sample, not repeatability.
- A delta without an uncertainty is not a saving.
- Values from different electrical boundaries are never added.
- Every claim names the board, firmware commit, state ID, boundary, instrument,
  and record ID.
- A row that says `expected` is a hypothesis. It is not a measurement and is
  not an acceptance criterion.

## 3. Electrical boundaries

Most power mistakes in this tree come from mixing boundaries. Plan 98 already
flags the GPS case: the model holds a 3.3 V module estimate, the vendor page
quotes a 5 V Grove input, and the battery sees the boost converter input.

| Boundary | Node | What a measurement here includes | Typical use |
| --- | --- | --- | --- |
| B1 | Battery positive at the board, before the PMIC | Cell current, PMIC conversion, every rail, every peripheral | Board current, runtime, model calibration |
| B2 | Grove Port A 5 V pin | Unit GPS v1.1 regulator and receiver | GPS unit comparison with the M5Stack 31.64 mA figure |
| B3 | GPS module VCC at 3.3 V | AT6668 module, TCXO, flash, antenna LNA | Module comparison with the 23 to 25 mA figures |
| B4 | USB or 5 V input jack | Charger, boost, USB PHY, console bridge | Charging behavior, Waveshare, debug correlation only |
| B5 | Ethernet jack or PoE input | W5500 PHY, magnetics, optional PoE HAT | Waveshare network states |

Boundary rules:

- B1 is the only boundary for a battery runtime claim.
- B2 and B3 are diagnostics for the GPS term. They are never substituted for
  B1 in a board total.
- When converting B2 or B3 to B1, use the measured conversion efficiency for
  that board and state, not an assumed 100 percent. Record the efficiency.
- B4 measurements made with USB attached are not battery measurements. USB
  also charges the cell and changes the PMIC state.
- B5 is a mains measurement. It has no battery runtime meaning.

## 4. Instruments

Instrument codes are used in the register.

| Code | Role | Minimum capability | Example instrument | Used for |
| --- | --- | --- | --- | --- |
| I1 | Primary source and measure, high dynamic range | 1 uA to 1 A, at least 16 bit, at least 100 kSa/s; 1 MSa/s for event work, otherwise pair it with I4, source mode at 3.7 V and 5 V | Keysight N6705C with N6781A, or Joulescope JS220 | All steady states, event captures, discharge support |
| I2 | Secondary logger | 1 uA to 1 A, at least 10 kSa/s, long capture | Nordic Power Profiler Kit II or Otii Ace | Repeat runs, 30 minute soaks |
| I3 | DC average reference | 6.5 digits, 1 Sa/s to 10 Sa/s, 4-wire resistance | Keysight 34465A or Keithley DMM6500 | Gain check, shunt value, DC cross-check |
| I4 | Transient capture | DC to 20 MHz, differential probe, 1 MS/s or faster | Oscilloscope with a differential probe across the shunt | BLE events, display flush, wake spikes, noise diagnosis |
| I5 | Battery discharge | Coulomb counting, at least 1 Sa/s, programmable cutoff | Electronic load with coulomb counter, or I1 in source mode | Capacity and runtime |
| I6 | Thermal | +/-0.5 degC, one ambient and one board contact channel | Calibrated thermocouple logger or thermal camera | Thermal drift and enclosure checks |
| I7 | 5 V input analyzer | 1 mA resolution, at least 1 kSa/s, USB or jack inline | Bench SMU or inline USB power analyzer with logging | Waveshare, charging reference |
| I8 | BLE sniffer | Captures connection events, interval, PHY, payload length, retries | Nordic Sniffer, Ellisys, or Teledyne front-line | Link-parameter and airtime evidence |
| I9 | State observer | Photo, video, or camera indicator | Phone camera or bench camera, phone radio off | Screen state, camera link, GPS fix |

Instrument rules:

- The primary instrument carries a calibration certificate no older than 12
  months. Record the certificate ID and the calibration date.
- I3 is the traceable gain reference for I1 and I2. I1 and I2 must agree on the
  same steady load before either is used for a claim.
- I4 is not an average-current instrument. Use it to understand an event shape
  and to diagnose noise, then integrate the event with I1 or I2.
- I8 is state evidence, not current evidence.
- I9 is state evidence. A photo of the screen is sufficient for a screen state;
  it is not a current measurement.

## 5. Shunt and current probe setup

### 5.1 Battery shunt

The preferred setup is a high-side shunt in the battery positive lead, before
the PMIC and before any charger node:

```text
cell + ---[ fuse ]---[ shunt ]---+--- board VBAT
                        |        |
                   probe +   probe -
                        |        |
                        +--------+

cell - --------------------------+--- board GND
```

Connect the probe so that current into the board reads positive. With the
shunt in the positive lead, the cell side of the shunt is the positive input
and the board side is the negative input.

- Use a 4-terminal, 0.1 percent shunt. Solder the sense wires at the shunt
  terminals, not at the board.
- Active states: 0.01 ohm. Sleep states: 10 ohm. Never put both in series.
- Burden voltage budget: below 20 mV in an active state, below 100 mV in a
  sleep state. Compute `V = I * R` and record it with the run. If the budget is
  exceeded, change range and rerun. Do not correct a clipped trace.
- Twist the sense pair, keep the loop area small, and keep the probe ground at
  the shunt, not at the USB connector.
- A 0.01 ohm shunt at 100 mA drops 1 mV. A 10 ohm shunt at 100 uA drops 1 mV.
  Both are within a 24 bit instrument's range; neither is within a cheap
  INA219 class monitor's noise floor.
- Fuse the battery lead at the cell's rated current. A Li-ion cell into a short
  is the main bench hazard.
- Do not cut the battery protection board out of the circuit. The protection
  FET stays between the cell and the shunt.

### 5.2 Current probe

For event work, use a differential probe across the shunt rather than a
clamp-style current probe. A clamp probe with a 1 mV/mA transfer cannot resolve
the 0.3 to 1 mA sleep floor and adds its own offset drift.

- DC coupling, 20 MHz bandwidth limit, probe zeroed with the load off.
- Common-mode range must cover the battery positive node. If it does not, move
  the shunt low side, but record that the board ground is no longer the cell
  negative.
- Check probe attenuation against I3 before the run: a 10 mA stable load should
  read 10.0 mA +/- 1 percent on both.

### 5.3 USB, charging, and ground loops

- Remove USB for every B1 measurement. Tape the connector or use a data-only
  cable with the power pins removed for console correlation, but never both at
  once. On the S3, the USB-Serial/JTAG console needs VBUS and resets the chip
  when the host opens the port, so it cannot be used for a B1 run.
- Confirm the charging state is false. On AXP192 boards the console reports it.
  On the S3 the M5PM1 reports charge status but no current. On the Plus2 there
  is no charging telemetry, so the USB connector must be physically absent.
- Put the host laptop on battery or use a USB isolator. A grounded laptop and a
  grounded instrument create a loop through the shunt.
- The camera peer runs on its own battery. Do not power the camera from the
  bench supply that powers the DUT.
- Keep the phone and the Wi-Fi access point at least 2 m from the DUT, or turn
  them off, except during the Wi-Fi rows where the AP is part of the state.
  Record which radios were active.

### 5.4 Board-specific notes

- **M5StickC, StickC Plus, Core2 (AXP192).** The PMIC has a battery current
  register. It is a cross-check only, not the reference. The AXP192 off-mode
  current is 27 uA; the operating quiescent is not a single datasheet number.
- **M5StickC Plus2 (no PMIC).** Battery voltage only. The firmware reports no
  current and no charging state. Use I1 at B1 and confirm USB absence visually.
- **M5Stack Core (IP5306).** The boost converter shuts off after 32 s of
  continuous load below 45 mA. Any state that settles below 45 mA is not a
  steady state. Record the time to cutoff and the post-cutoff current instead
  of a mean.
- **M5StickS3 (M5PM1).** No battery current backend in M5Unified and no
  documented current register. Use I1 at B1. The PMIC can shut the board down
  to 14 uA.
- **Waveshare ESP32-S3-ETH.** No battery, no display, no PMIC. Measure at B4 or
  B5 with I7. Do not use the battery path or the AXP192 assumptions.

## 6. Sample rates and capture windows

| State class | Rate | Pre-roll | Settle | Minimum window | Why |
| --- | --- | --- | --- | --- | --- |
| Connection event, shutter, wake | 1 MS/s | 5 s | none | 10 s or 100 events | BLE events are 1 to 3 ms; display flushes are 1 to 10 ms |
| Active steady, screen on | 100 kSa/s | 10 s | 120 s | 300 s | Resolves UI bursts without hiding the mean |
| Connected steady | 10 kSa/s | 10 s | 120 s | 600 s | Covers several reconnect and saver transitions |
| Sleep floor | 1 kSa/s | 10 s | 300 s | 1800 s | The floor is DC; a long window beats a fast one |
| Battery discharge | 1 Sa/s plus 10 kSa/s bursts | none | 60 min rest | Full run or 4 h | Coulomb counting, not waveform detail |
| Calibration | 10 kSa/s | none | 60 s | 60 s | Gain, offset, noise floor |
| Thermal soak | 1 kSa/s | none | 1200 s | 1800 s | Temperature and current must be stable together |

Rules:

- The sample rate must be at least 10 times the highest event rate of
  interest. A 30 ms connection interval needs at least 1 kSa/s to see the
  event, and 100 kSa/s to see its shape.
- Capture the 1 MS/s event shape with I4, and integrate the same window with I1
  or I2. An instrument that only reaches 100 kSa/s may miss a sub-millisecond
  spike even when its average is correct.
- Do not downsample before integrating. Integrate the raw trace with the
  trapezoid rule, then report the mean.
- For a pulsed load, report both the time-weighted mean and the charge per
  event. A sample mean that ignores the off time is not a current.
- Store the raw trace outside git. Commit the summary CSV, the settings dump,
  the model report, and the trace SHA-256.
- A 1 MS/s, 10 s capture is manageable. A 1 MS/s, 30 minute capture is not.
  Use the slow rate for long windows and trigger a fast capture around events.

## 7. State definitions

These are the canonical states. The register and the board matrix use these
IDs. A measurement that does not match the entry condition is not that state,
even if the current looks plausible.

### 7.1 Baseline settings

Apply this block with a `-debug` build, verify it with `settings list`, then
flash the release build without erasing NVS. Do not run `erase_flash` between
the two steps. The console distorts power, so it is used for configuration and
state verification only, never for a B1 claim.

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

If a build does not contain a setting, `settings set` rejects it. Record the
rejection in the session notes instead of silently skipping it.

### 7.2 Power and display states

| ID | State | Entry condition | Verification | Minimum window |
| --- | --- | --- | --- | --- |
| S01 | Boot to idle | Cold boot, no USB, splash complete, main page visible | Photo at 0 s and 30 s; no camera connected | 30 s |
| S02 | Menu idle, brightness 128 | Baseline block, main page, no camera, no GPS, no Wi-Fi | Photo; `settings get brightness` on the debug build | 300 s |
| S03 | Menu idle, brightness 240, CPU 80 | Baseline block, brightness 240, CPU 80 | Photo; `settings get` values | 300 s |
| S04 | Screen dim | Baseline block, `inactivity 1`, `display_off 0`, 30 s idle | Photo of the dim panel; no touch or button | 300 s |
| S05 | Screen off, disconnected | Baseline block, `inactivity 1`, `display_off 1`, 30 s idle plus 5 s dwell | Photo of the blank panel; no camera | 300 s |
| S06 | Scan full, balanced, low | Baseline block, `scan_mode 0/1/2`, scan page open | `scan` console state on the debug build; camera in range | 60 s per mode |
| S07 | Connect handshake | Saved camera, camera on, connect from cold boot | `cameras status` on the debug build; camera indicator | 1 event |
| S08 | Connected idle, fast, screen on | Camera connected, 60 s quiet, `conn_saver off` | `debug camera` interval in the fast profile | 600 s |
| S09 | Connected idle, saver, screen on | Camera connected, `conn_saver on`, wait 15 s | Idle profile, 200 to 240 BLE units, 250 to 300 ms | 600 s |
| S10 | Connected, screen off, sleep off | Camera connected, `sleep_conn off`, screen off | Blank panel; link alive; `power stats` lock on the debug build | 600 s |
| S11 | Connected, screen off, sleep on | S3 only, `sleep_conn on` before connect, screen off | Link alive; light-sleep residency in the model or the current signature | 600 s |
| S12 | Shutter single | S10 or S11 plus one shutter press | Camera takes a frame; `cameras status` link alive | 10 s per press |
| S13 | Intervalometer, 5 frames | Connected, interval 2 s, count 5, shutter 30 ms | Five frames on the camera | 15 s |
| S14 | IMU gesture poll, screen off | Screen off, `imu on`, `imu_wake 3`, `imu_trigger on` | Gesture timer present; screen still off | 300 s |
| S15 | Multi-connect, 2 cameras, screen off | Two saved cameras, `multiconnect on`, both connected | Two active targets | 600 s |
| S37 | Charging, reference only | USB attached, charging true where the board reports it, otherwise current into the battery at B1 | Charging flag or current direction; never used for B1 claims | 300 s |

### 7.3 GPS states

| ID | State | Entry condition | Verification | Minimum window |
| --- | --- | --- | --- | --- |
| S16 | GPS off | Baseline block, `gps off` | `gps` reports disabled, rail off | 300 s |
| S17 | GPS acquiring, cold | `gps on`, no backup supply, first fix not yet | `gps` cycle acquiring, no fix | 180 s or until fix |
| S18 | GPS tracking | Fix acquired, 30 min settle, antenna at the window | `gps` cycle tracking, satellites stable | 1800 s |
| S19 | GPS standby 5, 10, 15 s | `gps_power 1`, `gps_duty 5/10/15` | `gps` policy standby, duty value, cycle alternates | 1800 s per duty |
| S20 | GPS rail cycle | `gps_power 2`, duty 15, experimental warning accepted | `gps` cycle waiting, acquiring, tracking | 1800 s or 5 cycles |
| S21 | GPS degraded | Block sky view, or remove the antenna on the SMA unit | `gps` degraded yes, retries N, lock released | 900 s |
| S22 | GPS fix hold | `gps_hold 10`, fix acquired, then lose the receiver | `fix_state held`, `hold_remaining` counts down | 600 s |

### 7.4 Sleep and power-off states

| ID | State | Entry condition | Verification | Minimum window |
| --- | --- | --- | --- | --- |
| S23 | Disconnected light sleep | S05, no camera, no GPS, 300 s settle | Current in the sleep band; no lock held on the debug build | 1800 s |
| S24 | Power off | Power menu off, or the documented long press | Display dark; current at the PMIC off value | 600 s |
| S25 | Core IP5306 cutoff | Core, any state below 45 mA, 32 s | Current drops to the standby value; device is off | 120 s |
| S26 | Deep sleep | Not implemented. No `esp_deep_sleep` call exists in the tree. | None | Not measurable |

### 7.5 Network states

Wi-Fi is a real runtime path in `src/FurbleWiFi.cpp`, not config residue. Plan
98's statement that no code initializes Wi-Fi is stale for this tree:
`src/FurbleWiFi.cpp` is in the unconditional source list and calls
`esp_wifi_init` and `esp_wifi_start`. It is off by default. The station uses
`WIFI_PS_MIN_MODEM`, and NTP starts only when Wi-Fi, an IP address, and the NTP
setting are all present. MQTT is compiled only into the profiles that define
`FURBLE_MQTT`.

| ID | State | Entry condition | Verification | Minimum window |
| --- | --- | --- | --- | --- |
| S27 | Wi-Fi disabled | `wifi off`, no driver | `wifi status` state disabled | 300 s |
| S28 | Wi-Fi connected idle | `wifi_ssid` and `wifi_psk` set, `wifi on`, `wifi connect`, IP present, NTP off | `wifi status` connected, RSSI and IP | 600 s |
| S29 | Wi-Fi NTP sync | S28 plus `ntp on`, `ntp sync` | `ntp status` synced and offset | 30 s per sync |
| S30 | Wi-Fi reconnect | S28, AP power cycled | State connecting, backoff, then connected | 300 s |
| S31 | Ethernet link down | Waveshare, cable removed | Link down in the Ethernet log | 300 s |
| S32 | Ethernet link up idle | Waveshare, cable present, IP present, no MQTT | Link up, IP present | 600 s |
| S33 | MQTT connected idle | MQTT broker reachable, `mqtt connect` | `mqtt status` connected | 600 s |
| S34 | MQTT publish | S33 plus one discovery or status publish | Broker receives the message | 30 s per event |
| S35 | Companion BLE idle | `companion on` (restart applies), phone connected, no camera | Companion status connected | 600 s |

### 7.6 Camera link states

| ID | State | Entry condition | Verification | Minimum window |
| --- | --- | --- | --- | --- |
| S36 | Camera powered off while linked | Connected camera, then camera power off | Link drops; time to disconnect; camera off | 60 s |
| S07 | Handshake | see above | see above | 1 event |
| S08 | Idle fast | see above | see above | 600 s |
| S09 | Idle saver | see above | see above | 600 s |
| S12 | Shutter | see above | see above | 10 s per press |

## 8. Scenario matrix

### 8.1 Board matrix

The six release images are the measurement set. They cover seven board rows
because the M5StickC Plus2 shares the `m5stick-c-plus` image. The developer
`esp32-s3-headless` profile is not a release board; use it only as a control
when a display-less ESP32-S3 comparison is needed, and mark it as a developer
result.

| Board | Build env | MCU | PMIC | Panel | Cell capacity in firmware | Battery current sense | Sleep reachable | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| m5stick-c | `m5stick-c` | ESP32 | AXP192 | ST7735 80x160 | 95 mAh | Yes, AXP192 | Light sleep, modem sleep | End of life; 48 minimum brightness |
| m5stick-c-plus | `m5stick-c-plus` | ESP32 | AXP192 | ST7789 135x240 | 120 mAh | Yes, AXP192 | Light sleep, modem sleep | Plus and Plus2 share the image; 48 minimum brightness |
| m5stick-c-plus2 | `m5stick-c-plus` | ESP32 | None | ST7789 135x240 | 200 mAh | No | Light sleep | Voltage only; no charging telemetry; 32 minimum brightness |
| m5stack-core | `m5stack-core` | ESP32 | IP5306 | ILI9342C 320x240 | 0, unknown | No | No steady state below 45 mA | Boost cuts off after 32 s below 45 mA |
| m5stack-core2 | `m5stack-core2` | ESP32 | AXP192 | ILI9342C 320x240 | 390 mAh | Yes, AXP192 | Light sleep, modem sleep | Touch controller stays on; 32 minimum brightness |
| m5stick-s3 | `m5stick-s3` | ESP32-S3 | M5PM1 | ST7789 135x240 | 250 mAh | No | Light sleep with `sleep_conn`; no deep sleep | Watchdog, GPS power lock, 32 minimum brightness |
| waveshare-s3-eth | `waveshare-s3-eth` | ESP32-S3 | None | None | Mains | No | Not applicable | W5500 Ethernet; DFS init and tickless idle are not enabled in its sdkconfig |

The cell capacities are firmware constants used for the runtime estimate, not
measured cell capacity. A discharge test measures the actual cell.

Board abbreviations used below: SC = m5stick-c, SP = m5stick-c-plus, S2 =
m5stick-c-plus2, CO = m5stack-core, C2 = m5stack-core2, S3 = m5stick-s3, WAV =
waveshare-s3-eth.

### 8.2 State by board

Codes: `R` required, `C` conditional on hardware or build, `NA` not applicable.

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

### 8.3 Camera family matrix

Furble implements nine camera modes. Only Fujifilm has hardware test coverage
today; the other families are code-reviewed and FauxNY-tested. The bench matrix
is therefore an evidence plan, not a support claim.

The S3 is the primary radio reference because it is the only board with the
GPS power lock and the only board where the UI offers connected light sleep
through `SLEEP_CONN`. StickC Plus is the AXP192 cross-check for the Fujifilm
Basic family. If a family shows an anomaly on the S3, expand that family to the
other boards before generalizing.

| Code | Family | Class | Pairing | Shutter shape | Focus | Geotag | Link-loss signal |
| --- | --- | --- | --- | --- | --- | --- | --- |
| FB | Fujifilm Basic | `FujifilmBasic` | Unsecured | Two write-with-response operations | Yes | 10 s sync interval | Supervision timeout |
| FS | Fujifilm Secure | `FujifilmSecure` | Secure display or keyboard | Two write-with-response operations | Yes | 10 s sync interval written during setup | Supervision timeout |
| CS | Canon EOS Smart | `CanonEOSSmart` | Smart device | Write sequence | Yes | Yes | Supervision timeout |
| CR | Canon EOS Remote | `CanonEOSRemote` | Remote controller | Write sequence | Yes | No | Supervision timeout |
| NK | Nikon | `Nikon` | Remote mode works, smart mode does not pair | Write sequence | No in remote mode | No in remote mode | Supervision timeout |
| SO | Sony | `Sony` | Sony ZV and modern bodies | Write sequence | Yes | Yes, when location is enabled | Supervision timeout |
| RI | Ricoh | `Ricoh` | GR IV series | Single write, capture with autofocus | No | Yes | Announced power state |
| LX | Panasonic Lumix | `Lumix` | MEI0 session | Write sequence | Yes | 16 byte payload | Supervision timeout |
| DJ | DJI Osmo | `DJIOsmo` | DJI R SDK | Write sequence, record toggle | No | No | Supervision timeout |
| FX | FauxNY | `FauxNY` | Software peer | Simulated | Simulated | Simulated | Not a bench camera |

Camera state IDs are `K-<family>-01` to `K-<family>-05`:

| Suffix | State | Expected | Acceptance | Instrument |
| --- | --- | --- | --- | --- |
| 01 | Cold connect handshake | 2 to 30 s, 0.5 to 5 J; secure 1 to 8 J | T-state and T-event | I1, I8 |
| 02 | Connected idle, screen off, fast then saver | Fast 30 to 45 mA; saver 27 to 44 mA | T-mid | I1 |
| 03 | Shutter single, screen off | 0.2 to 2 mC per press | T-event | I1, I8 |
| 04 | Geotag sync, screen off | 0.02 to 0.2 mC per write; under 0.5 mA average | T-event | I1, I8 |
| 05 | Camera power off while linked | Disconnect in 5 to 10 s; charge under 2x idle | T-state | I1, I8 |

`K-FX-*` exists only for simulator regression. FauxNY has no radio, so it
cannot produce a camera-family current measurement. Do not put a FauxNY result
in a family row.

### 8.4 Simulator coverage

The simulator is an S3-only relative model. Every report says
`"board": "m5stick-s3"`. Running a StickC or Core panel build does not change
the energy model.

| Scenario | Baseline | States covered | What it can say | What it cannot say |
| --- | --- | --- | --- | --- |
| `menu-idle-30s` | 81.23 mA | S02 | Display plus MCU plus PMIC relative estimate | Brightness, real CPU residency, peripherals |
| `connected-idle-30s` | 84.53 mA | S08 | Connected floor plus display | Per-event radio, family traffic |
| `connected-gps-active-30s` | 107.52 mA | S18 | 3.3 V GPS tracking term | 5 V rail, boost efficiency, cold start |
| `gps-duty-standby-cycle` | 102.69 mA | S19 | 14 s tracking plus 1 s standby residency | Real `$PCAS12` current, cold-start cost |
| `screen-dimmed-30s` | 50.63 mA | S04 | 32/255 dim assumption | 48 minimum on StickC and Plus |
| `screen-off-30s` | 0.51 mA | S05 | Light-sleep floor with housekeeping | Wake overhead, real sleep entry |
| `blind-remote-shutter` | 43.65 mA | S10, S12 | Connected no-sleep plus ten presses | Charge per press |
| `intervalometer-5-frames` | 84.73 mA | S13 | Timer and radio event trend | Camera family protocol |
| `gesture-idle-30s` | 9.63 mA | S14 | 50 Hz poll cost trend | Real IMU current |
| `settings-navigation-sweep` | 80.74 mA | S02 variant | UI work trend | Display brightness, input latency |

Not covered at all: S06 scan, S09 saver, S11 connected light sleep, S15
multi-connect, S16 to S22 GPS cold start and rail cycle, S24 power off, S25
Core cutoff, S27 to S35 network, S36 link loss, S37 charging, and every camera
family. A scenario that does not exist cannot produce a gate.

Every connected simulator scenario uses the FauxNY peer. FauxNY has no radio,
so these reports do not represent a real camera family and cannot validate a
family-specific claim.

## 9. Measurement register

The register is the contract. Each row names the state, board scope, expected
value, acceptance threshold, instrument, and evidence. The expected value is a
hypothesis from the source described in the row, not a measurement.

### 9.1 Acceptance classes

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

### 9.2 Derived board expectations

Nominal values in mA at B1, 25 degC, no USB. `ON128` is brightness 128 at CPU
160, the product default. `ON240` is brightness 240 at CPU 80, the model
parity point. `DIM` is the firmware minimum brightness at CPU 160. `OFF` is
screen off and disconnected. `CONN_ON` is connected idle with fast parameters
and screen on. `SAVER_ON` is connected idle with the idle parameter profile and
screen on. `CONN_OFF` is connected, screen off, `sleep_conn` off. `CONN_SLEEP`
is S3 connected, screen off, `sleep_conn` on. `PWR_OFF` is PMIC shutdown.

These are arithmetic sums from the datasheet and estimated entries in
`tools/power-model/board-currents.yaml`. They are `inferred`, not measured.
The CONN_OFF band is deliberately wide because the UI offers `SLEEP_CONN` only
on the S3. On the other boards the baseline block stores it off and `Control`
holds `NO_LIGHT_SLEEP` for the whole connection. Confirm `settings get
sleep_conn` before a run, because the console can store the value on a non-S3
board even though the UI hides it.

| Board | ON128 | ON240 | DIM | OFF | CONN_ON | SAVER_ON | CONN_OFF | CONN_SLEEP | PWR_OFF | CPU80 | CPU160 | CPU240 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| m5stick-c | 51.7 | 54.8 | 42.3 | 1.0 | 56.7 | 52.7 | 25 to 45 | NA | 0.027 | 41.7 | 51.7 | 65.2 |
| m5stick-c-plus | 59.3 | 64.6 | 48.3 | 1.0 | 64.3 | 60.3 | 25 to 45 | NA | 0.027 | 49.3 | 59.3 | 72.8 |
| m5stick-c-plus2 | 59.1 | 64.4 | 45.9 | 0.8 | 64.1 | 60.1 | 25 to 45 | NA | NA | 49.1 | 59.1 | 72.6 |
| m5stack-core | 67.7 | 79.7 | 48.9 | 0 after 32 s | 72.7 | 68.7 | 25 to 45 or cutoff | NA | 0.050 | 57.7 | 67.7 | 81.2 |
| m5stack-core2 | 67.8 | 79.8 | 49.0 | 1.05 | 72.8 | 68.8 | 25 to 45 | NA | 0.027 | 57.8 | 67.8 | 81.3 |
| m5stick-s3 | 80.5 | 79.2 | 67.3 | 0.3 to 0.8 | 83.8 | 81.1 | 30 to 45 | 3.3 to 4.0 | 0.014 | 63.8 | 80.5 | 97.4 |

Notes on the table:

- `ON128` assumes the CPU stays at the configured maximum. DFS may spend time
  at 80 or 40 MHz, so the measured value can be lower. P11 measures the actual
  frequency slope.
- `ON240` for the ESP32 boards uses CPU 80, so it can be lower than `ON128` at
  CPU 160. Compare each column only with the same column.
- The S3 `ON128` total is close to the model's 81.2 mA menu-idle baseline for a
  misleading reason: the model uses CPU 80 and full backlight, while `ON128`
  uses CPU 160 and half backlight. The totals nearly cancel; the component
  split does not. Compare components, not just the total.
- The S3 `ON240` value is 79.2 mA, not 81.2 mA, because 240/255 of the 35 mA
  backlight is 32.9 mA while the model charges the full 35 mA.
- The Core `OFF` value is not a steady state. The IP5306 removes the boost
  after 32 s below 45 mA.
- The Plus2 has no PMIC and no firmware power-off path. `PWR_OFF` is not a
  firmware measurement; record the hardware method used, such as battery
  removal or the board's power control.

### 9.3 Calibration measurements

| ID | State | Board | Expected | Acceptance | Instrument | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| C01 | Instrument zero, open circuit | Bench | 0 A | +/-1 uA after 10 min | I1, I3 | unknown |
| C02 | Gain, 100 ohm 0.01 percent at 3.700 V | Bench | 37.000 mA | +/-0.2 percent against I3 | I1, I3 | unknown |
| C03 | Shunt resistance, 4-wire | Bench | 0.01000 or 10.000 ohm | +/-0.1 percent | I3 | unknown |
| C04 | Burden voltage at maximum state current | Bench | Below 20 mV active, below 100 mV sleep | Hard limit; rerun on a lower shunt | I4 | unknown |
| C05 | Noise floor, 1 Mohm load at 3.700 V (3.7 uA), 10 min | Bench | Below 2 uA RMS | At most 2 uA RMS; else find the source | I1 | unknown |
| C06 | Cross-instrument, same steady load | Bench | I1 and I2 agree | Within 2 percent above 5 mA; within 5 percent below 1 mA | I1, I2 | unknown |
| C07 | Onboard PMIC current vs I1 | AXP192 boards | Within 10 percent after correction | If worse, record the correction and do not use the PMIC for a claim | I1, `power log` | unknown |
| C08 | Timebase, 10 min marker | Bench | 600.0 s | Drift below 0.1 percent | I1, I4 | unknown |
| C09 | Temperature, ice point and room | Bench | 0.0 and 25.0 degC | +/-0.5 degC | I6 | unknown |
| C10 | Model digest and accounting | Simulator | Report digest equals the YAML SHA-256; `model_valid` true; accounting mode matches the baseline | Exact match | `compare.py` | unknown |

### 9.4 Board core states

Board scope `battery boards` means one run on each of SC, SP, S2, CO, C2, and
S3, not an average across boards. WAV is separate.

| ID | State | Board | Expected | Acceptance | Instrument | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| P01 | Menu idle, brightness 128, CPU 160 | battery boards | `ON128` column | T-active | I1 | unknown |
| P02 | Menu idle, brightness 240, CPU 80 | battery boards | `ON240` column | T-active | I1 | unknown |
| P03 | Screen dim, minimum brightness, CPU 160 | battery boards | `DIM` column | T-active | I1 | unknown |
| P04 | Screen off, disconnected, 300 s | battery boards | `OFF` column | T-floor; if the current stays above 20 mA, record a state failure, not a sleep value | I1 | unknown |
| P05 | Disconnected light sleep, 1800 s | battery boards | `OFF` column | T-floor plus stability within 10 percent from minute 5 to minute 30 | I1 | unknown |
| P06 | Connected idle, fast, screen on, 600 s | battery boards | `CONN_ON` column | T-active | I1 | unknown |
| P07 | Connected idle, saver, screen on, 600 s | battery boards | `SAVER_ON` column | T-active | I1 | unknown |
| P08 | Connected, screen off, `sleep_conn` off, 600 s | battery boards | `CONN_OFF` column | T-mid | I1 | unknown |
| P09 | Connected, screen off, `sleep_conn` on, 600 s | S3 | 3.3 to 4.0 mA | T-low | I1 | unknown |
| P10 | Power off, 600 s | SC, SP, CO, C2, S3 | `PWR_OFF` column | T-floor | I1 | unknown |
| P11 | CPU sweep 80, 160, 240 at brightness 128 | battery boards | `CPU80`, `CPU160`, `CPU240` | T-active; each step at least 5 percent above the previous | I1 | unknown |
| P12 | IMU gesture poll, screen off, 300 s | battery boards | S3 8 to 15; ESP32 10 to 25 | T-mid | I1 | unknown |
| P13 | Boot to idle, 30 s | battery boards | 30 to 120 mA transient; no PMIC cutoff | T-event; no reset | I1 | unknown |
| P14 | Scan full, balanced, low, 60 s per mode | battery boards | Above the same-board idle; full greater than balanced, balanced greater than low | T-mid; monotonic | I1 | unknown |
| P15 | Multi-connect, 2 cameras, screen off, 600 s | battery boards | Two connected floors; at most 2x the single-camera radio term | T-mid | I1 | unknown |

P04 and P05 are separate because P04 tests state entry and P05 tests floor
stability. A device that sleeps for 30 s but wakes every minute is not a
30 minute floor.

P04, P05, P08, P12, and P15 on the Core are cutoff tests if the load is below
45 mA. Record the time to boost cutoff and run S25 instead of reporting a
mean.

The UI offers 80, 160, and 240 MHz. The firmware minimum is 40 MHz, but a
40 MHz point needs a code change and is out of scope for this campaign.

### 9.5 Display brightness sweeps

The backlight current per board is one of the two calibration entries that
`tools/power-model/README.md` flags as highest impact. Measure five points per
panel: minimum (48 on SC and SP, 32 on S2, CO, C2, S3), 64, 128, 192, 240.
Panel off is S05 and is not part of the sweep.

| ID | State | Board | Expected | Acceptance | Instrument | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| D01 | Brightness sweep, 5 points | m5stick-c | 42.3 mA at minimum to 64.8 mA at 240 | T-display | I1 | unknown |
| D02 | Brightness sweep, 5 points | m5stick-c-plus | 48.3 to 74.6 mA | T-display | I1 | unknown |
| D03 | Brightness sweep, 5 points | m5stick-c-plus2 | 45.9 to 74.4 mA | T-display | I1 | unknown |
| D04 | Brightness sweep, 5 points | m5stack-core | 48.9 to 89.7 mA | T-display | I1 | unknown |
| D05 | Brightness sweep, 5 points | m5stack-core2 | 49.0 to 89.8 mA | T-display | I1 | unknown |
| D06 | Brightness sweep, 5 points | m5stick-s3 | CPU 160: 67.3 to 95.9 mA; CPU 80: 50.6 to 79.2 mA | T-display | I1 | unknown |

D06 mixes CPU points because the model parity point is CPU 80. Run the sweep
twice, once at CPU 160 for the product state and once at CPU 80 for the model
comparison. Report both.

### 9.6 GPS measurements

Take the GPS term at B1 for board totals, B2 for the Grove 5 V input, and B3 for
the module input. The 31.64 mA vendor figure is B2. The 23 to 25 mA figures are
B3. Do not put a B3 number in a B1 row.

| ID | State | Board | Expected | Acceptance | Instrument | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| G01 | GPS off, screen on 128 | SC, SP, CO, C2, S3 | `ON128` column | T-active | I1 | unknown |
| G02 | GPS acquiring, cold | SC, SP, CO, C2, S3 | `ON128` plus 45 to 55 mA at B1 | T-active | I1 | unknown |
| G03 | GPS tracking, B1 | SC, SP, CO, C2, S3 | `ON128` plus 45 to 55 mA at B1 | T-active | I1 | unknown |
| G04 | GPS tracking, B2 and B3 | S3 and m5stick-c-plus | 31.64 mA at B2; 23 to 25 mA at B3 | T-mid | I1, I4 | unknown |
| G05 | GPS standby 5, 10, 15 s, B1 | S3 and m5stick-c-plus | Between the standby floor and always-on tracking; 5 s expected to save least, 15 s most; record active and standby residency | T-mid plus residency evidence | I1 | unknown |
| G06 | GPS rail cycle, 5 cycles | S3 | Energy per cycle plus a 90 to 130 s cold start | T-event | I1 | unknown |
| G07 | GPS degraded backoff | S3 | 0.5 to 5 mA, screen off, lock released | T-low | I1 | unknown |
| G08 | GPS fix hold, 10 min | S3 | Receiver rail stays in its configured state; delta is the camera geotag write traffic | T-event | I1 | unknown |

G05 is not a simple duty arithmetic. `GPS_DUTY` is the number of seconds asleep
between fix windows, not the active seconds. The active window includes the
NMEA burst and the wake dwell, and the model baseline for duty 5 shows 14 s
tracking and 1 s standby, so an "active = duty" calculation is wrong. Record the
actual active and standby durations from the `gps` cycle state or the current
signature, then compute the average. The expected band stays wide on purpose.

G06 acceptance is two-part: the re-fix time stays within 90 to 130 s, which
matches the 107.7 s local observation in `plans/00-hardware-experiments.md`,
and the charge per cycle is within 20 percent of the sum of the tracking window
and the cold-start window. A rail cycle that looks cheaper because the cold
start is not integrated is a failed measurement.

### 9.7 Sleep and power-off measurements

| ID | State | Board | Expected | Acceptance | Instrument | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| L01 | Core IP5306 cutoff, load below 45 mA | m5stack-core | Boost off 32 to 40 s after the load drops; post-cutoff 0.05 mA | T-state plus T-floor for the post-cutoff value | I1 | unknown |
| L02 | Deep sleep | all | Not implemented; no `esp_deep_sleep` call in the tree | Not measurable | None | unknown |

L02 is deliberately an `unknown` row. Do not invent a deep-sleep number for
plan 19. Measure it only after the implementation exists.

### 9.8 Network measurements

Wi-Fi, Ethernet, and MQTT have no energy model terms. The first measurement
establishes the band; there is no model value to compare against.

| ID | State | Board | Expected | Acceptance | Instrument | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| N01 | Wi-Fi disabled | m5stick-s3 | `OFF` column | T-floor; delta under 0.5 mA against P04 | I1 | unknown |
| N02 | Wi-Fi connected idle, PS min modem | S3, C2 | Not modeled; first measurement establishes the band | T-mid across 3 runs; CV at most 3 percent | I1 | unknown |
| N03 | NTP sync event | S3 | One sync burst; charge not modeled | T-event; 10 repetitions | I1, I8 | unknown |
| N04 | Wi-Fi reconnect after AP loss | S3 | Backoff 1 to 60 s, then connected | T-state; reconnect within 120 s | I1 | unknown |
| N05 | Ethernet link down | waveshare | Not modeled; first measurement establishes the band | T-mid across 3 runs | I7 | unknown |
| N06 | Ethernet link up idle | waveshare | Not modeled; first measurement establishes the band | T-mid across 3 runs | I7 | unknown |
| N07 | MQTT connected idle | S3, C2, waveshare | Not modeled; first measurement establishes the band | T-mid across 3 runs | I1, I7 | unknown |
| N08 | MQTT publish event | waveshare | Not modeled | T-event; 10 repetitions | I7, I8 | unknown |
| N09 | Companion BLE idle | S3 | 3 to 15 mA above the same state without companion | T-mid | I1, I8 | unknown |

N02 and N06 are the first measurements that establish a band. Do not write a
model term from one run. A network state is accepted only after three runs on
two different days, because AP and broker behavior varies more than the DUT.

### 9.9 Camera family measurements

Every row uses the S3 as the radio reference with the screen off. K-FB-02 also
runs on StickC Plus as the AXP192 cross-check. That is the only cross-board
camera row in the first campaign; expand a family only after an S3 anomaly.

| ID | Family | State | Board | Expected | Acceptance | Instrument | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| K-FB-01 | Fujifilm Basic | Handshake | S3 | 2 to 30 s, 0.5 to 5 J | T-state, T-event | I1, I8 | unknown |
| K-FB-02 | Fujifilm Basic | Idle fast then saver | S3; SP | S3 30 to 45 fast, 27 to 44 saver; SP 25 to 45 fast | T-mid | I1 | unknown |
| K-FB-03 | Fujifilm Basic | Shutter single | S3 | 0.2 to 2 mC per press | T-event | I1, I8 | unknown |
| K-FB-04 | Fujifilm Basic | Geotag sync | S3 | 0.02 to 0.2 mC per write; under 0.5 mA average | T-event | I1, I8 | unknown |
| K-FB-05 | Fujifilm Basic | Camera power off | S3 | Disconnect 5 to 10 s; under 2x idle charge | T-state | I1, I8 | unknown |
| K-FS-01 | Fujifilm Secure | Handshake | S3 | 5 to 40 s, 1 to 8 J | T-state, T-event | I1, I8 | unknown |
| K-FS-02 | Fujifilm Secure | Idle fast then saver | S3 | 30 to 45 mA fast; 27 to 44 mA saver | T-mid | I1 | unknown |
| K-FS-03 | Fujifilm Secure | Shutter single | S3 | 0.2 to 2 mC per press | T-event | I1, I8 | unknown |
| K-FS-04 | Fujifilm Secure | Geotag sync | S3 | 0.02 to 0.2 mC per write; under 0.5 mA average | T-event | I1, I8 | unknown |
| K-FS-05 | Fujifilm Secure | Camera power off | S3 | Disconnect 5 to 10 s; under 2x idle charge | T-state | I1, I8 | unknown |
| K-CS-01 | Canon EOS Smart | Handshake | S3 | 2 to 30 s, 0.5 to 5 J | T-state, T-event | I1, I8 | unknown |
| K-CS-02 | Canon EOS Smart | Idle fast then saver | S3 | 30 to 45 mA fast; 27 to 44 mA saver | T-mid | I1 | unknown |
| K-CS-03 | Canon EOS Smart | Shutter single | S3 | 0.2 to 2 mC per press | T-event | I1, I8 | unknown |
| K-CS-04 | Canon EOS Smart | Geotag sync | S3 | 0.02 to 0.2 mC per write | T-event | I1, I8 | unknown |
| K-CS-05 | Canon EOS Smart | Camera power off | S3 | Disconnect 5 to 10 s | T-state | I1, I8 | unknown |
| K-CR-01 | Canon EOS Remote | Handshake | S3 | 2 to 30 s, 0.5 to 5 J | T-state, T-event | I1, I8 | unknown |
| K-CR-02 | Canon EOS Remote | Idle fast then saver | S3 | 30 to 45 mA fast; 27 to 44 mA saver | T-mid | I1 | unknown |
| K-CR-03 | Canon EOS Remote | Shutter single | S3 | 0.2 to 2 mC per press | T-event | I1, I8 | unknown |
| K-CR-04 | Canon EOS Remote | Geotag sync | S3 | Not supported | T-state | I1 | unknown |
| K-CR-05 | Canon EOS Remote | Camera power off | S3 | Disconnect 5 to 10 s | T-state | I1, I8 | unknown |
| K-NK-01 | Nikon | Handshake | S3 | 2 to 30 s, 0.5 to 5 J | T-state, T-event | I1, I8 | unknown |
| K-NK-02 | Nikon | Idle fast then saver | S3 | 30 to 45 mA fast; 27 to 44 mA saver | T-mid | I1 | unknown |
| K-NK-03 | Nikon | Shutter single | S3 | 0.2 to 2 mC per press | T-event | I1, I8 | unknown |
| K-NK-04 | Nikon | Geotag sync | S3 | Not supported in remote mode | T-state | I1 | unknown |
| K-NK-05 | Nikon | Camera power off | S3 | Disconnect 5 to 10 s | T-state | I1, I8 | unknown |
| K-SO-01 | Sony | Handshake | S3 | 2 to 30 s, 0.5 to 5 J | T-state, T-event | I1, I8 | unknown |
| K-SO-02 | Sony | Idle fast then saver | S3 | 30 to 45 mA fast; 27 to 44 mA saver | T-mid | I1 | unknown |
| K-SO-03 | Sony | Shutter single | S3 | 0.2 to 2 mC per press | T-event | I1, I8 | unknown |
| K-SO-04 | Sony | Geotag sync | S3 | 0.02 to 0.2 mC per write | T-event | I1, I8 | unknown |
| K-SO-05 | Sony | Camera power off | S3 | Disconnect 5 to 10 s | T-state | I1, I8 | unknown |
| K-RI-01 | Ricoh | Handshake | S3 | 2 to 30 s, 0.5 to 5 J | T-state, T-event | I1, I8 | unknown |
| K-RI-02 | Ricoh | Idle fast then saver | S3 | 30 to 45 mA fast; 27 to 44 mA saver | T-mid | I1 | unknown |
| K-RI-03 | Ricoh | Shutter single | S3 | 0.2 to 2 mC per press | T-event | I1, I8 | unknown |
| K-RI-04 | Ricoh | Geotag sync | S3 | 0.02 to 0.2 mC per write | T-event | I1, I8 | unknown |
| K-RI-05 | Ricoh | Camera power off | S3 | Announced power state; disconnect within 10 s | T-state | I1, I8 | unknown |
| K-LX-01 | Lumix | Handshake | S3 | 2 to 30 s, 0.5 to 5 J | T-state, T-event | I1, I8 | unknown |
| K-LX-02 | Lumix | Idle fast then saver | S3 | 30 to 45 mA fast; 27 to 44 mA saver | T-mid | I1 | unknown |
| K-LX-03 | Lumix | Shutter single | S3 | 0.2 to 2 mC per press | T-event | I1, I8 | unknown |
| K-LX-04 | Lumix | Geotag sync | S3 | 0.02 to 0.2 mC per 16 byte write | T-event | I1, I8 | unknown |
| K-LX-05 | Lumix | Camera power off | S3 | Disconnect 5 to 10 s | T-state | I1, I8 | unknown |
| K-DJ-01 | DJI Osmo | Handshake | S3 | 2 to 30 s, 0.5 to 5 J | T-state, T-event | I1, I8 | unknown |
| K-DJ-02 | DJI Osmo | Idle fast then saver | S3 | 30 to 45 mA fast; 27 to 44 mA saver | T-mid | I1 | unknown |
| K-DJ-03 | DJI Osmo | Shutter single | S3 | Record toggle; 0.2 to 2 mC per press | T-event | I1, I8 | unknown |
| K-DJ-04 | DJI Osmo | Geotag sync | S3 | Not supported | T-state | I1 | unknown |
| K-DJ-05 | DJI Osmo | Camera power off | S3 | Disconnect 5 to 10 s | T-state | I1, I8 | unknown |

K-05 acceptance is `T-state` for the disconnect time plus `T-event` for the
charge across the timeout window. The table lists `T-state` alone to keep the
row short.

The 30 to 45 mA fast band is the S3 screen-off connected state with
`sleep_conn` off. It is not a family-specific number. The family claim is the
difference between families and the charge per event, not the absolute floor.

The Nikon rows use remote mode. Smart mode is not a valid bench state because
it does not pair, per `docs/supported-hardware.md`.

Focus is a sub-case of the shutter event and is measured only where a family
uses a distinct focus write with a measurable charge. Reconnect is the
handshake row plus the BLE reconnect backoff (`recon_backoff`) and the
link-loss row. Multi-connect is a board state S15, not a family state, because
the family protocol does not change when a second camera is added. Secure
pairing is part of the handshake row, not a separate state.

### 9.10 Battery discharge measurements

| ID | State | Board | Expected | Acceptance | Instrument | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| B01 | Mixed 4 h profile, then full discharge | m5stick-c | 95 mAh cell; 1.8 h at 51.7 mA | T-discharge | I5 | unknown |
| B02 | Mixed 4 h profile, then full discharge | m5stick-c-plus | 120 mAh cell; 2.0 h at 59.3 mA | T-discharge | I5 | unknown |
| B03 | Mixed 4 h profile, then full discharge | m5stick-c-plus2 | 200 mAh cell; 3.4 h at 59.1 mA | T-discharge | I5 | unknown |
| B04 | Mixed 4 h profile, then full discharge | m5stack-core | Cell capacity unknown; runtime only after capacity is measured | T-discharge | I5 | unknown |
| B05 | Mixed 4 h profile, then full discharge | m5stack-core2 | 390 mAh cell; 5.8 h at 67.8 mA | T-discharge | I5 | unknown |
| B06 | Mixed 4 h profile, then full discharge | m5stick-s3 | 250 mAh cell; 3.1 h at 80.5 mA | T-discharge | I5 | unknown |

The runtimes are arithmetic from the firmware capacity constant and the
`ON128` expectation. They are not measured runtime claims. The discharge test
replaces them with measured `mAh / mean current`.

## 10. Acceptance, uncertainty, and promotion

### 10.1 What acceptance means

Acceptance has three separate parts, and a row can pass one and fail another:

1. **Validity.** The state, boundary, instrument range, burden, temperature,
   charging state, and firmware identity are all correct. A row that fails
   validity is `unknown`, not a low measurement.
2. **Repeatability.** The three runs meet the T-class CV and range. A row that
   fails repeatability is noisy and goes to section 15.
3. **Model agreement.** The measured value is compared with the expected band
   and with the model. Disagreement is a model finding, not a failed
   measurement.

### 10.2 Expected band versus acceptance

The expected band catches a wrong state, for example a screen-off run that is
still at 40 mA because the panel never slept. It is not a pass/fail band for
the model. The acceptance threshold in the register is the repeatability and
validity contract. The model comparison is reported separately.

### 10.3 Model agreement tiers

| Tier | Criterion | What it supports |
| --- | --- | --- |
| M0 | Model only, no bench | Relative ranking, CI regression |
| M1 | Bench measured, model not compared | A measured state, no model promotion |
| M2 | Bench and model sign agree, magnitude within 30 percent or within the YAML min/max band | A reviewed model correction, still not a default change |
| M3 | M2 plus three runs on two days, two boards where applicable, and a second instrument | `measured-local` model entry and a default-change proposal |

### 10.4 From a modeled saving to a measured saving

A saving is a difference, not a state. Use a paired design: baseline and
changed state on the same board, same camera, same firmware, same ambient, in
alternating order.

Definitions:

```text
Delta_model = model_baseline_mA - model_changed_mA
Delta_bench = median_baseline_mA - median_changed_mA
sigma_delta = sqrt(sigma_baseline^2 + sigma_changed^2)
noise_floor = 3 * RMS noise of the instrument on a stable load
```

A saving must also have a nonzero model delta. If the model predicts no change,
there is no modeled saving to promote. The saving is promoted only when all
five conditions hold:

1. `Delta_bench > max(3 * sigma_delta, 0.05 * median_baseline, 2 * noise_floor)`.
2. `sign(Delta_bench) == sign(Delta_model)`.
3. `abs(Delta_bench - Delta_model) <= max(0.30 * abs(Delta_model), model_band)`,
   where `model_band` is the YAML `max - min` for the changed component. If the
   component has no band, use 30 percent.
4. The state definitions are identical, including brightness, CPU setting,
   sleep policy, camera family, GPS policy, and link parameters.
5. The record has three accepted runs and a raw trace hash.

If condition 1 fails, the saving is below the noise floor. Report it as
`unknown`, not as a small saving.

If condition 2 fails, the model or the state definition is wrong. Do not change
the default. File a model correction with the trace.

If condition 3 fails but the sign agrees, the model magnitude is wrong. Add the
measurement to the record and correct the YAML entry in a separate review.

When the gate passes, the saving gets the label `measured` only for the exact
board, state, firmware, and camera family in the record. A second board needs
its own run. This is an M2 result. A `measured-local` YAML entry or a default
change still needs M3, which adds a second day, a second board where applicable,
and a second instrument.

### 10.5 What cannot be promoted

- A single run.
- A run with USB attached.
- A run that mixes B1 and B2 or B3.
- A run on a different firmware commit or a different camera firmware.
- A run with a different brightness, CPU setting, or sleep policy.
- A FauxNY result presented as a camera family result.
- A model report with a missing or mismatched digest.
- A run that failed validity but produced a plausible number.
- A delta that is smaller than the instrument noise floor.
- A default change supported only by the S3 when the setting is compiled for
  all boards.

## 11. Calibration procedure

Run this procedure at the start of every bench campaign, and again after any
instrument, shunt, cable, or fixture change. Record every step in the session
header.

### 11.1 Instrument and shunt

1. Power the instrument for 30 minutes. Zero it with the load open. Record the
   offset.
2. Measure the shunt with I3 in 4-wire mode. Record the value and tolerance.
   A 0.01 ohm shunt that reads 0.01012 ohm is a 1.2 percent error and must be
   corrected or replaced.
3. Connect a 100 ohm, 0.01 percent load to a stable 3.700 V source. Measure
   the current with I3 and I1. They must agree within 0.2 percent.
4. Set the active shunt. Apply the highest expected state current. Measure the
   shunt voltage with I4. Confirm the burden is below 20 mV. Repeat for the
   sleep shunt and confirm below 100 mV.
5. Run a 10 minute capture on a 1 Mohm load at 3.700 V, which draws 3.7 uA.
   Compute RMS and peak-to-peak. Record the noise floor. If RMS is above
   2 uA, find the source before continuing.
6. Run the same steady load on I1 and I2. Record the agreement. If they differ
   by more than 2 percent above 5 mA, stop and fix the setup.

### 11.2 Thermal and timebase

1. Place the ambient sensor away from the DUT and the instrument exhaust.
2. Place the board sensor on the PMIC or the panel edge, not on the cell.
3. Check the timebase with a 1 Hz marker for 10 minutes. Record the count.
4. Soak the DUT for 20 minutes in the state before the first capture.

### 11.3 Onboard current cross-check

On AXP192 boards, the console `power log 10` prints `current_ma` and
`current_ewma_ma`. This is the PMIC current, not a reference.

1. Run a stable load at three points, for example 5, 20, and 50 mA.
2. Compare the PMIC value with I1.
3. If the error is constant, record the offset. If it is proportional, record
   the gain. Do not apply a correction to a claim without stating it.
4. On the S3, Core, and Plus2, `power log` has no current backend. It prints
   `na` and falls back to percent per hour. That is not a current measurement.
   Do not use it as one.

### 11.4 Model pre-flight

Before any model comparison:

```sh
shasum -a 256 tools/power-model/board-currents.yaml
```

Run one scenario as in section 17.3, then check the report:

```sh
python3 - <<'PY'
import json
report = json.load(open(".pio/furble-power-reports/connected-idle-30s.json"))
print("estimate_kind:", report["estimate_kind"])
print("model_source:", report["model_source"])
print("model_digest:", report["model_digest"])
print("model_valid:", report["model_valid"])
print("accounting_mode:", report["energy"].get("accounting_inputs", {}).get("accounting_mode"))
PY
```

The digest must equal `sha256:` plus the YAML SHA-256. The accounting mode must
match the baseline, or `compare.py` will reject the comparison with exit 2. A
legacy-unaccounted report has no `accounting_mode` key, so the command prints
`None`; `compare.py` classifies it as `legacy-unaccounted`. Do not mix a legacy
report with a synthetic-accounting baseline.

## 12. Battery discharge tests

### 12.1 Cell reference

1. Charge the cell to 4.20 V +/-0.02 V with a 0.5 C constant-current,
   constant-voltage profile. Taper until the current is below 0.05 C.
2. Rest for 60 minutes at 20 to 25 degC.
3. Discharge at 0.2 C to 3.30 V with I5. Record the delivered mAh and Wh.
   This is the usable cell capacity for the runtime arithmetic.
4. Repeat once. If the two capacities differ by more than 5 percent, the cell
   is not qualified for a runtime claim.

### 12.2 Device profile

The mixed profile is:

```text
10 min  S02  menu idle, brightness 128
10 min  S08  connected, fast, screen on
10 min  S10  connected, screen off, sleep off
10 min  S18  GPS tracking, screen off
repeat for 4 h or until cutoff
```

For a full discharge, continue the profile until the device cuts off at 3.30 V
or the PMIC cutoff, whichever comes first. Record the end state and the end
voltage.

### 12.3 Integration and runtime

Use the raw current and voltage:

```text
charge_mAh = integral(I dt) / 3600      # I in mA, t in seconds
energy_mWh = integral(V * I dt) / 3600  # V in volts, I in mA, t in seconds
runtime_h = usable_capacity_mAh / mean_current_mA
```

- The mean current is time-weighted, not the arithmetic mean of state means.
- Report the measured usable capacity, the mean current, the runtime, and the
  end voltage.
- Compare the runtime with `usable_capacity / model_mA` only after the model
  has an M2 or M3 result for the same states.

### 12.4 Core and Plus2 special cases

- **Core.** The IP5306 cuts the boost below 45 mA after 32 s. A mixed profile
  that drops below 45 mA ends in cutoff, not in a low-current state. Record
  the cutoff time and the post-cutoff current. A "Core sleep runtime" claim is
  not available.
- **Plus2.** No PMIC current sense and no charging telemetry. The discharge
  fixture must be the only source. The firmware capacity constant is 200 mAh,
  but the measured cell capacity is the number to use.
- **S3.** The M5PM1 has no current register. The discharge fixture is the only
  current path. The PMIC off current is a separate measurement (P10).

### 12.5 Runtime claims

A runtime claim requires:

- A measured usable capacity, not the firmware constant.
- A measured mean current for the exact state or profile.
- A measured end voltage.
- A stated temperature and cell age.
- A statement that the runtime excludes self-discharge and cell aging.

A runtime calculated from a nameplate capacity and a modeled current is
`inferred`, not measured.

## 13. Thermal effects

### 13.1 Sources and limits

| Source | Effect | Control |
| --- | --- | --- |
| Backlight | 30 to 50 mA at 3.3 V, panel edge heating | 20 minute soak; measure panel edge |
| PMIC | Conversion loss, 50 to 300 uA quiescent plus load | Measure PMIC case, not cell |
| Radio | Burst heating at 130 to 240 mA peaks | Keep duty cycle fixed during a comparison |
| Cell | Internal resistance and capacity change with temperature | Rest 60 min; measure cell surface |
| Enclosure | Traps heat and raises all of the above | Test the final enclosure, not a bare board |

Limits:

- Ambient 20 to 25 degC for a standard run.
- Board temperature drift at most 2 degC over the capture window.
- Ambient drift at most 3 degC over the capture window.
- Cell surface at most 45 degC during discharge.
- No charging during a thermal run.

If a limit is exceeded, the run is invalid. Do not apply a temperature
correction to make it pass. Report the coefficient and rerun in a chamber.

### 13.2 Thermal matrix

| ID | State | Board | Condition | Acceptance | Instrument | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| T01 | S02 menu 128 | m5stick-s3 | 0 degC chamber | Report current coefficient; no pass band | I1, I6 | unknown |
| T02 | S02 menu 128 | m5stick-s3 | 25 degC chamber | Report current coefficient; repeat 3 runs | I1, I6 | unknown |
| T03 | S02 menu 128 | m5stick-s3 | 40 degC chamber | Report current coefficient; no pass band | I1, I6 | unknown |
| T04 | S05 screen off | m5stick-s3 | 25 degC chamber | Report sleep-floor coefficient | I1, I6 | unknown |
| T05 | S02 brightness 240 vs 32 | m5stick-s3 | 25 degC room | Panel edge delta; T-display for current | I1, I6 | unknown |

The thermal matrix is a sensitivity study, not a product qualification. It
answers whether a 5 degC bench swing can explain a measured delta before the
delta is called a saving.

## 14. Repeatability and uncertainty

### 14.1 Run count and order

- Three runs minimum per state. Five runs for a promoted model entry.
- Each run starts from a cold boot and a fresh connection.
- Alternate baseline and changed states: A, B, A, B, A, B. Do not run all A
  then all B, because temperature and cell voltage drift with time.
- Randomize the board order across a session. Do not always measure the S3
  first.
- Repeat the reference state S02 at the start and end of every session. If the
  two references differ by more than 5 percent, invalidate the session.

### 14.2 Uncertainty budget

| Source | Typical contribution | How to bound it |
| --- | --- | --- |
| Instrument gain | 0.2 to 0.5 percent | Calibration certificate and C02 |
| Shunt tolerance | 0.1 percent | 4-wire measurement, C03 |
| Burden and lead resistance | 0.2 percent | C04 and short leads |
| Timebase | 0.1 percent | C08 |
| Temperature | 1 to 3 percent | T01 to T04; reject over 2 degC drift |
| State repeatability | 2 to 10 percent | Three runs, CV |
| Camera or AP variability | 5 to 20 percent | Fixed peer, fixed channel, two days |

Report the combined U95 for each state. For active states the target is 5
percent or better. For sleep states the target is 15 percent or better. If the
U95 is worse than the T-class, the state is not resolved and the row stays
`unknown`.

### 14.3 Environmental controls

- Same room, same bench, same camera distance (1.0 m), same orientation.
- Camera battery above 50 percent. Record the camera battery level.
- Phone off or in airplane mode. Wi-Fi AP on a fixed channel, 2 m away, or off,
  except during the Wi-Fi rows where the AP is the state under test.
- No USB charger on the bench during a B1 run.
- Same enclosure and strap. An enclosure changes the thermal and antenna
  environment.
- Record the firmware commit, build flags, and settings SHA-256. A different
  build is a different DUT.

## 15. Failure protocol

### 15.1 Fault classes

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

### 15.2 Decision tree

```text
Measurement out of band
  |
  +-- Is the state correct? -- no --> F5, fix, rerun
  |
  +-- Is the boundary correct? -- no --> F4 or F6, fix, rerun
  |
  +-- Is the temperature stable? -- no --> F7, rerun
  |
  +-- Is the noise above budget? -- yes --> F1 or F2, fix, up to 3 attempts
  |
  +-- Is the model digest correct? -- no --> F8, fix, do not compare
  |
  +-- Otherwise the measurement stands; compare with the expected band
```

### 15.3 Escalation

If three attempts fail, or two different instruments disagree beyond their
budgets:

1. Mark the row `unknown`.
2. Preserve the raw trace and the session record.
3. Open an issue with the state ID, board, instrument, trace hash, and the
   failure class.
4. Do not change a default, a baseline, or a model entry while the row is
   `unknown`.
5. Do not delete or overwrite a rejected trace. A rejected trace is evidence
   that the setup was wrong.

### 15.4 Never do this

- Never average an invalid run into a valid one.
- Never change `compare.py --threshold` to make a regression pass.
- Never replace a baseline with a run that failed validity.
- Never call a single run a repeatable result.
- Never call a datasheet value a local measurement.
- Never use the console build for a B1 current claim.
- Never measure with USB attached and call it battery current.
- Never compare a B1 measurement with a B3 model term without a conversion.

## 16. Data recording template

### 16.1 Record IDs

Use `PWR-<date>-<board>-<state>-R<run>`.

```text
PWR-<YYYYMMDD>-<board>-<state>-R<run>
```

Examples of the shape, not measurements:

```text
PWR-<YYYYMMDD>-m5stick-s3-S02-R1
PWR-<YYYYMMDD>-m5stick-c-plus-K-FB-03-R2
PWR-<YYYYMMDD>-waveshare-s3-eth-S32-R3
```

### 16.2 Session header

```csv
record_id,date_utc,operator,board,board_serial,firmware_version,git_commit,build_env,pmic,panel,cell_serial,cell_capacity_mah,camera_family,camera_model,camera_firmware,gps_unit,gps_serial,instrument,instrument_serial,cal_date,shunt_ohms,shunt_tolerance,sample_rate_sps,range,ambient_c,board_temp_c,usb_connected,charging,settle_s,capture_s,state_id,settings_sha256,raw_trace,raw_trace_sha256,model_report,model_digest,notes
```

### 16.3 Per-run rows

```csv
run_id,record_id,run_index,start_utc,duration_s,mean_ma,median_ma,p95_ma,rms_noise_ma,charge_mc,energy_mj,temp_start_c,temp_end_c,state_observed,valid,invalid_reason
```

### 16.4 Summary rows

```csv
state_id,board,n_runs,median_ma,min_ma,max_ma,cv_pct,u95_ma,model_ma,model_delta_pct,expected_band,verdict,evidence_label
```

### 16.5 Artifact layout

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

Raw traces stay in the approved lab storage, not in git. Commit the summary,
the settings dump, the model report, the trace hash, and the notes. The record
directory does not exist yet; it is created by the first accepted run.

### 16.6 Settings dump

Capture the settings before flashing the release image:

```text
settings list
```

Save the output as `settings.txt`. Compute its SHA-256 and put it in the
session header. The settings are part of the DUT identity.

### 16.7 Evidence package checklist

- Record ID and date.
- Board, serial, PMIC, panel, cell.
- Firmware version, commit, build env, build flags.
- Settings dump and SHA-256.
- Camera family, model, firmware, battery level.
- GPS unit and serial, if used.
- Instrument, serial, calibration date.
- Shunt value and tolerance.
- Sample rate, range, settle, capture.
- Ambient and board temperature at start and end.
- Raw trace path and SHA-256.
- Model report and digest, if compared.
- Verdict and evidence label.
- Failure class and rerun count, if any.

## 17. Simulator model commands

The `Power model gate` workflow exists in this tree at
`.github/workflows/power-gate.yml`. Plan 98's statement that the CI gate is not
wired is stale. The commands below run the same selection locally.

### 17.1 Preconditions

The simulator needs clang, SDL2, M5GFX, M5Unified, TinyGPSPlus, and LVGL. The
default dependency root is `.pio/libdeps/m5stick-s3`; the workflow uses
`sim/.pio/libdeps/sim`. Set both:

```sh
export FURBLE_DEP_ROOT="$PWD/sim/.pio/libdeps/sim"
export FURBLE_LVGL_DIR="$PWD/sim/.pio/libdeps/sim/lvgl"
```

If the dependencies are missing, use the pinned clones from
`.github/workflows/power-gate.yml`.

### 17.2 Build

```sh
python3 tools/gen_lv_conf.py sdkconfig.m5stick-s3 sim/lv_conf.h
sh sim/build.sh
```

The output is `sim/build/furble-sim`.

### 17.3 Run one scenario

```sh
REPORT_DIR="$PWD/.pio/furble-power-reports"
mkdir -p "$REPORT_DIR"

FURBLE_POWER_MODEL="$PWD/tools/power-model/board-currents.yaml" \
  SDL_VIDEODRIVER=dummy \
  SDL_AUDIODRIVER=dummy \
  ./sim/build/furble-sim \
  --script sim/scenarios/connected-idle-30s.txt \
  --report-dir "$REPORT_DIR"

python3 tools/power-model/compare.py \
  "$REPORT_DIR/connected-idle-30s.json" \
  tools/power-model/baseline/connected-idle-30s.json
```

A passing line prints the scenario, both currents, the delta, and `PASS`.

### 17.4 Run every power-gate scenario

```sh
set -e
REPORT_DIR="$PWD/.pio/furble-power-reports"
mkdir -p "$REPORT_DIR"

for scenario in $(python3 tools/check_sim_scenarios.py \
    --list-certified --suite power-gate --board m5stick-s3); do
  name=$(basename "$scenario" .txt)
  FURBLE_POWER_MODEL="$PWD/tools/power-model/board-currents.yaml" \
    SDL_VIDEODRIVER=dummy \
    SDL_AUDIODRIVER=dummy \
    ./sim/build/furble-sim \
    --script "$scenario" \
    --report-dir "$REPORT_DIR"
  python3 tools/power-model/compare.py \
    "$REPORT_DIR/$name.json" \
    "tools/power-model/baseline/$name.json"
done
```

This is the same selection the `Power model gate` workflow uses. Each scenario
also writes `setup.json`; compare only the named report.

### 17.5 Compare with an explicit threshold

```sh
python3 tools/power-model/compare.py \
  "$REPORT_DIR/connected-idle-30s.json" \
  tools/power-model/baseline/connected-idle-30s.json \
  --threshold 0.02
```

The default is 0.10. The comparison is two-sided, so a large decrease is also
reported. A custom threshold is for a local experiment, not for CI. Do not
raise the threshold to hide a regression.

### 17.6 Verify the model identity

```sh
python3 - <<'PY'
import hashlib
import json
from pathlib import Path

report = json.loads(Path(".pio/furble-power-reports/connected-idle-30s.json").read_text())
yaml = Path("tools/power-model/board-currents.yaml").read_bytes()
print("model_source:", report["model_source"])
print("model_digest:", report["model_digest"])
print("file_digest:", "sha256:" + hashlib.sha256(yaml).hexdigest())
print("model_valid:", report["model_valid"])
print("estimate_kind:", report["estimate_kind"])
PY
```

The two digests must match. A mismatch means the report used a different model
than the one on disk. Do not compare that report.

### 17.7 Determinism check

```sh
A="$PWD/.pio/furble-power-reports/a"
B="$PWD/.pio/furble-power-reports/b"
mkdir -p "$A" "$B"

FURBLE_POWER_MODEL="$PWD/tools/power-model/board-currents.yaml" \
  SDL_VIDEODRIVER=dummy ./sim/build/furble-sim \
  --script sim/scenarios/screen-off-30s.txt --report-dir "$A"

FURBLE_POWER_MODEL="$PWD/tools/power-model/board-currents.yaml" \
  SDL_VIDEODRIVER=dummy ./sim/build/furble-sim \
  --script sim/scenarios/screen-off-30s.txt --report-dir "$B"

python3 tools/power-model/compare.py \
  "$A/screen-off-30s.json" "$B/screen-off-30s.json" --threshold 0
```

A nonzero exit means the two runs differ. Record the host load and rerun in
isolation. Do not use a nondeterministic run for a gate.

### 17.8 Bench versus model

The model report and the bench record are different evidence. Compare them
only after the state, boundary, brightness, CPU setting, and camera family
match.

```sh
python3 - <<'PY'
import json
from pathlib import Path

report = json.loads(Path(".pio/furble-power-reports/connected-idle-30s.json").read_text())
model_ma = report["energy"]["estimated_mA"]
measured_ma = float("<accepted median from the record>")
print(f"model={model_ma:.3f} mA")
print(f"measured={measured_ma:.3f} mA")
print(f"delta={(measured_ma - model_ma) / model_ma:+.1%}")
PY
```

The angle-bracket value is a placeholder. Do not paste a single sample into it
and call the result measured. Use the median of the accepted runs.

### 17.9 What the simulator cannot answer

- Absolute current or runtime.
- Non-S3 boards. Every report is `board: m5stick-s3`.
- Brightness-dependent display current.
- Per-event BLE charge, connection interval scaling, PHY, retries, or camera
  family traffic.
- GPS 5 V rail, boost efficiency, cold start, or in-circuit `$PCAS12` current.
- Scan, advertising, reconnect backoff, Wi-Fi, Ethernet, MQTT, or companion.
- Power off, Core cutoff, thermal behavior, cell aging, or battery capacity.
- Peripheral current. The model's `peripheral` term is a fixed 0.0035 and does
  not read the YAML `peripherals` block.

## 18. Execution order and exit criteria

| Phase | Work | Depends on | Exit criterion |
| --- | --- | --- | --- |
| 0 | C01 to C10, bench qualification | Instruments and fixtures | All calibration rows pass or are explicitly waived |
| 1 | P01 to P15, D01 to D06 | Phase 0 | Board floors, scan, multi-connect, and display slopes recorded; Core cutoff recorded |
| 2 | P04, P05, P08 to P10, L01 | Phase 1 | Sleep and off states classified; no unresolved state ambiguity |
| 3 | P06, P07, K-FB to K-DJ | Phase 1 | Camera family bands and per-event charge recorded |
| 4 | G01 to G08 | Phase 1 | GPS B1, B2, B3 separated; cold start and standby measured |
| 5 | N01 to N09 | Phase 1 | Network bands established; no model claim made |
| 6 | B01 to B06, T01 to T05 | Phase 2 | Capacity, runtime, and thermal sensitivity recorded |
| 7 | Model promotion and doc update | Phases 1 to 6 | M2 or M3 evidence recorded; YAML changes reviewed separately |

Phase 1 is the minimum useful campaign. It answers whether the screen, MCU, and
PMIC expectations are even close, and it finds the states where the model and
the board disagree before the expensive camera and discharge work.

## 19. Opportunity traceability

The ranked matrix is `docs/power/opportunity-matrix.md`. The IDs below are the
bridge from each opportunity to the register. When a matrix row is populated,
it inherits the measurement IDs and closure criterion from this table.

| Opportunity | Source | Measurement or state IDs | Closure criterion |
| --- | --- | --- | --- |
| O1 Battery saver profile | plans/98 action 1 | P04, P05, P07, P08, P09, P14, K-05 | Measured delta for each setting, not a bundle average |
| O2 Power gate CI | plans/98 action 2 | Section 17 | Local loop passes and the workflow is green |
| O3 Interval deep sleep | plans/98 action 3 | L02, B06 | Blocked until deep sleep exists; then measure sleep and wake energy |
| O4 GPS lock on all boards | plans/98 action 4 | G02 to G06 on ESP32 boards | Baseline measured first; after the code change, lock release and current drop measured during standby |
| O5 Auto off and low battery | plans/98 action 5 | P10, L01, B01 to B06 | Power-off current and cutoff behavior measured |
| O6 Connected idle with saver | plans/98 action 6 | P07, P08, K-*-02 | Saver delta above the noise floor |
| O7 GPS duty rework | plans/98 action 7 | G05 to G08 | Duty saving and cold-start cost measured together |
| O8 APB lock scope | plans/98 action 8 | P01 to P05, D01 to D06 | MCU term isolated; flicker test passes |
| O9 Screen-off housekeeping | plans/98 action 9 | P04, P05, P08, P12 | Housekeeping cost measured against the floor |
| O10 Peripheral sweep | plans/98 action 10 | P10, P12, D01 to D06 | Rails and suspend states measured |
| O11 Calibration debt | plans/98, model README | D01 to D06, G05, P08, C01 to C10 | YAML entries move to `measured-local` |
| O12 BLE link efficiency | plans/98 missing plans | P06 to P09, K-FB to K-DJ | Interval, PHY, and per-family charge recorded |
| O13 Wi-Fi and network | plans/98, FurbleWiFi | N01 to N09 | Bands established; model term decision recorded |
| O14 Runtime and thermal | plans/98 | B01 to B06, T01 to T05 | Measured capacity, runtime, and temperature sensitivity |

## 20. Non-claims and open limits

- No measurement in this document has been made. The register is `unknown`
  from top to bottom.
- The simulator is an S3-only relative model. It is not a hardware measurement
  and it does not establish physical parity.
- The model does not read the `peripherals` block. The 0.685 mA BMI270 figure
  and the other peripheral entries do not reach the model.
- Deep sleep is not implemented. There is no `esp_deep_sleep` call in the tree.
- The Core cannot hold a steady state below 45 mA. Its sleep behavior is a
  PMIC cutoff, not a firmware sleep state.
- Wi-Fi, Ethernet, MQTT, and companion have no model terms. Their rows establish
  bands; they do not validate a model.
- The camera family matrix is an evidence plan. Only Fujifilm has hardware test
  coverage today; the other families are code-reviewed and FauxNY-tested. A
  family without hardware on the bench stays `unknown`; do not substitute
  FauxNY, a code review, or another vendor's camera for that measurement.
- FauxNY is a software peer. It cannot produce a camera family measurement.
- The console build distorts power. It is not used for a B1 claim.
- The 3.3 mA S3 connected floor is a published or previously observed figure,
  not a measurement made for this plan. Plan 98 explicitly says it is not
  trace-equivalent. P09 either reproduces it or replaces it.
- The YAML entry `ble_connected_idle_floor` is tagged `measured-local` and cites
  `plans/00-hardware-experiments.md`, but that file records a boot-log crystal
  result and a GPS refix time, not a current trace. Do not treat the 3.3 mA
  value as a local measurement until P09 produces one.
- The 14.1 mA ESP32 modem-sleep figure is a published example figure, not a
  local measurement. On non-S3 boards, with the baseline block's `sleep_conn`
  off, `Control` holds `NO_LIGHT_SLEEP` for the whole connection, so P08
  measures the actual floor rather than assuming it. A run that stores
  `sleep_conn` on is a different state and must be recorded as such.
- The battery capacities in `src/FurblePlatform.cpp` are firmware constants for
  the runtime estimate. They are not measured cell capacities.
- Waveshare hardware verification remains outstanding. Its Ethernet and MQTT
  rows are first measurements, not validation of an existing claim.
- No RF, EMC, safety, or regulatory claim is supported by this plan.

## 21. References

Repository documents:

- `plans/98-power-optimization-audit.md`
- `plans/98b-power-followup.md`
- `plans/158-sim-scheduler-parity.md`
- `plans/00-hardware-experiments.md`
- `plans/63-sim-power-analysis.md`
- `plans/12-display-off.md`
- `plans/13-auto-off-low-batt.md`
- `plans/15-gps-power.md`
- `plans/19-interval-deep-sleep.md`
- `plans/26-pm1-watchdog.md`
- `plans/32-gps-advanced.md`
- `plans/77-battery-saver-profile.md`
- `tools/power-model/README.md`
- `tools/power-model/board-currents.yaml`
- `tools/power-model/compare.py`
- `tools/power-model/baseline/`
- `sim/scenarios/manifest.json`
- `sim/power_profiler.cpp`
- `sim/power_profiler.h`
- `.github/workflows/power-gate.yml`
- `docs/console-commands.md`
- `docs/sim.md`
- `docs/settings-and-controls.md`
- `docs/supported-hardware.md`
- `src/FurblePlatform.cpp`
- `src/FurbleControl.cpp`
- `src/FurbleConsole.cpp`
- `src/FurbleGPS.cpp`
- `src/FurbleWiFi.cpp`
- `platformio.ini`
- `sdkconfig.m5stick-c`
- `sdkconfig.m5stick-s3`
- `sdkconfig.m5stack-core`
- `sdkconfig.m5stack-core2`
- `sdkconfig.waveshare-s3-eth`

Datasheet and vendor sources are listed with their URLs in
`tools/power-model/board-currents.yaml`. The key parts are the ESP32 and
ESP32-S3 series datasheets, ST7735S, ST7789V, ILI9342C, AXP192, IP5306, M5PM1,
BMI270, MPU-6886, ATGM336H and AT6558, W5500, and NS4168. A datasheet claim in
this plan cites the part and the table in the YAML entry; it is not a local
measurement.

## 22. Change log

| Date | Change | Evidence |
| --- | --- | --- |
| 2026-11-01 | Initial plan. No bench measurements executed. | None |
