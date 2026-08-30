# PR20 - Hardware motion detection

## Goal

Let the IMU watch for motion in hardware and raise an interrupt on a state
change. The CPU then sleeps instead of polling the accelerometer. This replaces
the software variance poll of PR18 where the hardware supports it, and keeps the
software detector as a fallback everywhere else.

## Implementation state

Implemented on `feat/20-hw-motion`:

- Added the opt-in `MotionSource` API and software fallback detector.
- Added raw BMI270 any-motion and no-motion configuration.
- Added MPU6886 wake-on-motion configuration and one-second status polling.
- Added M5StickS3 M5PM1 GPIO4 to GPIO13 wake setup and StickC GPIO35 wake setup.
- Added the `HW_MOTION` setting with provisional wire ID 34.
- Added the Motion Engine roller and IMU diagnostics rows.

The checked out base contains PR16 only. PR17 and PR18 motion consumers are not
present. The source is therefore opt-in and does not arm at boot. This preserves
the current runtime behavior until a consumer subscribes and calls `arm()`.

The setting uses NVS key `hw_motion`. Values are Auto, Software, and Hardware.
The default is Auto. The hardware interrupt wiring is not hardware tested.

No sdkconfig file was changed. The required build reached the sandbox
permission and component-registry blockers. It did not reach compilation.

## Scope

In scope:

- BMI270 any-motion and no-motion engines on M5StickS3.
- MPU6886 wake on motion on M5StickC and M5StickC Plus.
- A motion source abstraction with a hardware backend and a software backend.
  PR18 consumes events from it instead of polling.
- Interrupt to GPIO wiring where the interrupt actually reaches an ESP32 pin,
  including light sleep GPIO wakeup.
- Automatic fallback to the PR18 software detector when no hardware engine or no
  usable interrupt line exists.

Out of scope:

- Tap and double tap. BMI270 tap detection lives in the legacy feature config.
  PR17 keeps its software detector for that.
- Deep sleep and power off wake through the M5PM1. That is PR19.
- Any new user visible feature. This PR changes how PR17 and PR18 detect motion,
  not what they do.

## Files to change

Verified anchors against the current tree.

| File | Lines | What |
|---|---|---|
| `include/FurbleIMU.h` | new | Motion source interface. `enum class MotionState {MOVING, STATIONARY}`, `arm()`, `disarm()`, `poll()`, event callback registration. |
| `src/FurbleIMU.cpp` | new | Software backend (moved from PR18) plus the board dispatch. |
| `src/FurbleIMUMotionBMI270.cpp` | new | BMI270 any-motion and no-motion setup and interrupt status decode. |
| `src/FurbleIMUMotionMPU6886.cpp` | new | MPU6886 wake on motion register setup. |
| `src/FurblePlatform.cpp` | 16-22 | `M5.config()` block. `cfg.internal_imu` at line 18 becomes the PR16 setting. The IMU must be up before any engine is armed. |
| `src/FurblePlatform.cpp` | 34-39 | `FURBLE_M5STICKS3` M5PM1 init block. Add the M5PM1 GPIO4 and IRQ mask setup here if hardware verification shows the chained interrupt works. |
| `src/FurblePlatform.cpp` | 65-72 | `setSleep()`, the single `esp_pm_configure()` call. PR06 replaces it with named locks. GPIO wakeup arming belongs next to it. |
| `include/FurblePlatform.h` | 36-41 | `powerOff()` and `setSleep()` declarations. Add the GPIO wake arm and disarm helpers. |
| `src/FurbleGPS.cpp` | 111-133 | `enable()` and `disable()`. Line 120-123 disables light sleep on S3 whenever GPS is on. That lock is what PR15 makes window based and what this PR depends on. |
| `src/FurbleGPS.cpp` | 150-158 | `startService()`, the 1 Hz `SERVICE_MS` timer. PR18 hangs motion evaluation here. This PR turns that into an event handler. |
| `src/FurbleUI.cpp` | 53-76 | `m_Menu` grid map. One entry if the override roller lands. |
| `src/FurbleUI.cpp` | 1911-1933 | Inactivity roller. Pattern for the optional Motion Engine roller. |
| `src/FurbleUI.cpp` | 2062-2082 | `addSettingsMenu()`. `Sensors` was created by PR16. |
| `platformio.ini` | 15-19 | `lib_deps`. Only touched if the Bosch API is pulled in as a library instead of a component. |
| `components/` | new dir | `components/bmi270/` if the Bosch API is vendored as an ESP-IDF component. `components/icons/CMakeLists.txt` is the pattern. |

## New settings

None required. This is the recommended outcome.

