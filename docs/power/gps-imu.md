# GPS and IMU power

Status: evidence lane report. Base commit `7383561a`, 2026-11-01.

No code was changed and no hardware was run for this document. No measurement
below was made for this document. Every number is attributed to a prior local
measurement, a datasheet or vendor publication, a model, or arithmetic on stated
inputs.

## Headline

1. The receiver is the load. The M5Stack Unit GPS v1.1 is published at 31.64 mA
   at the 5 V Grove input. The parser, the I2C bus and the UART current are
   small by comparison. Receiver mode and receiver on-time are the only large
   GPS levers.
2. The largest firmware-side GPS leaks found here, outside receiver mode itself,
   are scheduling and lock lifetime, not receiver settings. The GPS task wakes
   10 times per second in every enabled state except `ABSENT`, a fixed-baud
   receiver that never speaks holds `NO_LIGHT_SLEEP` forever, and the fix path
   logs once per second per connected camera.
3. Standby is the only viable receiver power mode on this unit. Rail cycling
   loses more energy than it saves for any cut shorter than the measured re-fix
   time, because the unit has no backup supply.
4. Motion-adaptive GPS is implemented as an advisory detector only. Nothing
   consumes `GPS::isStationary()`, so the feature saves exactly zero today.
5. The IMU is small in absolute current and large relative to the idle floor.
   The sensor mode (gyro enabled, normal ODR) is the lever, not the I2C bus. All
   IMU cost is opt-in because `IMU` defaults off.

## 1. Evidence labels and reference loads

Labels used in this lane:

| Label | Meaning |
| --- | --- |
| `measured` | Observed on this project's hardware. The citation names the plan or corpus entry. |
| `datasheet` | Stated by a component, module, vendor or protocol document. This is split out from `measured` so a vendor limit is never mistaken for a local measurement. |
| `modeled` | Simulation or arithmetic on stated inputs. Not a device measurement. |
| `inferred` | Code reading with no numeric evidence. |
| `unknown` | No usable evidence. |

The lane README's four labels are preserved. `datasheet` is the only addition,
and it always carries the document and limit in the citation.

Method. This report was built from `plans/18-gps-motion.md`,
`plans/implemented/20-imu-hw-motion.md`, `plans/implemented/14-gps-pcas.md`, `plans/15-gps-power.md`,
`plans/32-gps-advanced.md`, `plans/98b-power-followup.md`,
`plans/111-power-recommendations.md`, `src/FurbleGPS.cpp`,
`include/FurbleGPS.h`, the three IMU source files and `include/FurbleIMU.h`,
`src/FurbleUIGesture.cpp`, `include/FurbleUIGesture.h`, `src/FurblePlatform.cpp`,
`src/FurbleSettings.cpp`, `src/FurblePower.cpp`, `src/FurbleControl.cpp`, the
camera geodata call sites in `lib/furble/`, `platformio.ini`, every
`sdkconfig.*` file, and the hardware notes corpus: `tools/power-model/board-currents.yaml`,
`tools/power-model/README.md`, `plans/00-hardware-experiments.md` and
`docs/supported-hardware.md`. No build was run and no device was touched.

Reference loads used for the saving estimates. These are not measurements of
furble's boards unless the label says so.

| Load | Value | Label | Source |
| --- | --- | --- | --- |
| GPS unit, whole, 5 V input | 31.64 mA | `datasheet` | M5Stack Unit GPS v1.1 product page; `tools/power-model/board-currents.yaml` `unit_active_5v` |
| GPS module, 3.3 V, GPS+BDS | below 25 mA typical, 100 mA peak | `datasheet` | ATGM336H manual, `C90770_ATGM336H-5N31` |
| GPS chip, tracking proxy | 23 mA typical BD+GPS | `datasheet` | AT6558 datasheet, used as an AT6668 proxy |
| GPS standby, in circuit | 0.5 mA, range 0.01 to 2 mA | `modeled`, `unknown` | `board-currents.yaml` `standby_pcas12_module`; chip standby is below 10 uA, module rails stay up |
| GPS cold re-fix after a 60 s rail cut | 107.7 s | `measured` | `plans/00-hardware-experiments.md`, Experiment B part 1 |
| S3 light sleep | 0.240 mA | `datasheet` | ESP32-S3 datasheet, `board-currents.yaml` |
| S3 active, 80 MHz | 40.2 mA typical, 33.1 to 47.3 mA | `datasheet` | ESP32-S3 datasheet, dual core 32-bit access |
| S3 connected idle floor | 3.3 mA | `measured` | Experiment A, `plans/00-hardware-experiments.md` |
| Display backlight, full | 30 to 40 mA | `modeled`, `datasheet` | `board-currents.yaml`, estimated for StickC Plus and StickS3 |
| BMI270 normal, accel+gyro | 0.685 mA | `datasheet` | Bosch BMI270 datasheet |
| BMI270 accel low power | 0.004 to 0.010 mA | `datasheet` | Bosch BMI270 datasheet |
| BMI270 suspend | 0.0035 mA | `datasheet` | Bosch BMI270 datasheet |
| MPU6886 normal, gyro+accel | 2.79 mA | `datasheet` | TDK MPU-6886 datasheet |
| MPU6886 accel low power | 0.040 mA | `datasheet` | TDK MPU-6886 datasheet |
| MPU6886 sleep | 0.006 mA | `datasheet` | TDK MPU-6886 datasheet |

The simulator's power model does not charge peripheral current. Its own README
says so: `model.peripheral` is a constant 0.0035 and the `peripherals` block in
`board-currents.yaml` never reaches it. A simulator result therefore cannot rank
any IMU opportunity and is only weak evidence for GPS scheduling. The model is
not used as evidence in this document except where explicitly labeled.

## 2. GPS behavior as implemented

### 2.1 Task, UART and buffering

`GPS::getInstance()` installs `UART_NUM_2` with 8N1, no flow control, a
256-byte RX ring buffer, a 32-entry event queue, an interrupt in IRAM and
pattern detection on `\n`. The clock source is `UART_SCLK_XTAL` on the S3 and
`UART_SCLK_REF_TICK` elsewhere. The GPS task runs at priority 3 with a 4096-byte
stack.

The task loop in `GPS::task()` services the autobaud probe, then the cycle,
configuration and poll state machines, then blocks on the UART event queue for
`cycleWait()`. On any UART data or pattern event it runs `serviceSerial()` and
then, when a receiver is present, re-runs `serviceConfig()`, `servicePoll()` and
`serviceCycle()`.

`cycleWait()` starts from a 100 ms base and only ever shortens it, except in the
`ABSENT` state, which sleeps until the retry is due. The task therefore wakes 10
times per second in `ACQUIRING`, `MEASURING`, `WAITING`, `STANDBY`, `RAIL_OFF`,
`RESYNC` and `DEGRADED`. In `WAITING` and `STANDBY` the only real deadline is
the next predicted window, so nine of those ten wakeups exist to re-check a
deadline that is already known.

When `m_Enabled` is false the task does not block on the queue at all. It calls
`vTaskDelay(pdMS_TO_TICKS(100))` and loops. GPS defaults off, so this 10 Hz loop
is the GPS cost paid by a default install.

The 256-byte ring is a real constraint at high sentence rates. The measured full
NMEA set at 1 Hz is about 710 characters per second, which is 74 percent of a
9600 8N1 line. At a configured 100 ms interval the same set is about 7100
characters per second, more than seven times the line rate, so pruning must
precede any rate above 1 Hz. `UART_BUFFER_FULL` calls `reset()`, which flushes the ring,
resets the event queue and clears the assembly buffer.

### 2.2 Receiver configuration and message families

Every receiver setting defaults to "send nothing". A fresh NVS boot therefore
produces the same receive-only traffic as before the configuration work.

The transmit path has two families:

| Family | Framing | Used for |
| --- | --- | --- |
| NMEA 0183 | `$` payload `*` two hex digits CRLF | `$PCAS02` rate, `$PCAS03` sentence rates, `$PCAS04` constellation, `$PCAS10` restart, `$PCAS11` platform fallback, `$PCAS12` standby |
| CASIC binary | `BA CE`, length, class, id, payload, 4-byte checksum | `CFG-MSG` 0x06 0x01, `CFG-RATE` 0x06 0x04, `CFG-NAVX` 0x06 0x07, `AID-INI` 0x0B 0x01, `MON-HW` 0x0A 0x09, ephemeris polls |

There is no UBX path. UBX is the u-blox binary protocol with `B5 62` framing. The
AT6668 is a CASIC part. The firmware's binary parser looks for `BA CE` and would
treat UBX frames as noise. This is correct for the supported hardware, and it is
listed as a rejected opportunity rather than a gap.

`configure()` builds the queue in this order:

1. If `GPS_NMEA` is true, eight `CFG-MSG` messages prune the output to RMC and
   GGA at one per fix, with `$PCAS03,1,0,0,0,1,0,0,0` as the NMEA fallback.
2. If `GPS_RATE` is nonzero, one `CFG-RATE` message sets the interval in
   milliseconds, with `$PCAS02,<ms>` as the fallback.
3. If constellation or platform is set, one `CFG-NAVX` query-modify-write edits
   the 44-byte struct under mask bit B8 (`navSystem`) and bit B0 (`dyModel`),
   with `$PCAS04` and `$PCAS11` as the fallback.

One command is outstanding at a time. Each has a 300 ms acknowledgement timeout
and three attempts, then one NMEA fallback per configuration pass. A receiver
that answers nothing therefore costs about 9 seconds of retries for a full
prune-plus-rate pass. Configuration starts only after the first received
character or after `SETTLE_MS` of 3000 ms.

`$PCAS00` (save to receiver flash) is never sent. `$PCAS06` (identity query) is
not implemented. `$PCAS01` (change receiver baud) is not implemented. `$PCAS10`
(restart) is reachable only from the diagnostics page, not from a setting. Its
argument is 0 for a hot restart, 1 for warm and 2 for cold, as defined by the
`$PCAS` family.

Antenna. The v1.1 unit uses a built-in ceramic patch and is documented as
marginal indoors. The SMA variant is a drop-in with an external active antenna.
The module's antenna feed is 2.4 to 3.6 mA and is already inside the module
current figures. The receiver reports `$GPTXT ANTENNA OPEN` continuously on the
v1.1 unit, which Experiment B recorded as likely benign for the patch. Antenna
quality changes time to first fix and therefore energy; it is not a firmware
setting.