`IMU_WAKE` (PR17) and `GPS_MOTION` (PR18) already express user intent. Whether
the detection runs in hardware or software is a platform detail, not a
preference. Adding a third key would make the user responsible for a choice they
cannot evaluate.

One optional key for field debugging:

| Enum | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|
| `IMU_HWMOT` | `imu_hwmotion` (12) | `FURBLE_STR` | `uint8_t` | `0` | 0 auto, 1 force software, 2 force hardware. 0 selects the current PR18 software path on any board where hardware is not verified, so the default preserves behaviour. |

Name string: `"Motion Engine"`. Drop this key before merge if hardware detection
proves stable. It exists so a bug report can be narrowed without a rebuild.

## Menu placement

```
Settings
└─ Sensors
   ├─ IMU              (PR16)
   ├─ Wake Gesture     (PR17)
   ├─ Double-Tap Shutter (PR17)
   └─ Motion Engine    (roller: Auto / Software / Hardware, optional)

Settings
└─ Diagnostics
   └─ IMU live         (PR16, gains engine state lines)
```

The IMU live page gets three extra lines: active backend, current motion state,
and an interrupt counter. Those lines are the primary debugging tool for this
PR.

## Implementation notes

### What M5Unified gives and does not give

`IMU_Class` exposes `begin`, `init`, `sleep`, `setClock`, `update`,
`getImuData`, `setAxisOrder`, `getAccel`, `getGyro`, `getMag`, `getTemp`,
`isEnabled`, `getType`, `setINTPinActiveLogic`, the calibration offset helpers,
`getRawData` and `getImuInstancePtr`. There is no register access, no feature
configuration and no wake on motion call. `setINTPinActiveLogic` only sets the
polarity of the interrupt pin. So any hardware engine work needs a path around
M5Unified.

M5Unified does provide the bus. `I2C_Class` exposes `writeRegister8`,
`readRegister8`, `readRegister`, `bitOn` and `bitOff`, and declares the `In_I2C`
instance used for internal devices. `FurblePlatform.cpp:35` already passes
`&M5.In_I2C` to the M5PM1 driver, so the pattern is established. On StickS3 the
BMI270 is on the internal bus at address 0x68.

### BMI270 option A: Bosch BMI270_SensorAPI

Vendor `boschsensortec/BMI270_SensorAPI` as `components/bmi270/`, following the
`components/icons` layout. Bind `bmi2_dev.read`, `bmi2_dev.write` and
`bmi2_dev.delay_us` to `M5.In_I2C`.

Calls needed:

- `bmi270_init(&dev)` uploads the feature config file and populates the device
  struct.
- `bmi270_set_sensor_config(cfg, n, &dev)` with `BMI2_ANY_MOTION` and
  `BMI2_NO_MOTION` config structs.
- `bmi270_sensor_enable(list, n, &dev)`.
- `bmi270_map_feat_int(int_cfg, n, &dev)` to route the features to INT1.

Trade-offs:

- Correct by construction. The any-motion and no-motion feature words live in
  an undocumented feature page. The Bosch code is the only public description of
  their layout.
- Licence is BSD-3-Clause, compatible with this project.
- Adds roughly 8 kB of config blob plus the driver to the image, and the blob is
  written over I2C at init. At 400 kHz that is a few hundred milliseconds of boot
  time, once.
- M5Unified already ran its own BMI270 init during `M5.begin()`. Running
  `bmi270_init` again re-uploads the config and resets the accel and gyro
  configuration to Bosch defaults. Re-apply the ranges afterwards, or accept the
  Bosch defaults and check that the PR16 spirit level still scales correctly.
  This is the main integration risk and must be checked on device.
- Two drivers share one chip and one bus. Data register reads through
  `M5.Imu.getAccel()` stay valid, but configuration writes must not interleave.
  Do all engine configuration from the LVGL thread, same as every other I2C user
  in this project.

### BMI270 option B: raw I2C register writes

Write the feature config words directly through `M5.In_I2C.writeRegister8` and
read `INT_STATUS_0` for the feature interrupt source.

Trade-offs:

- No new dependency, no config blob, a few hundred bytes of code.
- Needs the feature page offsets and bit layouts, which are not in the
  datasheet register map. They have to be copied out of the Bosch source anyway,
  so the correctness argument for option A applies without its safety.
- Silent breakage risk. A wrong offset writes into an adjacent feature and the
  engine simply never fires. Debugging that on device is expensive.
- Skips the double init problem. M5Unified already uploaded the config file, so
  the engines are usable after only the feature words and the interrupt map are
  written.

### Recommendation