Assisted start has two tiers. Mode 1 sends `AID-INI` with the cached position
and time, at most 24 hours old. Mode 2 additionally polls `MSG-GPSEPH`,
`MSG-GPSION` and `MSG-GPSUTC` on a stable fix, stores the frames in NVS at most
once an hour, and replays them paced at 30 ms per frame on the next enable. The
replay commit logic requires a date-bearing RMC received after the arm. Mode 0
is the default and sends nothing.

There is no SBAS control, no QZSS control and no per-signal enable beyond the
three constellations that `$PCAS04` exposes.

### 2.3 Power policy and lock lifetime

`GPS_POWER` selects always on (default), `$PCAS12` standby, or 5 V rail cycling.
`GPS_DUTY` selects 0, 5, 10 or 15 seconds between fixes and applies only to the
two duty policies. The 15-second ceiling exists because a fix older than
`MAX_AGE_MS`, 30 seconds, is no longer live. The newer `GPS_HOLD` feature can
keep sending a held fix for 30 seconds to 60 minutes, so the ceiling is now more
conservative than the freshness logic itself.

The `NO_LIGHT_SLEEP` lock is compiled only for `FURBLE_M5STICKS3`. It is held
during `ACQUIRING`, `MEASURING`, `RESYNC` and each `BURST`, and released during
`WAITING`, `STANDBY`, `RAIL_OFF` and `DEGRADED`. On every other board, including
the four ESP32 boards and the Waveshare S3, the lock does not exist, so the
burst windowing has no light-sleep effect there.

Two lock lifetimes are unbounded:

- The initial `ACQUIRING` state has `m_ProbeDeadline == 0`. `serviceCycle()`
  re-acquires the lock on every pass and has no exit except a received burst.
- A fixed `GPS_BAUD` publishes `PRESENT` immediately and skips the autobaud
  ladder entirely. A receiver that is powered but silent therefore holds
  `NO_LIGHT_SLEEP` forever and is never declared absent.

The second case is reached by a normal user action: enable GPS with no unit
attached while the stored baud is the default 9600. It does not require a fault.

`plans/98b-power-followup.md` records the cost of a permanently held lock as
about 37 mA. That figure is modeled, but the bound is solid: S3 active at 80 MHz
is 40.2 mA and S3 light sleep is 0.240 mA. The exact delta on a given board still
needs a trace.

### 2.4 Fix path, parsing and the camera push

`TinyGPSPlus::encode()` is fed one byte at a time under `m_GPSMutex`. Position,
date, time, altitude, speed, course, HDOP, satellite count and the three parser
counters are read into a `status_t` snapshot under the same mutex. The snapshot
calls about 30 accessors, including the floating point conversions for latitude,
longitude, speed and course.

The snapshot is taken:

- once per UART read in `serviceSerial()`,
- once per second in `GPS::update()`,
- by the GPS Data, Raw NMEA and console status paths.

At the default 1 Hz and 710 characters per second, the snapshot cost is small.
At a configured 100 ms interval with the full sentence set, 28 reads per second
multiply that cost. The measured pruning result, 710 to 91 characters per
second, reduces both the UART duty and this parser work.

When a fix is live, `update()` calls `Control::updateGPS()` once per second.
`Control` forwards `CMD_GPS_UPDATE` to every connected target. The target task
logs `updateGeoData(<camera>)` at INFO on every command, whether or not that
camera vendor sends anything in response. Fujifilm only transmits when the body
has requested geodata, which the hardware trace in `lib/furble/Fujifilm.cpp`
records as about every 10 seconds. The 1 Hz log is therefore mostly noise.

The GPS service timer is created unconditionally and runs `update()` once per
second even when GPS is disabled.

### 2.5 Measured facts and open measurements

Facts recorded by earlier work, reused here:

| Fact | Value | Source |
| --- | --- | --- |
| `$PCAS12,10` stops NMEA and it resumes on its own | about 10 s | Experiment B part 2 |
| `$PCAS03` pruning to GGA+RMC | 710 to 91 characters per second | Experiment B part 2 |
| `$PCAS02,500` without pruning | output rose only 16 percent | Experiment B part 2 |
| Antenna supervisor | `$GPTXT ANTENNA OPEN` seen continuously | Experiment B part 2, likely benign for the patch antenna |
| Backup supply | none, V_BCKP not wired | Experiment B part 1 |
| Rail cut re-fix | 107.7 s after a 60 s cut | Experiment B part 1 |
| Cold and hot start, vendor claim | 23 s cold, 1 s hot | M5Stack unit documentation, cited in plan 32 |
| Indoor reception | marginal with the built-in patch | `docs/supported-hardware.md` |
| S3 connected idle floor | 3.3 mA | Experiment A |
| S3 32.768 kHz crystal | absent | Experiment A |

Not measured, and not claimed anywhere below:

- In-circuit `$PCAS12` standby current.
- Wake-to-first-fix time after a standby period, with or without a prior fix.
- Time to first fix with `AID-INI`, with ephemeris replay, or with neither,
  under a controlled cold boot.
- GPS-only versus multi-constellation receiver current.
- Receiver current at 2 s or slower fix intervals.
- IMU current on any furble board, per chip and per mode.
- False motion and false gesture rates on a tripod, in wind, in a bag, or on a
  vehicle dashboard.
- The energy of one scheduler wake or one light-sleep entry and exit on the S3.

The measurement plan in section 7 exists to close these.

## 3. IMU behavior as implemented

### 3.1 Backends and poll cadence

`IMU::MotionSource` is a singleton with three backends:

| Backend | Board | Poll | Entry to stationary | Exit to moving |
| --- | --- | --- | --- | --- |
| `software` | any IMU M5Unified can read | 1 Hz from the UI housekeeping timer | 60 s continuous quiet on `fabs(magnitude - 1 g) < 0.20 g * scale` | first sample above the threshold |
| `bmi270-motion` | M5StickS3 | 1 Hz status read, `POLL_INTERVAL_MS` 1000 | BMI270 no-motion engine, 70 mg, 3000 counts (60 s) | BMI270 any-motion, 83 mg, 1 count (20 ms) |
| `mpu6886-wom` | StickC, StickC Plus, Core2 | 1 Hz status read | software hold, 60 s since last WOM event | WOM event, 64 mg |

`HW_MOTION` defaults to Software. Auto prefers the board engine and falls back
to software. The setting is not immediate; the engine is chosen when the source
is armed, which is at boot.

The source is armed once if the `IMU` setting is on. It is polled from the same
1 Hz timer that runs inactivity, auto-off and low-battery handling. A 60 s hold
with a 1 Hz poll means the stationary edge is observed about 61 s after the last
quiet sample. The exit is immediate at the source, but the GPS consumer reads it
on a second, unsynchronised 1 Hz timer, so it can be up to a second late.

`MotionSource::poll()` calls `Platform::motionWakeSample()` before every backend
poll. The platform function returns immediately unless `armMotionWake()`
succeeded. Only a hardware backend calls `armMotionWake()`, and it does so after
its engine is configured. With the software backend, which is the default, no
wake source is armed and no PMIC or GPIO read happens. When a hardware backend
arms, the sample is one M5PM1 I2C read of the GPIO4 IRQ status on the S3, plus a
clear when it was asserted, and one GPIO level read on the StickC family.

### 3.2 Sensor mode and bus

`Platform::getInstance()` sets `cfg.internal_imu` from the `IMU` setting before
`M5.begin()`. When the setting is on, M5Unified initializes the chip and leaves
it in its normal accel+gyro configuration. The motion backends use the
accelerometer only. The spirit level uses the accelerometer only: it derives roll
from `atan2(ay, az)` and pitch from `atan2(-ax, sqrt(ay*ay + az*az))`. Only the
IMU live diagnostics page and the console IMU probe read the gyroscope.

The BMI270 backend writes `PWR_CTRL` only to set the accelerometer enable bit if
it is clear. It does not change `ACC_CONF`, does not enable the gyro
selectively, and does not use the chip's low-power accelerometer modes. The
MPU6886 backend does the opposite: its wake-on-motion sequence writes
`PWR_MGMT_2 = 0x07` to disable the gyro axes, `PWR_MGMT_1 |= 0x20` for cycle
mode, `ACCEL_CONFIG_2 = 0x21`, and `SMPLRT_DIV = 19` for about 50 Hz. That makes
the MPU6886 WOM path an accelerometer-only low-power path already, while the
BMI270 motion path leaves the part in normal mode.

The IMU bus is M5Unified's internal I2C at 400 kHz, address 0x68, shared with
the M5PM1 on the S3. Every access takes `g_IMUMutex` because the engines do
read-modify-write sequences on the same registers the spirit level and the live
page read.

A rough bus cost, modeled from the protocol and not measured: an accelerometer
plus gyro read is about 12 to 15 bytes. With address, register, acknowledge and
start/stop overhead that is about 135 to 165 bits, or 340 to 410 us at 400 kHz.
At 1 Hz the bus duty is under 0.05 percent. At 50 Hz it is about 2 percent. If
the bus and CPU draw the full 40 mA while the transaction runs, 50 Hz costs at
most about 0.8 mA average, and the CPU is blocked rather than executing for much
of it. The sensor's own 0.685 mA or 2.79 mA normal-mode current dominates
either way. This is why the bus is not the lever.

### 3.3 Motion wake path

On the S3, `Platform::armMotionWake()` configures M5PM1 GPIO4 as an input with a
pull-up, maps it to the IRQ function, clears the IRQ status, unmasks GPIO4 and
enables a falling-edge wake. It then configures ESP32-S3 GPIO13 as a low-level
light-sleep wake source. On the StickC family it configures GPIO35 the same way.
`disarmMotionWake()` releases both.

The wake source is armed when a hardware backend arms and is never disarmed
during a session. A level-triggered source that never releases blocks light
sleep, which is why `clearMotionWake()` runs after each consumed event and why
`motionWakeSample()` clears the PMIC latch every second.

The panel consumer registers a callback that calls `lv_display_trigger_activity()`
on `MOVING` when `IMU_WAKE` is non-zero. It does not debounce, does not require
a second event and does not distinguish a real pickup from a single noisy
sample. The same `MOVING` edge is what the future GPS policy would use, but the
two consumers have different correctness needs: the panel wants a low
false-positive rate, the GPS policy wants an immediate exit.