Start with option B for the interrupt map and the two feature enables, because
it keeps the diff small and does not disturb the M5Unified init. Take the exact
register offsets from the Bosch source and cite them in comments. If the engines
do not fire reliably on device within a short timebox, switch to option A and
accept the boot cost. Decide with hardware in hand, not in review.

### MPU6886 path

The MPU6886 has wake on motion, not no-motion. Registers, taken from the
M5StickC-Plus driver header: `INT_PIN_CFG` 0x37, `INT_ENABLE` 0x38,
`ACCEL_WOM_X_THR` 0x20, `ACCEL_WOM_Y_THR` 0x21, `ACCEL_WOM_Z_THR` 0x22,
`ACCEL_INTEL_CTRL` 0x69, `SMPLRT_DIV` 0x19, `PWR_MGMT_1` 0x6B, `PWR_MGMT_2`
0x6C. The M5StickC library wraps this as
`enableWakeOnMotion(Ascale ascale, uint8_t thresh_num_lsb)`. furble uses
M5Unified, not that library, so write the same sequence through `M5.In_I2C`.

Consequence for PR18: on MPU6886 boards the hardware gives the motion resume
edge only. Entry into the stationary state still needs a timer, but it can run
at 1 Hz instead of the 10 Hz variance poll, because the only question is whether
a motion interrupt arrived in the last N seconds. That is already most of the
saving.

### Interrupt wiring per board

| Board | IMU | INT destination | Usable by the ESP32 |
|---|---|---|---|
| M5StickC | MPU6886 | GPIO35, shared `SYS_INT` net with the BM8563 RTC | Yes, active low. The official M5Stack example calls `esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, LOW)`. GPIO35 is input only with no internal pull, the net relies on an external pull-up. Read both device status registers to identify the source. |
| M5StickC Plus | MPU6886 | Same net per the M5StickC Plus schematic | Treat as GPIO35, confirm on the board before enabling. |
| M5StickC Plus SE, Plus2 | varies | pin map changed | Not assumed. Verify from the schematic PDF, otherwise software fallback. |
| M5Stack Core, Core2 | varies | not verified | Software fallback until someone checks a schematic. |
| M5StickS3 | BMI270 | M5PM1 GPIO4 (PYG4), not an ESP32-S3 pin | See below. |

StickS3 is the important and awkward case. The M5Stack low power guide documents
the BMI270 INT1 going to M5PM1 GPIO4, and the M5PM1 driving an IRQ line named
PYG1_IRQ to ESP32-S3 GPIO13. The M5PM1 API has `gpioSetWakeEnable`,
`gpioSetWakeEdge`, `irqSetGpioMask`, `irqGetGpioStatus` and `irqClearGpioAll`,
so unmasking a GPIO4 edge into the M5PM1 IRQ status is expressible. What is not
documented is whether a GPIO4 edge asserts PYG1_IRQ while the system is running,
as opposed to only waking the M5PM1 from its own sleep. Verify this on hardware
before writing any code that depends on it.

Two outcomes:

- If PYG1_IRQ fires: the S3 gets a true event path. Arm
  `gpio_wakeup_enable(GPIO_NUM_13, GPIO_INTR_LOW_LEVEL)` plus
  `esp_sleep_enable_gpio_wakeup()`, then on wake read `irqGetGpioStatus()` to
  confirm the source and `irqClearGpioAll()` to clear it. Note the extra I2C
  round trip per event.
- If PYG1_IRQ does not fire: the S3 keeps the BMI270 engines armed but reads
  `INT_STATUS_0` on a 1 Hz timer. The CPU still sleeps for the whole second, and
  the accelerometer stream is never read. That is still much cheaper than the
  PR18 10 Hz variance poll, and it is the honest expected outcome. Plan for it.

### Light sleep integration

All five sdkconfigs already have `CONFIG_PM_ENABLE=y` and
`CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`, and `Platform::setSleep()` calls
`esp_pm_configure()` with `light_sleep_enable`. So automatic light sleep is the
mechanism, not manual `esp_light_sleep_start()`.

Consequences:

- GPIO wakeup is a light sleep only source and is level triggered.
  `gpio_wakeup_enable()` accepts only `GPIO_INTR_LOW_LEVEL` or
  `GPIO_INTR_HIGH_LEVEL`. Match the level to the IMU interrupt polarity. Both
  BMI270 and MPU6886 are configured active low here, so use low level.
- With automatic light sleep the wake source is armed once at setup, not before
  each sleep entry. Arm on `arm()`, disarm on `disarm()`.
- A level triggered source held asserted prevents re-entry into light sleep.
  Clear the IMU interrupt status promptly in the handler, or configure the
  BMI270 interrupt as non latched so the pin releases by itself.