### 3.4 Gesture thresholds

`GestureDetector` runs a 50 Hz timer (`GESTURE_POLL_MS = 20`) when `IMU_WAKE` or
`IMU_TRIG` is on. It is deleted when both are off. It reads one accelerometer
sample per poll and classifies it in software.

| Constant | Value | Consequence |
| --- | --- | --- |
| `SHAKE_THRESHOLD` | 0.6 g | shake must exceed this after the per-type gain and `imu scale` |
| `SHAKE_SAMPLES` | 3 | three consecutive samples above the bar |
| `TAP_THRESHOLD` | 1.5 g | tap candidate threshold |
| `TAP_RELEASE_THRESHOLD` | 0.5 g | release threshold that completes a tap |
| `TAP_WINDOW_MS` | 120 ms | a candidate that does not release inside this is dropped |
| `DOUBLE_TAP_MIN_MS` | 80 ms | minimum gap between two taps |
| `DOUBLE_TAP_MAX_MS` | 400 ms | maximum gap |
| `REFRACTORY_MS` | 750 ms | one physical event cannot produce a burst |
| MPU6886 type gain | 1.25 | the MPU6886 reads noisier at the same impulse |
| `imu scale` range | 0.25 to 4.0 | runtime calibration |
| `motion scale` range | 0.25 to 4.0 | separate knob for the motion detector, not gestures |

These are paper values. Plans 17 and 20 both say they are a starting point for a
hardware tuning run, not a measured result. The power consequence is indirect
but large: a false gesture or a false motion event resets the display inactivity
clock, and the backlight is 30 to 40 mA. Threshold tuning is therefore a display
power decision as much as an input decision.

## 4. GPS opportunities

The register below is the full list. Section 6 ranks it. The saving column is a
range with its basis, not a claim.

### G-01 Make the GPS task wait for its next deadline

- **Change.** Replace the 100 ms base in `cycleWait()` with the next state
  deadline. Keep the 75 ms burst-gap deadline while `m_BurstActive` and the
  10 ms floor. Send a queue wake from every state transition.
- **Current behavior.** The task wakes 10 times per second in every enabled
  state except `ABSENT`. In `WAITING` and `STANDBY` the only deadline is the next
  predicted window, which can be seconds away.
- **Evidence.** `inferred` from `cycleWait()` and `task()`.
- **Estimated saving.** `modeled` 0.05 to 0.5 mA. Basis: 10 wakes per second at
  0.2 to 1 ms of 40 mA active time, plus light-sleep re-entry when the system
  would otherwise sleep longer. That is 0.2 to 1.6 percent of the 31.64 mA
  receiver load and 1.5 to 15 percent of the 3.3 mA connected idle floor.
- **Risk.** Low. A missed deadline delays a window or a burst finish, not data
  reception, because UART events wake the queue. The 75 ms burst gap and the
  10 ms stale-deadline floor must stay. The disabled path can block
  indefinitely because `disable()` and `enable()` both send a queue wake before
  the task has to observe the new state; keep that ordering.
- **Effort.** S.
- **Required test.** Host unit test of `cycleWait()` for every state; simulator
  `gps-standby-wake` and `gps-no-receiver` still pass; a new `gps loops` counter
  drops from about 10 per second to the deadline rate; the external trace shows
  higher light-sleep residency; fix cadence and `failedChecksum` are unchanged.
- **Citation.** `src/FurbleGPS.cpp:530-593` (`cycleWait`), `:213-283` (`task`),
  `include/FurbleGPS.h:353` (`SERVICE_MS`); plan 15 review note at lines 196-199.

### G-02 Make GPS off cost nothing

- **Change.** When `!m_Enabled`, block on `xQueueReceive()` with no timeout
  instead of `vTaskDelay(100)`, and pause the 1 Hz service timer in `disable()`
  and resume it in `enable()`.
- **Current behavior.** The disabled task loops at 10 Hz, releasing the power
  lock every pass. The service timer still runs `update()` once per second.
- **Evidence.** `inferred` from `task()`, `disable()` and `startService()`.
- **Estimated saving.** `modeled` 0.02 to 0.2 mA for the task, plus 0.004 to
  0.04 mA for the timer. GPS defaults off, so this is the only GPS cost most
  users pay.
- **Risk.** Low. The wake helper already exists for exactly this transition. A
  paused timer must still refresh the status icon once when GPS turns off.
- **Effort.** S.
- **Required test.** Simulator `gps-motion-setting` and `gps-standby-wake`;
  assert the loop counter stops; `gps on` still tracks; the icon shows disabled;
  measure current with GPS off and BLE idle.
- **Citation.** `src/FurbleGPS.cpp:213-221`, `:138-147` (`wakeGPSService`),
  `:1942-1992` (`disable`), `:2056-2070` (`startService`).

### G-03 Bound the initial acquisition lock and detect a silent fixed-baud receiver

- **Change.** After a bounded silence, for example 10 seconds at a fixed baud,
  declare the receiver absent even when `GPS_BAUD` is fixed: drop the rail,
  release `NO_LIGHT_SLEEP`, and retry once after 60 seconds. The autobaud path
  already has this state; the fixed-baud path does not.
- **Current behavior.** A fixed baud publishes `PRESENT` immediately and enters
  `ACQUIRING` with `m_ProbeDeadline == 0`. `serviceCycle()` re-acquires the lock
  on every pass and has no exit. The receiver is never declared absent.
- **Evidence.** `inferred` from `enable()`, `serviceCycle()` and `cycleWait()`.
- **Estimated saving.** `modeled` up to 37 mA while the lock is held. Basis:
  plan 98b's +37 mA; bounded by 40.2 mA active versus 0.240 mA light sleep. The
  case is a fault, but it is reached by enabling GPS with no unit attached at
  the default 9600 baud, which is a normal user action.
- **Risk.** Low to medium. A very slow receiver could be declared absent. The
  60-second retry recovers it, and the autobaud path already accepts that
  trade. Do not fire while characters are arriving.
- **Effort.** S to M.
- **Required test.** Simulator `gps-no-receiver` with a fixed baud seed; device:
  enable GPS with no unit, confirm `receiver: absent`, no `gps` lock owner in
  `power stats`, rail off, and a unit attached later is found on the retry.
- **Citation.** `src/FurbleGPS.cpp:314-391` (`enable`), `:595-708`
  (`serviceCycle`), `:530-593` (`cycleWait`); `include/FurbleGPS.h:378`
  (`NO_RECEIVER_RETRY_MS`), `:400` (`SETTLE_MS`); plan 98b lines 14-40.

### G-04 Back off the receiver when there is no fix

- **Change.** If no valid fix has been produced for a configurable period, for
  example 2, 5 or 10 minutes, switch the receiver to `$PCAS12` standby and probe
  on a bounded schedule until a fix returns. This is a new fix-aware policy, not
  a change to the manual duty setting.
- **Current behavior.** Always on is the default. A receiver that cannot fix,
  indoors or with a disconnected antenna, stays powered indefinitely at its
  published 31.64 mA; the no-fix current is not separately measured.
  `GPS_POWER` standby exists but is a fixed timer, not fix-aware.
- **Evidence.** `inferred` from the power policy; receiver current is
  `datasheet`; standby current is `modeled` and unmeasured in circuit.
- **Estimated saving.** `modeled` about 31 mA during backoff if standby is
  0.5 mA, at least 21 mA if standby is 10 mA. Scenario: 8 hours indoors at 90
  percent backoff is about 22 mA average, roughly 176 mAh over those 8 hours.
  The standby current is the uncertain term and must be measured first.
- **Risk.** Medium. A camera request during backoff gets a stale or absent fix.
  The fix hold can cover it if enabled. A user who walks outdoors waits for the
  next probe. A confirmed antenna fault should shorten the schedule, not extend
  it.
- **Effort.** M.
- **Required test.** Measure standby current and wake-to-first-fix after 10, 30
  and 60 seconds, with and without a prior fix; 60 minutes with a shielded
  antenna, then uncover and record time to first fix; Fujifilm EXIF after a
  backoff; confirm the icon and GPS Data page report the state.
- **Citation.** `$PCAS12` path in `src/FurbleGPS.cpp:623-627`, `:848-858`;
  `include/FurbleGPS.h:277-282`; plan 15 lines 101-118; Experiment B result in
  plan 00 line 190; `board-currents.yaml` `standby_pcas12_module`.

### G-05 Wire `GPS_MOTION` to a receiver policy

- **Change.** Consume `GPS::isStationary()` in the receiver policy. Stationary
  selects standby or a low rate; moving restores full rate immediately. Use
  `MotionSource::addCallback()` for the moving edge instead of reading the
  source from the GPS service timer.
- **Current behavior.** `GPS::updateMotion()` logs transitions only.
  `isStationary()` has no consumer. The setting defaults off, and even when on
  it changes no receiver state, so it saves zero.
- **Evidence.** `inferred` from `updateMotion()` and the absence of callers.
- **Estimated saving.** `modeled`, same receiver delta as G-04. With a 10-second
  standby and a 2-second wake plus fix, the average is
  `0.3 * 31.64 + 0.7 * 0.5 = 9.8 mA`, a saving of about 21.8 mA while
  stationary. A 0.685 mA BMI270 in normal mode offsets about 3 percent of that.
  The detector's blind spots decide how often the saving applies.
- **Risk.** Medium to high. A stale fix when the device moves; constant velocity
  and slow rotation are invisible to all three backends; the software backend
  has a 1 g reference bias; a camera request during standby is slow. The plan 18
  hardware gate must pass before this ships as a default.
- **Effort.** M to L.
- **Required test.** The full plan 18 hardware gate on the S3 plus one MPU6886
  board; stationary and moving transitions over 20 cycles; fix latency after
  each stationary duration; Fujifilm EXIF in all three states; battery A/B with
  the feature off and on.
- **Citation.** `src/FurbleGPS.cpp:2091-2096` (`updateMotion`), `:2050-2053`
  (`isStationary`); `include/FurbleIMU.h:97-100`; plans 18 and 20.

### G-06 Allow longer duty when Fix Hold is on, and find an early wake