- Register the GPIO ISR with `ESP_INTR_FLAG_IRAM` only if the handler is IRAM
  safe. It is simpler to set a FreeRTOS notification and do the I2C work in a
  task.
- On S3 today, `GPS::enable()` calls `Platform::setSleep(false)`
  (`src/FurbleGPS.cpp:120-123`), so light sleep is off whenever GPS is on. That
  is exactly the case this PR wants to optimise. This PR is only worth merging
  after PR15 makes that lock window based.

### PR18 integration

PR18 currently owns a 10 Hz poll, a 5 s rolling variance and a 60 s entry hold.
Replace the input, keep the policy.

New shape:

- `IMU::MotionSource` emits `STATIONARY` and `MOVING` events.
- The hardware backend maps no-motion to `STATIONARY` and any-motion to
  `MOVING`. Set the BMI270 no-motion duration to the same 60 s PR18 used for its
  software hold, so behaviour matches between backends.
- Arm only one engine at a time. While moving, arm no-motion. While stationary,
  arm any-motion. This keeps the interrupt source unambiguous and avoids reading
  the feature status register just to decide which event happened.
- The PR18 GPS policy becomes a pure event handler: on `STATIONARY` apply the
  standby, rail cut or low rate policy; on `MOVING` restore full rate. No timer
  is needed on the GPS side beyond the existing 1 Hz service timer.
- Keep the asymmetry PR18 relies on. Exit from stationary must be immediate.
  Set the any-motion duration to the minimum the chip supports.

PR17 keeps its own detector. Tap needs the accelerometer stream or the BMI270
legacy feature config, and neither is in scope here. If `IMU_WAKE` is set to
shake only, PR17 can subscribe to `MOVING` events from this module and delete
its 50 Hz timer for that case. Do that as a follow up, not in this PR.

### Fallback

Selection order at arm time:

1. `IMU_HWMOT` forces a backend if set.
2. `M5.Imu.getType()` selects the chip specific backend.
3. The backend reports whether its interrupt line is usable on this board.
4. If not usable, fall back to hardware engine plus 1 Hz status polling.
5. If no engine is available, fall back to the PR18 software detector.

Log the chosen backend once at arm time. Show it on the IMU live page. A user
reporting bad battery life must be able to say which path is active.

## Dependencies

- PR16. Hard. The IMU has to be enabled.
- PR17. Shares the sensor. Do not let both configure the chip at once.
- PR18. Hard. This PR replaces its detector input and is pointless without its
  policy.
- PR15. The S3 light sleep gain only appears once the GPS `NO_LIGHT_SLEEP` lock
  is window based.
- PR06. GPIO wake arming belongs next to the pm lock helpers.
- PR05 for the Diagnostics page that shows the backend state.

## Risks

- The S3 interrupt may not reach the ESP32-S3 at all while running. Then the
  whole event driven design degrades to 1 Hz polling. Verify first, design
  second.
- Running `bmi270_init` after `M5.begin()` resets sensor configuration under
  M5Unified. The PR16 spirit level could silently change scale. Check the level
  page after any engine setup.
- Option B writes undocumented feature page offsets. A wrong offset fails
  silently. Bound the debugging effort and switch to option A if it drags.
- Level triggered GPIO wakeup that is never cleared blocks light sleep entirely.
  That is the opposite of the goal and would show as a large idle current
  regression. Test for it explicitly.
- GPIO35 on the StickC family is shared with the RTC. A wake source that
  triggers on RTC events too will produce spurious motion events. Read both
  status registers.
- Threshold and duration units differ between BMI270 and MPU6886. Expect a per
  chip table.
- Two I2C masters in one process. All engine configuration must stay on one
  thread.
- If the hardware path is not clearly better than the PR18 software path in the
  drain runs, do not merge it. Say so in the PR body.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Defaults regression: fresh NVS boot. `IMU` off, `GPS_MOTION` off,
`IMU_HWMOT` auto. No engine armed, no interrupt handler installed, behaviour
identical to master.

Hardware verification first, on M5StickS3 over USB. Do this before writing the
feature code:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Small test sketch or debug page: arm BMI270 any-motion, then poll
   `INT_STATUS_0` at 10 Hz and log transitions. Shake the device. Confirm the
   engine fires at all.
3. Configure the M5PM1 with `irqSetGpioMask` for GPIO4, then poll
   `irqGetGpioStatus()` while shaking. Record whether GPIO4 edges show up in the
   M5PM1 IRQ status while the system is running.