- **Change.** Add 30 s and 60 s standby intervals when `GPS_HOLD` is nonzero,
  because the fix hold now carries a fix past `MAX_AGE_MS`. Separately, measure
  whether any command can wake the receiver before its `$PCAS12` timer expires,
  so a camera request can cut standby short.
- **Current behavior.** `DUTY_SECONDS` is 0, 5, 10 or 15. The receiver wakes
  only on its own timer. There is no early wake path.
- **Evidence.** `inferred` from `DUTY_SECONDS`, `FurbleGPSHold.h` and the absent
  wake command.
- **Estimated saving.** `modeled`. With a 3-second wake plus fix, 15 s duty
  averages 6.7 mA and 30 s averages 3.6 mA, a saving of 3.1 mA. With a 5-second
  wake plus fix, the pair is 10.9 and 5.7 mA, a saving of 5.2 mA.
- **Risk.** A held position on a moving user; camera latency; a wake command that
  the receiver ignores. The hold timestamp advances but the position does not.
- **Effort.** S for the range, M for the wake experiment.
- **Required test.** Time to first fix after 15, 30 and 60 seconds of standby;
  EXIF position and timestamp after a 30-second hold; Fujifilm request latency;
  try `$PCAS12,0`, a bare UART byte, `$PCAS10,0` and a hot `CFG-RST` as wake
  candidates, and record which one works.
- **Citation.** `include/FurbleGPS.h:281-282`, `:354`; `include/FurbleGPSHold.h:21-32`;
  `src/FurbleGPS.cpp:848-872`; plan 15 lines 115-118.

### G-07 Make `AID-INI` the default after the first fix

- **Change.** Set `GPS_ASSIST` default to 1 (position and time). Keep 0 as an
  option. Do not change the cache format.
- **Current behavior.** Default 0, no assistance. A boot after the receiver rail
  has been off is a cold start. `AID-INI` is implemented and refuses a cache
  older than 24 hours.
- **Evidence.** `inferred` from the setting default and `sendAidIni()`; the
  re-fix after a rail cut is `measured`; the TTFF benefit is unmeasured.
- **Estimated saving.** `modeled` time saved multiplied by 31.64 mA. Saving 10 s
  is 0.088 mAh per boot, 20 s is 0.176 mAh. Small per boot, but the cost is zero
  once a cache exists.
- **Risk.** Low to medium. A wrong time hurts TTFF, which is why the code clears
  the time-valid bit when the cache age is unknown. A wrong position narrows the
  search to the wrong place. The cache writes NVS every 10 minutes while a fix
  exists, about 144 writes per day, which is more NVS traffic than the
  ephemeris tier and needs a wear check.
- **Effort.** S for the default, but only after the measurement.
- **Required test.** Cold boot with assist 0 and 1, five repeats each, measure
  TTFF and check that a stale or impossible cache is refused; confirm the NVS
  write rate.
- **Citation.** `src/FurbleGPS.cpp:1538-1631` (`sendAidIni`), `:1446-1536`
  (cache); `include/FurbleGPS.h:370-375`; plan 32 lines 35-42.

### G-08 Measure ephemeris replay before any default change

- **Change.** Keep `GPS_ASSIST` 2 opt-in. Run the owed hardware test. If replay
  shortens TTFF, consider it as a default after the NVS wear check.
- **Current behavior.** Implemented and stored at most hourly, replayed on the
  next enable after a date-bearing RMC commits. The receiver effect is
  explicitly hardware-tuning-pending in plan 32.
- **Evidence.** `inferred` from the code; the re-fix after a rail cut is
  `measured`; the replay benefit is `unknown`.
- **Estimated saving.** `modeled`. If the measured 107.7 s re-fix after a rail
  cut falls to 30 s, the saving is 0.683 mAh per cold start; to 10 s, 0.86 mAh.
- **Risk.** Stale or rejected ephemeris, receiver validation behavior, NVS wear,
  and a commit path that has already had three wrong designs. Do not default it
  before the receiver rejects an expired injection correctly.
- **Effort.** M.
- **Required test.** Plan 32's owed run: capture a cache, leave the unit
  unpowered past four hours, replay, and compare TTFF with a no-replay control;
  check whether the receiver rejects expired frames itself.
- **Citation.** `src/FurbleGPS.cpp:1633-1940`; `include/FurbleGPS.h:386-397`;
  plan 32 lines 58-66, 186-230, 491-506.

### G-09 Measure constellation current before recommending it

- **Change.** No code. Use the existing GPS-only option and measure it.
- **Current behavior.** Default sends nothing, so the receiver's own
  constellation set applies. The UI can select GPS, BDS, GLONASS or a
  combination through `$PCAS04` or `CFG-NAVX`.
- **Evidence.** `unknown`. The only nearby datasheet figure is 23 mA typical for
  BD+GPS tracking on the sibling AT6558. There is no GPS-only figure.
- **Estimated saving.** `unknown`. A few mA is plausible but not evidenced. Do
  not quote a number until the receiver is measured.
- **Risk.** Fewer satellites, worse indoor fix, longer TTFF. A constellation that
  saves current but loses the fix is a net loss.
- **Effort.** S to measure, no code.
- **Required test.** Same sky, same antenna, same position: GPS-only versus
  GPS+BDS+GLONASS, measure current, TTFF, satellites used and C/N0.
- **Citation.** `src/FurbleGPS.cpp:1332-1359`; `include/FurbleGPS.h:273-274`;
  plan 14 lines 95-96; AT6558 datasheet.

### G-10 Measure slower fix intervals before adding them

- **Change.** Add 2 s and 5 s options to `RATE_MS` and the UI only if the
  receiver current responds to the interval.
- **Current behavior.** 1000, 500, 200 and 100 ms are available. Default sends
  nothing. There is no slower-than-1 Hz option.
- **Evidence.** `unknown`. No datasheet in the corpus states that the AT6668
  navigation current scales with the output interval. The `$PCAS02` command sets
  the fix interval, which is related but not the same as receiver duty.
- **Estimated saving.** `unknown`. If the navigation engine scales linearly, 2 s
  could halve processing current; the RF front end may not scale at all. The
  UART side is already small at 91 characters per second when pruned.
- **Risk.** Stale geodata, although 2 s and 5 s both stay inside `MAX_AGE_MS`.
  Lower GPX track resolution. Camera timestamps still advance.
- **Effort.** S.
- **Required test.** Measure current at 1, 2 and 5 s with pruning, same sky;
  record `failedChecksum`, fix age and TTFF.
- **Citation.** `include/FurbleGPS.h:270-271`; `src/FurbleGPS.cpp:1326-1330`;
  plan 14 lines 87-89.

### G-11 Default baud should be Auto, not 9600

- **Change.** Change the `GPS_BAUD` default from 9600 to Auto. Keep 9600 and
  115200 selectable.
- **Current behavior.** The stored default is 9600. The supported AT6668 units
  ship at 115200, so a fresh device gets no fix until the user finds the
  setting. The autobaud ladder exists but is opt-in.
- **Evidence.** `inferred` from the default; the unit baud is `datasheet`; the
  character rate is `measured`.
- **Estimated saving.** `inferred` 0.1 to 0.7 mA from shorter UART transmit
  active time with the full sentence set. Basis: 710 characters per second is
  74 percent of a 9600 line and 6.2 percent of a 115200 line. The module's
  transmit driver current is not published; the bound assumes 0.2 to 1 mA while
  the line is active, which gives 0.13 to 0.68 mA over that duty change. With
  pruning the saving falls to 0.02 to 0.09 mA. The larger win is a working
  default and no overflow.
- **Risk.** Auto costs up to 7.2 seconds of probing per enable, about 0.063 mAh
  at 31.64 mA, and requires two checksummed sentences to lock. A 9600 module
  still works through Auto. The change affects fresh installs only.
- **Effort.** S.
- **Required test.** Fresh NVS on the v1.1 unit, confirm a fix with no user
  action; measure TTFF and current at 9600, 115200 and Auto; confirm a 9600 unit
  locks; confirm an absent unit reaches the absent state.
- **Citation.** `src/FurbleSettings.cpp` `GPS_BAUD` default; `src/FurbleGPS.cpp:159`,
  `:346-350`; plan 32 lines 136-167; `docs/supported-hardware.md` lines 56-77.

### G-12 Prune sentences by default

- **Change.** Default `GPS_NMEA` to true (RMC+GGA), or send the prune whenever
  the configured rate is above 1 Hz.
- **Current behavior.** Default false, so the receiver sends its full set.
  Pruning to RMC+GGA was measured to cut output from 710 to 91 characters per
  second. The satellite page temporarily re-enables GSV and GSA.
- **Evidence.** `measured` for the character reduction; `inferred` for the
  current.
- **Estimated saving.** `inferred` 0.1 to 0.7 mA from UART line duty at 9600
  (74 percent to 9.5 percent), on the same 0.2 to 1 mA transmit driver
  assumption as G-11, plus parser work. At 115200 the saving is 0.01 to 0.06 mA.
  The receiver RF current is unchanged.
- **Risk.** A module that ignores `$PCAS03` silently does nothing. The satellite
  page needs GSV and GSA, but already restores them on open. RMC and GGA carry
  position, date, time, altitude and satellite count.
- **Effort.** S.
- **Required test.** Fresh boot, confirm only RMC and GGA, GPS Data and camera
  geotag still work; measure current and `chars`; open the satellite page and
  confirm GSV and GSA return; power cycle and confirm the receiver returns to
  its own defaults.
- **Citation.** `src/FurbleGPS.cpp:1317-1324`, `:2744-2767`; plan 14 lines 90-94;
  Experiment B result line 190.

### G-13 Demote per-event INFO logs on the fix path

- **Change.** Move the `updateGeoData` log in `Control::Target::task` and the
  acquire and release logs in `Power` to DEBUG, or log only on a state
  transition.
- **Current behavior.** One INFO line per second per connected camera on every
  `CMD_GPS_UPDATE`, plus two INFO lines per GPS burst for the
  `NO_LIGHT_SLEEP` lock. Release builds keep INFO because
  `CONFIG_LOG_MAXIMUM_LEVEL=3`.
- **Evidence.** `inferred` from the call sites and `sdkconfig`.
- **Estimated saving.** `modeled` 0.05 to 0.2 mA. Basis: 150 to 240 characters
  per second is 1.3 to 2.1 percent of the 115200 console line, plus formatting
  on the active CPU. The delta may be below the measurement noise; if so, keep
  the change as hygiene and do not claim a power win.
- **Risk.** Low. Debug builds with `-DLOG_LOCAL_LEVEL=VERBOSE` still show the
  lines. Check that no test parses the console text.
- **Effort.** S.
- **Required test.** Host and simulator suites pass; the verbose build still
  shows the lines; measure console line volume before and after.
- **Citation.** `src/FurbleControl.cpp:155-158`; `src/FurblePower.cpp:84-92`,
  `:118-130`; `sdkconfig.m5stick-s3:2585-2594`.

### G-14 Buffer sizing is a correctness item, not a power lever

- **Change.** Raise `BUFFER_SIZE` from 256 to 1024, or drain in a loop per
  event, so a configured 100 ms rate with the full sentence set does not
  overflow.
- **Current behavior.** 256-byte ring and a 32-event queue. `UART_BUFFER_FULL`
  calls `reset()` and loses the burst.
- **Evidence.** `inferred` from arithmetic and the event handler.
- **Estimated saving.** No direct mA. The receiver stays powered either way; the
  cost of an overflow is a lost fix and a resync, not receiver energy.
- **Risk.** 768 more bytes of RAM. No functional risk.
- **Effort.** S.
- **Required test.** A 100 ms rate with the full set for 10 minutes, zero
  `UART_BUFFER_FULL` events, `failedChecksum` flat.
- **Citation.** `include/FurbleGPS.h:351-352`; `src/FurbleGPS.cpp:249-256`,
  `:2680-2707`.

### G-15 A custom parser is not worth it

- **Change.** None. Keep TinyGPSPlus.
- **Current behavior.** Per-byte encode under a mutex, about 30 accessors per
  snapshot, called per UART read and at 1 Hz.
- **Evidence.** `measured` character rate; `modeled` CPU share.
- **Estimated saving.** Under 0.1 mA at default settings. At a 100 ms rate with
  the full set, up to about 0.5 mA modeled, and the fix for that is pruning and
  a sane rate, not a new parser.
- **Risk.** A custom parser would risk the checksum and field handling that the
  project already depends on.
- **Effort.** M to L.
- **Required test.** Not applicable if rejected.
- **Citation.** `src/FurbleGPS.cpp:2560-2585`, `:2858-2898`; `platformio.ini:26`.

### G-16 Rail cycling costs more than it saves for short cuts

- **Change.** None. Keep rail cycling as an explicitly discouraged expert
  option.
- **Current behavior.** Cutting the 5 V rail removes the 31.64 mA unit draw but
  loses the ephemeris. The unit has no backup supply.
- **Evidence.** `measured` re-fix time; `datasheet` current; arithmetic.
- **Estimated saving.** Negative for short cuts. A 60-second cut saves
  `31.64 * 60 = 1898 mA s`. The measured 107.7-second re-fix costs, by
  arithmetic, `31.64 * 107.7 = 3408 mA s`. Net loss 1510 mA s, about 0.42 mAh
  per cycle, before counting the missed geotags. The break-even off time is the
  re-fix time itself, 107.7 seconds, even ignoring fix quality.
- **Risk.** Cold start on every cycle, stale geodata, and the user sees a slow
  fix. The only case that wins is a rail that stays off for hours.
- **Effort.** S to remove or leave as is.
- **Required test.** Already measured in Experiment B; repeat only if the
  ephemeris replay path changes the re-fix time.
- **Citation.** `src/FurbleGPS.cpp:860-872`; Experiment B part 1, plan 00 line
  191; plan 15 lines 108-114; `board-currents.yaml` `cold_start_refix_s`.

### G-17 Never send `$PCAS00`

- **Change.** None.
- **Current behavior.** The command is not implemented.
- **Evidence.** `inferred` from the code and plan 14.
- **Estimated saving.** No power effect.
- **Risk.** Saving the configuration to receiver flash makes furble's settings
  outlive furble, survive a device factory reset, and wear the module flash.
- **Effort.** S to keep out.
- **Required test.** Confirm a power cycle returns the receiver to its own
  defaults.
- **Citation.** plan 14 lines 102-105; `src/FurbleGPS.cpp:989-1000`.

### G-18 SBAS and QZSS have no control and no evidence

- **Change.** None.
- **Current behavior.** The firmware does not configure SBAS or QZSS. `$PCAS04`
  exposes GPS, BDS and GLONASS only.
- **Evidence.** `unknown`.
- **Estimated saving.** `unknown`. Do not quote a number.
- **Risk.** Disabling a correction service could degrade fix quality; there is
  no documented command in the corpus to do it cleanly.
- **Effort.** S to investigate, M to implement if a command exists.
- **Required test.** First prove the receiver enables SBAS at all and that a
  disable command exists; then measure current and fix quality.
- **Citation.** plan 14 lines 95-96; CASIC protocol specification; Quectel L76K
  specification.

### G-19 UART wake on a character cannot work for NMEA

- **Change.** None.
- **Current behavior.** The design holds `NO_LIGHT_SLEEP` across each expected
  burst and wakes before it. UART wake is not used.
- **Evidence.** `datasheet`. ESP-IDF states that the character which triggers
  UART wakeup, and every character before it, is not received after wakeup.
- **Estimated saving.** No usable saving. A sentence cannot survive the lost
  prefix, so the receiver would have to re-send it.
- **Risk.** Enabling it would silently drop sentences and break geotagging.
- **Effort.** Not applicable.
- **Required test.** Not applicable; the IDF behavior is the citation.
- **Citation.** plan 15 lines 64-70; ESP-IDF UART and sleep documentation.

### G-20 UBX is the wrong protocol family for this hardware

- **Change.** None.
- **Current behavior.** The binary path is CASIC `BA CE`. NMEA is parsed by
  TinyGPSPlus. There is no UBX parser.
- **Evidence.** `inferred` from the code and the AT6668 part.
- **Estimated saving.** None on this hardware. UBX would be dead code.
- **Risk.** Adding it would grow the image and the test surface for no supported
  unit.
- **Effort.** L.
- **Required test.** Not applicable.
- **Citation.** `src/FurbleGPS.cpp` CASIC constants; plan 32; the AT6668 is a
  CASIC part, so UBX is not its native binary protocol.

### G-21 MON-HW antenna feedback is an enabler, not a saving

- **Change.** Verify the MON-HW layout on hardware and surface antenna status
  and jam indicator on the diagnostics page. Use a confirmed antenna fault to
  trigger G-04, not to change receiver settings directly.
- **Current behavior.** `gps monhw` polls and decodes the CASIC 2.14.2 layout:
  `agcData0` at 12, `antStatus` at 20, and the jamming word at 24. The offsets
  are specification-derived; the live field values still need a hardware
  comparison. `$GPTXT ANTENNA OPEN` was observed continuously on the v1.1 unit.
- **Evidence.** `measured` for the GPTXT line; `spec` plus code for the MON-HW
  layout; `unknown` for the live status-bit mapping.
- **Estimated saving.** No direct saving. It lets G-04 distinguish "no sky" from
  "no antenna" and avoid running a deaf receiver at full power.
- **Risk.** A wrong decode could mislead the policy. Keep it diagnostic until
  the layout is confirmed.
- **Effort.** S to M.
- **Required test.** Compare MON-HW against a known-good antenna and a
  disconnected one; confirm the antenna status bits and the jam indicator.
- **Citation.** `src/FurbleGPS.cpp:2779-2804`; `Casic::parseMonHw`; plan 32 lines
  80-83; Experiment B result line 190.

## 5. IMU opportunities

### I-01 Withdrawn: the PMIC wake sample is already gated

- **Change.** None. This was withdrawn after checking `motionWakeSample()` and
  both hardware arm paths.
- **Current behavior.** `Platform::motionWakeSample()` returns false unless
  `m_MotionWakeArmed` is true. Only the BMI270 and MPU6886 backends call
  `armMotionWake()`, and each does so after its engine is configured. The
  software backend never arms a wake source. The 1 Hz sample therefore does not
  touch the PMIC on the default path. It exists only when a hardware engine is
  actually using the line.
- **Evidence.** `inferred` from `motionWakeSample()` and the two backend `arm()`
  methods.
- **Estimated saving.** None. There is no PMIC read to remove on the software
  path.
- **Risk.** None.
- **Effort.** Not applicable.
- **Required test.** Not applicable. Plan 20 gate steps 1 and 2 already exercise
  the armed path.
- **Citation.** `src/FurblePlatform.cpp:548-583`;
  `src/FurbleIMUMotionBMI270.cpp:255`; `src/FurbleIMUMotionMPU6886.cpp:144`.

### I-02 Put the IMU in accelerometer-only mode when the gyro has no consumer

- **Change.** Initialize the accelerometer only. Enable the gyro only while the
  IMU live page is open, or when a future consumer needs it. For motion-only
  sessions, use the BMI270 accelerometer low-power mode and restore normal mode
  when the level or live page needs it.
- **Current behavior.** `M5.begin()` with `internal_imu=true` leaves the chip in
  normal accel+gyro mode for the whole session. The motion backends, the level
  page and the gesture detector all use the accelerometer only. The gyro is read
  only by the diagnostics live page and the console IMU probe.
- **Evidence.** `inferred` from the code; `datasheet` for the mode currents.
- **Estimated saving.** `datasheet` endpoints, `modeled` delta. BMI270: up to
  0.675 mA if the part can drop from 0.685 mA to 0.010 mA; disabling only the
  gyro saves an unquantified fraction of 0.685 mA. MPU6886: up to 2.75 mA from
  2.79 mA to 0.040 mA. Relative to the 3.3 mA idle floor that is 20 percent on
  the S3 and 83 percent on an MPU6886 board; relative to GPS-on it is 2 to 8.7
  percent.
- **Risk.** Medium to high. M5Unified's `update()` may expect both sensors.
  Reconfiguration must hold `g_IMUMutex` and must not interleave with the
  spirit level or the live page. The BMI270 any-motion and no-motion engines may
  require a specific accelerometer mode and ODR, so the threshold behavior has
  to be re-verified.
- **Effort.** M.
- **Required test.** `motion status` transitions in each mode; level accuracy on
  the S3 and one MPU6886 board; `imu live` gyro values when the page opens;
  external current with IMU off, IMU on with gyro, IMU on without gyro.