4. Configure ESP32-S3 GPIO13 as an input with a plain GPIO ISR and count edges
   while shaking. This answers the PYG1_IRQ question definitively.
5. Record all three results in the PR body. They decide the S3 design.

Then, functional, on M5StickS3:

1. Enable `IMU` and `GPS_MOTION`. Confirm the IMU live page reports the hardware
   backend.
2. Leave the device still. Confirm a `STATIONARY` event about 60 s later and the
   PR18 policy applied.
3. Move it. Confirm a `MOVING` event within one second and full rate restored.
4. Repeat 20 times. Count missed and spurious events. Report both.
5. Light sleep check: with GPS off and the display off, log
   `esp_pm` behaviour or the uptime versus tick drift over 10 minutes to confirm
   light sleep is actually being entered with the engine armed.

On one AXP192 board, StickC or StickC Plus:

1. Confirm the MPU6886 wake on motion path arms and fires.
2. Confirm GPIO35 edges are seen and that RTC activity does not produce false
   motion events. Leave the device still for 10 minutes and count events.

Camera check, Fujifilm only:

1. Connect to a Fujifilm body with GPS and motion adaptive on. Run the PR18
   camera checks again with the hardware backend active. Geodata must still be
   served on request.

Battery impact, on-board instrumentation only, no external meter:

1. Unplug USB, log battery voltage and percent every 30 s.
2. Run A: 60 minutes stationary with the PR18 software detector.
3. Run B: 60 minutes stationary with the hardware backend.
4. Report both drain slopes. If B is not clearly better, do not merge.

## References

All links checked.

- Bosch BMI270 sensor API. Any-motion and no-motion are in the default feature
  set. `bmi270_init`, `bmi270_sensor_enable`, `bmi270_set_sensor_config` and
  `bmi270_map_feat_int` are declared in `bmi270.h`, with `BMI2_ANY_MOTION` and
  `BMI2_NO_MOTION` as feature selectors. BSD-3-Clause:
  https://github.com/boschsensortec/BMI270_SensorAPI
- BMI270 datasheet PDF, register level detail for the feature engines:
  https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf
- Bosch BMI270 product page, on-chip motion triggered interrupts, 685 uA at full
  ODR: https://www.bosch-sensortec.com/products/motion-sensors/imus/bmi270/
- M5Unified IMU class API. Full method list, no register or wake on motion API:
  https://docs.m5stack.com/en/arduino/m5unified/imu_class
- M5Unified `I2C_Class` header. `writeRegister8`, `readRegister8`,
  `readRegister`, `bitOn`, `bitOff`, and the `In_I2C` instance:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/I2C_Class.hpp
- M5StickC-Plus MPU6886 driver header. WOM threshold registers 0x20 to 0x22,
  `INT_PIN_CFG` 0x37, `INT_ENABLE` 0x38, `ACCEL_INTEL_CTRL` 0x69, and
  `enableWakeOnMotion`:
  https://github.com/m5stack/M5StickC-Plus/blob/master/src/utility/MPU6886.h
- M5StickC wake on motion example. Confirms the IMU interrupt reaches GPIO35 and
  is active low:
  https://github.com/m5stack/M5StickC/blob/master/examples/Advanced/IMU_Wake_On_Motion/IMU_Wake_On_Motion.ino
- StickS3 low power guide. BMI270 INT1 to M5PM1 GPIO4, the PYG1_IRQ line to
  ESP32-S3 GPIO13, `gpioSetWakeEnable`, `gpioSetWakeEdge`, `ldoSetPowerHold`:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- M5PM1 function reference. `gpioSetWakeEnable`, `gpioSetWakeEdge`,
  `irqSetGpioMask`, `irqGetGpioStatus`, `irqClearGpioAll`:
  https://github.com/m5stack/M5PM1/blob/main/README_FUNCTION_EN.md
- ESP-IDF v5.4 sleep modes. GPIO wakeup is light sleep only, armed with
  `esp_sleep_enable_gpio_wakeup()` and `gpio_wakeup_enable()`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/sleep_modes.html
- ESP-IDF v5.4 GPIO API. `gpio_wakeup_enable()` accepts only
  `GPIO_INTR_LOW_LEVEL` or `GPIO_INTR_HIGH_LEVEL`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/gpio.html
- ESP-IDF v5.4 power management. Automatic light sleep needs `CONFIG_PM_ENABLE`
  and `CONFIG_FREERTOS_USE_TICKLESS_IDLE`, plus the `ESP_PM_NO_LIGHT_SLEEP` lock:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/power_management.html
- StickS3 product page, confirms the BMI270: https://docs.m5stack.com/en/core/StickS3