- **Citation.** `src/FurblePlatform.cpp:41-48`; `src/FurbleIMU.cpp:59-67`;
  `src/FurbleIMUMotionBMI270.cpp:70-74`, `:208-215`; `src/FurbleUI.cpp:6136-6137`
  (level uses accel), `:9067-9072` (live page reads gyro); BMI270 datasheet;
  MPU-6886 datasheet; `board-currents.yaml`.

### I-03 Lower the gesture poll when the display is off

- **Change.** Run the gesture timer at 50 Hz only while the display is on. Use
  25 Hz or 10 Hz when the display is off, and restrict tap wake to the rate it
  needs.
- **Current behavior.** `GESTURE_POLL_MS = 20` (50 Hz) whenever `IMU_WAKE` or
  `IMU_TRIG` is on. Each poll calls `M5.Imu.update()` and `getAccel()`.
- **Evidence.** `inferred` from the timer and the poll; plan 17 states the
  tradeoff itself.
- **Estimated saving.** `modeled` 0.25 to 2 mA. The lower bound assumes the poll
  cost is dominated by I2C and CPU time; the upper bound assumes the 20 ms
  period is the light-sleep floor and each wake costs about 1 ms at 40 mA. The
  sensor's normal-mode current is not reduced by this change.
- **Risk.** Medium. Tap detection has a 120 ms window. At 25 Hz there are three
  samples, at 10 Hz there is one. Plan 17 calls 25 Hz marginal for tap and
  adequate for shake. A missed wake is a user-visible regression.
- **Effort.** S.
- **Required test.** 100 taps and 100 shakes at 50, 25 and 10 Hz, count misses
  and false events; measure current with the display off; confirm double-tap
  shutter still fires exactly once.
- **Citation.** `include/FurbleUI.h:752`; `src/FurbleUI.cpp:9681`;
  `src/FurbleUIGesture.cpp:70-90`; plan 17 lines 128-138.

### I-04 Use the hardware engine as the shake wake source

- **Change.** When only shake or motion wake is needed, use the BMI270
  any-motion engine or the MPU6886 WOM path to wake, and stop the 50 Hz software
  poll. Keep software tap detection where tap is required.
- **Current behavior.** All gestures are classified in software at 50 Hz. The
  hardware engines only feed `MotionSource` at 1 Hz.
- **Evidence.** `inferred` from the engines and the gesture timer; plan 20.
- **Estimated saving.** `modeled`. It removes the 50 Hz poll and may allow the
  accelerometer low-power mode from I-02. The combined endpoint is roughly
  1 to 3 mA depending on chip. It is not additive with I-02 and I-03; it is the
  endpoint those two move toward.
- **Risk.** Medium to high. The BMI270 tap and double-tap features live in the
  legacy feature config, so hardware tap would need a config blob. The interrupt
  path is unverified on both boards. A shake wake has a different latency and
  false-positive profile than 50 Hz classification.
- **Effort.** M to L.
- **Required test.** Plan 20 hardware gate steps 1 to 6 on the S3 and one
  MPU6886 board; 100 intentional shakes; a false-wake run; current with the
  display off.
- **Citation.** `src/FurbleIMUMotionBMI270.cpp:183-325`;
  `src/FurbleIMUMotionMPU6886.cpp:88-233`; plans 17 and 20.

### I-05 Suspend the IMU explicitly when the IMU setting is off

- **Change.** After `M5.begin()`, if `Settings::IMU` is false, put the chip in
  suspend with `M5.Imu.sleep()` or the chip's suspend register, and restore it
  when the setting is enabled at the next boot.
- **Current behavior.** `cfg.internal_imu=false` makes M5Unified skip the IMU
  initialization. The chip's power state is not set explicitly. A cold part
  should be in its reset or suspend state; a warm reset after an IMU-on session
  may leave it in the previous mode.
- **Evidence.** `unknown`. The chip state is not read or reported anywhere.
- **Estimated saving.** `modeled` up to 0.685 mA on the BMI270 or 2.79 mA on the
  MPU6886 if the part is not already suspended. The suspend currents are
  0.0035 mA and 0.006 mA, so the delta is the whole normal-mode current.
- **Risk.** Low to medium. Calling `sleep()` on a bus that was never initialized
  may fail or hang; the bus is shared with the PMIC. The change must be guarded
  by the same bus mutex and must not run during a light-sleep wake race.
- **Effort.** S.
- **Required test.** Measure current with IMU off after a cold boot and after an
  IMU-on session plus restart; read the chip power register to confirm suspend.
- **Citation.** `src/FurblePlatform.cpp:41`; M5Unified IMU API (`sleep`);
  BMI270 datasheet 3.5 uA suspend; MPU-6886 datasheet 6 uA sleep.

### I-06 Debounce the panel wake separately from the GPS exit

- **Change.** Require two consecutive `MOVING` events, or a minimum impulse,
  before calling `lv_display_trigger_activity()`. Keep the GPS policy's exit
  immediate.
- **Current behavior.** Any `MOVING` event wakes the panel when `IMU_WAKE` is
  non-zero. The software backend exits stationary on a single sample above
  0.20 g, by design. There is no debounce.
- **Evidence.** `inferred` from the callback and the detector; plan 18 records
  the tripod and wind risk.
- **Estimated saving.** `modeled`. One false wake costs one display-on interval.
  With a 30 s inactivity timeout and a 40 mA display, that is about 0.33 mAh per
  false wake; ten per day is 3.3 mAh per day. Continuous false wakes keep the
  display on at 30 to 40 mA, which dwarfs every GPS saving in this document.
- **Risk.** Medium. Added wake latency is user-visible. A pickup should still
  wake promptly. The GPS policy and the panel policy must not share a debounce,
  because the GPS exit is required to be immediate.
- **Effort.** S to M.
- **Required test.** Tripod in wind, bag while walking, car dashboard; count
  wakes and display-on seconds; measure current; confirm an intentional pickup
  still wakes within the product's stated latency.
- **Citation.** `src/FurbleUI.cpp:425-430`, `:447-462`; `src/FurbleIMU.cpp:89-125`;
  `MOTION_DELTA_G` at `src/FurbleIMU.cpp:42`; plan 18 lines 163-179, 298-311.

### I-07 Compensate the software backend's 1 g bias

- **Change.** Sample the resting magnitude at `arm()` and use it as the
  reference instead of the hardcoded 1.0 g. Keep `motion scale` as the threshold
  knob.
- **Current behavior.** `fabs(magnitude - 1.0f) >= 0.20f * scale`. A part whose
  resting magnitude sits more than 0.20 g from 1 g reads `MOVING` forever. The
  quiet window never starts and `STATIONARY` never fires.
- **Evidence.** `inferred` from the code; plans 18 and 20 record it as a known
  exposure.
- **Estimated saving.** No direct saving. It is the difference between G-05
  working and silently doing nothing. If the detector never reports stationary,
  the motion policy saves zero.
- **Risk.** Low to medium. Arming while the device is moving samples the wrong
  reference. Add a settling period and refuse an implausible reference.
- **Effort.** S.
- **Required test.** Read the resting magnitude on each board; confirm stationary
  still fires on a desk and not while moving; host and simulator tests; then
  rerun the G-05 gate.
- **Citation.** `src/FurbleIMU.cpp:35-42`, `:97-125`; plan 20 lines 311-330;
  plan 18 lines 296-311.

### I-08 Disarming when no consumer needs the source is not worth it

- **Change.** None for now.
- **Current behavior.** The source arms at boot whenever `IMU` is on, even if
  `IMU_WAKE`, `IMU_TRIG` and `GPS_MOTION` are all off.
- **Evidence.** `inferred` from the UI constructor and the settings.
- **Estimated saving.** `modeled` under 0.1 mA. The cost is the 1 Hz IMU read,
  plus the PMIC read only when a hardware engine is armed.
- **Risk.** The source is armed once by design and the settings require a
  restart. Dynamic arming would add a new lifecycle requirement for almost no
  power.
- **Effort.** S to M.
- **Required test.** Not applicable if rejected.
- **Citation.** `src/FurbleUI.cpp:441-463`; `src/FurbleIMU.cpp:184-242`;
  plan 20 lines 332-347.

### I-09 I2C speed and SPI are not the lever

- **Change.** None.
- **Current behavior.** 400 kHz internal I2C at address 0x68, shared with the
  M5PM1 on the S3.
- **Evidence.** `modeled` from the protocol.
- **Estimated saving.** Under 0.1 mA at 1 Hz and about 0.8 mA upper bound at
  50 Hz if the bus and CPU draw the full 40 mA during the transaction. The
  sensor mode dominates.
- **Risk.** Raising the bus to 1 MHz or moving the IMU to SPI is not wired and
  risks the PMIC and the other internal devices.
- **Effort.** M.
- **Required test.** Not applicable if rejected.
- **Citation.** `src/FurbleIMUMotionBMI270.cpp:76`;
  `src/FurbleIMUMotionMPU6886.cpp:35`; M5Unified `I2C_Class` API.

### I-10 Polling the motion source faster than 1 Hz is a regression

- **Change.** None.
- **Current behavior.** 1 Hz.
- **Evidence.** `inferred` from the timer and the 60 s hold.
- **Estimated saving.** Negative. Faster polling costs more CPU and more bus
  time, and it cannot make the stationary edge earlier than the 60 s hold.
- **Risk.** None if left alone.
- **Effort.** Not applicable.
- **Required test.** Not applicable.
- **Citation.** `src/FurbleUI.cpp:415-430`; `src/FurbleIMU.cpp:29`.

### IMU measurement caveat: MPU6886 mode restore

This is not an opportunity. It is a caveat that will distort any IMU current
comparison.

The MPU6886 WOM sequence writes `ACCEL_CONFIG`, `PWR_MGMT_1`, `PWR_MGMT_2`,
`ACCEL_CONFIG_2`, `INT_PIN_CFG`, `INT_ENABLE`, the three WOM thresholds,
`ACCEL_INTELLIGENCE` and `SMPLRT_DIV`. `disarm()` restores them, but nothing
calls `disarm()` during a session. While the WOM engine is armed, the gyro is
off, the accelerometer runs at about 50 Hz with a 16-sample average, and the
spirit level reads that mode. The BMI270 path does not change the accelerometer
mode at all. "IMU on" therefore means a different sensor mode and a different
current on the two chips. Measurements must state the backend and whether the
engine armed.

Citation: `src/FurbleIMUMotionMPU6886.cpp:109-178`; plan 20 lines 170-181.

## 6. Ranked lists

### 6.1 Safe now

These are code and schedule changes with no product behavior change on the
happy path. They still need tests, and G-03 changes a fault path.

| Rank | ID | Why now | Est. saving | Effort |
| --- | --- | --- | --- | --- |
| 1 | G-03 | A silent fixed-baud receiver holds `NO_LIGHT_SLEEP` forever. Reached by enabling GPS with no unit at the default baud. | Up to 37 mA in the fault case | S to M |
| 2 | G-01 | The task knows its next deadline and still wakes 10 times per second. | 0.05 to 0.5 mA | S |
| 3 | G-02 | GPS is off by default, so this is the only GPS cost a default install pays. | 0.02 to 0.2 mA | S |
| 4 | G-13 | Per-second log lines are pure overhead and they pollute the console during every measurement. | 0.05 to 0.2 mA | S |

G-14 (buffer sizing) is also safe but is a correctness fix, not a power win. Do
it with the same batch and do not claim mA for it.

No IMU item is safe-now. The `IMU` setting gates the whole path, the gesture
timer is created only when a gesture is enabled, the level timer runs only while
that page is open, and I-01 was withdrawn because the wake-line sample is
already gated on an armed wake source. The remaining IMU items either change
observable behavior or need a current measurement first.

### 6.2 Needs measurement

Ranked by potential saving, then by how much of the answer is unknown.

| Rank | ID | What measurement decides | Potential | Effort |
| --- | --- | --- | --- | --- |
| 1 | G-04 | In-circuit standby current and wake-to-fix. If standby is really sub-mA and wake is seconds, this is the largest GPS saving. | About 31 mA while backed off | M |
| 2 | G-05 | The same receiver delta, gated by detector accuracy. | 15 to 22 mA while stationary | M to L |
| 3 | I-02 | Whether the chip can run accelerometer-only or low power without breaking the level and the engines. | 0.5 to 2.75 mA | M |
| 4 | G-06 | The marginal value of 30 and 60 second duty with Fix Hold, and whether an early wake exists. | 3 to 5 mA at 15 to 30 s | S to M |
| 5 | G-08 | Whether ephemeris replay shortens the measured 107.7 s re-fix after a rail cut. | Up to 0.7 to 0.9 mAh per cold start | M |
| 6 | G-11 | Whether Auto costs more than 9600 saves, and whether the default fix works. | 0.1 to 0.7 mA with the full set | S |
| 7 | G-12 | Whether the receiver honors `$PCAS03` on this unit and how much line time it saves. | 0.1 to 0.7 mA at 9600 | S |
| 8 | I-03 | The tap and shake miss rate at 25 and 10 Hz, and the real poll cost. | 0.25 to 2 mA | S |
| 9 | G-07 | TTFF with and without `AID-INI`, plus NVS wear. | 0.09 to 0.18 mAh per boot | S |
| 10 | G-09 | GPS-only versus multi-constellation current, with fix quality. | Unknown, a few mA plausible | S |
| 11 | G-10 | Whether the receiver current responds to the fix interval at all. | Unknown | S |
| 12 | I-04 | Whether the hardware engine can replace the 50 Hz poll for shake. | Endpoint of I-02 and I-03 | M to L |
| 13 | I-05 | Whether the IMU is already suspended when the setting is off. | Up to 0.685 to 2.79 mA if not | S |
| 14 | I-06 | False wake rate on a tripod, in a bag, on a dashboard. | 0.33 mAh per false wake | S to M |
| 15 | I-07 | The resting magnitude on each board. | Enables G-05 | S |
| 16 | G-21 | Whether MON-HW can identify a disconnected antenna. | Enables G-04 | S to M |

### 6.3 Rejected

| ID | Reason |
| --- | --- |
| G-14 as a power item | No direct mA; the receiver stays on. Keep it as a correctness fix. |
| G-15 | A custom parser is not worth the correctness risk for under 0.1 mA. |
| G-16 | Rail cycling is a net loss by arithmetic on the measured re-fix time for any cut shorter than that time. |
| G-17 | `$PCAS00` persists furble's settings in the module and wears its flash. |
| G-18 | No documented SBAS or QZSS control and no evidence of a saving. |
| G-19 | UART wake loses the triggering character and everything before it. NMEA cannot survive. |
| G-20 | UBX is u-blox. The supported hardware is CASIC. |
| I-01 | Withdrawn. `motionWakeSample()` is already gated on an armed wake source, and the software backend never arms one. |
| I-08 | Under 0.1 mA and it breaks the arm-once contract. |
| I-09 | Bus cost is under 0.1 mA at 1 Hz; the sensor mode is the lever. |
| I-10 | Faster polling is strictly worse. |

Out of scope, not rejected: deep sleep between intervalometer shots (plan 19),
and any change to `MAX_AGE_MS` (plan 21 explicitly forbids it).

## 7. Measurement plan

No part of this plan has been run. It is written so the next person can run it
without re-deriving the states.

### 7.1 Instrumentation

- Use a calibrated inline power analyzer at one declared boundary, preferably
  the complete device input with charging disabled. Record analyzer model,
  range, sample rate, supply voltage, cable, battery presence and whether USB
  data is attached. Retain raw samples, not only a displayed average.
- Add a second channel with a series shunt on the Grove Port A 5 V rail. This
  separates the GPS unit from the board and is the only way to measure the
  receiver's standby current without cutting the board.
- On the S3 there is no battery current register. `Platform::readBattery()` says
  so in code: the M5PM1 reports voltage and charge status only. The console
  current field is therefore not a measurement on that board. On the AXP192
  boards `M5.Power.getBatteryCurrent()` exists, but it is a PMIC ADC reading,
  not a calibrated instrument.
- Flash `m5stick-s3-debug` for the S3 and `m5stick-c-plus-debug` for the
  MPU6886 board. Both build with the serial console and verbose local logging.
- Align analyzer samples and firmware events with one visible marker. A spare
  GPIO toggled at the start and end of each state is the simplest; the console
  timestamp is the fallback.

### 7.2 Instrumentation gaps to close first

These are test enablers, not power opportunities. Without them, G-01 and G-02
cannot be asserted in firmware and I-03 has no poll count to compare.

| Gap | Minimal addition |
| --- | --- |
| No per-task wake count | A `gps loops` counter in `GPS::task()`, a `motion polls` counter in `MotionSource` and a `gesture polls` counter in the gesture timer, printed by `gps status`, `motion status` and the `imu` console command. |
| `gps status` does not print `failedChecksum()` | Add `failed: N` next to `chars` and `sentences`. |
| No GPS standby residency | Add `standby_ms` accumulated in `enterStandby()` and cleared on leave, or derive it from the lock trace. |
| No IMU mode report | Add `imu_mode` and `gyro` to `motion status` or `imu status`, read from the chip. |
| No light-sleep residency counter | Use the external analyzer trace. `esp_pm` exposes locks, not residency, in the public API. |

### 7.3 Scenario matrix

Every scenario runs for at least 30 minutes after a 5-minute settle, three
repeats, with the same antenna position and sky view. GPS runs are sensitive to
satellite geometry and weather, so record date, time, location and satellite
count.

| # | State | Settings | What it decides |
| --- | --- | --- | --- |
| 1 | GPS off, IMU off, BLE off, screen off | defaults | G-02 baseline; the only GPS cost a default install pays |
| 2 | GPS on, no unit attached, fixed 9600 | `gps on`, `gps_baud 9600` | G-03; confirm the lock is held, then confirm the bounded build releases it |
| 3 | GPS on, unit attached, fix, always on | `gps on`, default | The 31.64 mA receiver baseline and the board overhead |
| 4 | GPS on, unit attached, fix, standby 5/10/15 s | `gps_power 1`, `gps_duty 5/10/15` | In-circuit standby current, duty saving, fix age |
| 5 | GPS on, unit attached, fix, pruned 1 Hz | `gps_nmea on` | G-12 line time and parser work |
| 6 | GPS on, unit attached, fix, pruned 500/200 ms | `gps_nmea on`, `gps_rate 2/3` | G-10 and the buffer headroom |
| 7 | GPS on, unit attached, shielded antenna | `gps on` | G-04 baseline; time to fix once uncovered |
| 8 | Cold boot, assist off / AID-INI / ephemeris | `gps_assist 0/1/2` | G-07 and G-08 TTFF |
| 9 | Baud 9600 versus 115200 versus Auto | `gps_baud 9600/115200/0` | G-11 |
| 10 | Constellation default / GPS only / all | `gps_constel 0/1/7` | G-09 |
| 11 | IMU off, IMU on software, IMU on hardware | `imu on`, `hw_motion 1/2` | I-02, I-05, backend overhead |
| 12 | Wake Gesture shake, still, display off | `imu_wake 2` | I-03, I-04 baseline |
| 13 | Wake Gesture shake, tripod in wind, bag, dashboard | `imu_wake 2` | I-06 false wake rate and display-on time |
| 14 | Level page open versus closed | `imu on`, Level page | Gyro and level cost, I-02 |
| 15 | `motion status` pin and edges while still | any hardware backend | Plan 20 gate step 1, wake source sanity |

### 7.4 Metrics and acceptance

For each run, report mean, median, p95, minimum and maximum input current, the
95 percent confidence interval of the mean, and the state residency from the
firmware log. Report event counts: GPS bursts, lock acquires, failed checksums,
motion transitions, display wakes and camera commands.

A change is accepted only when:

1. The lower bound of the 95 percent CI of the difference is above zero, and
   the difference is at least 1 percent of the baseline state's current. This
   filters analyzer noise and run-to-run drift. The 1 percent is a policy
   threshold, not a hardware noise measurement; the actual band must come from
   repeated baseline runs.
2. The correctness gates pass: no increase in `failedChecksum`, fix age inside
   `MAX_AGE_MS`, a correct EXIF position and timestamp on the Fujifilm body, no
   missed or extra frames, and no new false wake on the tripod run.
3. The state did not silently change during the run. Reject any run where the
   display, GPS state, camera connection or connection interval moved.

For a change that is expected to save less than the noise floor, such as G-13,
report the console line volume and the CPU time instead, and do not claim a
current saving.

### 7.5 Order

1. Run scenario 1 and 2 first. They close G-02 and G-03, the two safe-now items
   that need only a current baseline and a lock trace.
2. Run scenario 3 and 4. They close G-04 and G-06's standby term, which is the
   gate for all receiver duty work.
3. Run scenario 11, 12 and 14. They close I-02 and I-03, the largest IMU terms.
4. Run scenario 5, 6, 9 and 10. They close the remaining GPS configuration
   questions.
5. Run scenario 7, 8 and 13 last. They are the slowest and the most sensitive to
   sky and environment.

### 7.6 What each result decides

- If standby is above about 5 mA in circuit, G-04 and G-05 lose most of their
  value. Measure before writing either policy.
- If wake-to-first-fix after 10 seconds of standby is longer than 10 seconds,
  the duty ceiling stays at 15 seconds and G-06 is not worth it.
- If `AID-INI` does not shorten the cold boot measurably, leave `GPS_ASSIST` at
  0 and do not add NVS writes for it.
- If GPS-only current is within the noise of the full constellation, do not ship
  a constellation default; keep it a user choice.
- If the BMI270 cannot run accelerometer-only without breaking the any-motion
  engine, I-02 collapses to "disable the gyro only", with a smaller saving.
- If the false wake rate on a tripod is non-zero, fix I-06 before enabling any
  motion-adaptive GPS default. A display kept awake by a false wake costs more
  than the GPS policy saves.

## 8. Citations and datasheet register

### 8.1 Code anchors

| Area | Anchor |
| --- | --- |
| GPS task, waits, enable and disable | `src/FurbleGPS.cpp:213-283`, `:314-391`, `:530-593`, `:1942-1992` |
| GPS power cycle and standby | `src/FurbleGPS.cpp:595-936`, `:848-872` |
| GPS configuration, NMEA and CASIC | `src/FurbleGPS.cpp:989-1064`, `:1299-1413`, `:1860-1940` |
| GPS parsing and snapshots | `src/FurbleGPS.cpp:2560-2585`, `:2680-2707`, `:2858-2898` |
| GPS constants and cache layout | `include/FurbleGPS.h:351-409`, `:270-282` |
| GPS hold bounds | `include/FurbleGPSHold.h:21-62` |
| GPS degraded retry and lock gate | `include/FurbleGPSPowerCycle.h:22-104` |
| IMU source and software backend | `src/FurbleIMU.cpp:24-136`, `:244-261` |
| BMI270 engine | `src/FurbleIMUMotionBMI270.cpp:17-181`, `:183-325` |
| MPU6886 engine | `src/FurbleIMUMotionMPU6886.cpp:17-86`, `:88-233` |
| Motion API | `include/FurbleIMU.h:28-190` |
| UI timers and motion consumer | `src/FurbleUI.cpp:415-462`, `:9681` |
| Gesture thresholds | `include/FurbleUIGesture.h:45-53`, `src/FurbleUIGesture.cpp:70-190` |
| Platform IMU and wake path | `src/FurblePlatform.cpp:41-48`, `:401-605`, `:674-740` |
| Defaults | `src/FurbleSettings.cpp:545-760` |
| Per-second logs | `src/FurbleControl.cpp:155-158`, `src/FurblePower.cpp:84-92`, `:118-130` |
| PM and log config | `sdkconfig.m5stick-s3:2110-2120`, `:2466`, `:2491`, `:2585-2594` |
| TinyGPSPlus pin | `platformio.ini:22-26` |

### 8.2 Project evidence

| Document | What it supports |
| --- | --- |
| `plans/00-hardware-experiments.md` | Experiment A (no 32 kHz crystal, 3.3 mA floor); Experiment B (PCAS12, pruning 710 to 91, 107.7 s cold start, no backup supply, ANTENNA OPEN) |
| `plans/implemented/14-gps-pcas.md` | PCAS02/03/04/10 definitions and defaults; no PCAS00; rate and pruning ordering |
| `plans/15-gps-power.md` | Burst window design, UART wake limitation, standby vs rail cycling, 15 s duty ceiling |
| `plans/18-gps-motion.md` | Motion detector phase 1, no consumer, 60 s hold, immediate exit, blind spots, hardware gate |
| `plans/implemented/20-imu-hw-motion.md` | Three backends, register level detail, interrupt path, hardware gate, software bias limitation |
| `plans/32-gps-advanced.md` | Autobaud, AID-INI, ephemeris replay, MON-HW, satellite page, unverified receiver items |
| `plans/98b-power-followup.md` | Degraded retry, +37 mA held-lock figure, no permanent latch |
| `plans/111-power-recommendations.md` | S3 measurement protocol, no battery current register, state list |
| `tools/power-model/board-currents.yaml` | Per-board current table with confidence tags |
| `tools/power-model/README.md` | Model limits: peripheral current not charged, standby current estimated |
| `docs/supported-hardware.md` | Supported GPS units, patch antenna limits, baud defaults |
| `docs/settings-and-controls.md` | User-facing settings, defaults and applies timing |

### 8.3 Datasheets and protocol documents

| Part or protocol | Document and limit | URL |
| --- | --- | --- |
| M5Stack Unit GPS v1.1 | Product page, 5 V 31.64 mA, ATGM336H with AT6668, no backup supply listed | https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1 |
| ATGM336H module | Manual C90770_ATGM336H-5N31, 2016-12-26: GPS+BDS below 25 mA typical, 100 mA peak, VBAT 10 uA | https://github.com/Albresky/GPS-ATGM336H-Library/blob/master/manual/C90770_ATGM336H-5N31_2016-12-26.pdf |
| AT6558 chip (AT6668 proxy) | Datasheet: 23 mA typical BD+GPS tracking, standby below 10 uA, active antenna feed 2.4 to 3.6 mA | https://dratek.cz/docs/produkty/1/1045/at6558.pdf |
| CASIC protocol | Multimode navigation receiver protocol: `$PCAS` family, `BA CE` framing, AID-INI, CFG-NAVX, MON-HW. The printed checksum formula is wrong; the L76K id-first form is the one the code uses | http://www.espruino.com/files/CASIC_en.pdf |
| Quectel L76K | GNSS protocol specification v1.1: PCAS02, PCAS03, PCAS04, PCAS10, GSV and GSA fields, id-first checksum | https://www.waveshare.net/w/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf |
| Espruino Bangle.js 2 | Worked PCAS00, PCAS02, PCAS03 and PCAS04 examples on an AT6558; note that rates below 500 ms need pruning | https://www.espruino.com/Bangle.js2+Technical |
| M5StickS3 | Product page: BMI270, 250 mAh, M5PM1 device current table | https://docs.m5stack.com/en/core/StickS3 |
| M5StickS3 low power | BMI270 INT1 to M5PM1 GPIO4, PYG1_IRQ to ESP32-S3 GPIO13, M5PM1 wake API | https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1 |
| M5PM1 | Function reference: `gpioSetWakeEnable`, `gpioSetWakeEdge`, `irqSetGpioMask`, `irqGetGpioStatus`, `irqClearGpioAll` | https://github.com/m5stack/M5PM1/blob/main/README_FUNCTION_EN.md |
| Bosch BMI270 | Datasheet BST-BMI270-DS000-08: 685 uA normal accel+gyro, 4 to 10 uA accelerometer low power, 3.5 uA suspend | https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf |
| Bosch BMI270 API | Any-motion and no-motion feature layout, 20 ms per duration count, 0.48 mg per threshold count, 83 mg and 70 mg defaults | https://github.com/boschsensortec/BMI270_SensorAPI |
| TDK MPU-6886 | Datasheet v1.1: 2.79 mA gyro+accel low noise, 40 uA accelerometer low power at 100 Hz, 6 uA sleep | https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/MPU-6886-000193%2Bv1.1_GHIC_en.pdf |
| MPU6886 WOM registers | M5StickC-Plus driver: `ACCEL_WOM_X/Y/Z_THR` 0x20 to 0x22, `INT_PIN_CFG` 0x37, `INT_ENABLE` 0x38, `ACCEL_INTEL_CTRL` 0x69 | https://github.com/m5stack/M5StickC-Plus/blob/master/src/utility/MPU6886.h |
| ESP32-S3 | Datasheet v2.2: 40.2 mA active at 80 MHz dual core, 0.240 mA light sleep | https://documentation.espressif.com/esp32-s3_datasheet_en.pdf |
| ESP-IDF sleep | Sleep modes: GPIO wake is light sleep only and level triggered; UART wake loses the trigger character and everything before it | https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/sleep_modes.html |
| ESP-IDF UART | UART wakeup behavior and the fixed clock requirement during sleep | https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/uart.html |
| ESP-IDF power management | `esp_pm_configure`, `ESP_PM_NO_LIGHT_SLEEP`, lock semantics | https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/power_management.html |
| ESP-IDF GPIO | `gpio_wakeup_enable` accepts only low or high level | https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/gpio.html |
| M5Unified IMU | `IMU_Class` API: `begin`, `init`, `sleep`, `update`, `getAccel`, `getGyro`, `getType`, `isEnabled`, no feature-engine access | https://docs.m5stack.com/en/arduino/m5unified/imu_class |
| M5Unified I2C | `I2C_Class`: `readRegister`, `writeRegister`, `writeRegister8`, `readRegister8`, `bitOn`, `bitOff` | https://github.com/m5stack/M5Unified/blob/master/src/utility/I2C_Class.hpp |
| M5Unified Power | `setExtOutput` and `getBatteryCurrent` | https://raw.githubusercontent.com/m5stack/M5Unified/master/src/utility/Power_Class.hpp |
| TinyGPSPlus | Fork pinned at `gkoh/TinyGPSPlus#92ac8c2` | https://github.com/gkoh/TinyGPSPlus |

### 8.4 What this document does not establish

It does not establish the in-circuit standby current, the wake-to-fix time, the
TTFF benefit of assistance, the IMU current on any board, or the energy of a
scheduler wake. It does not validate the simulator's model. It does not certify
the plan 20 interrupt path, which has still not run on hardware. It does not
claim that any safe-now item has been measured; the safe-now ranking is based on
code reading and the reference loads above, and each item still needs the test
named with it.
