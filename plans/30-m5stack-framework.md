# PR30 - Upstream M5Stack framework work and fork strategy

## Goal

Several furble plans need capabilities that the M5Stack libraries do not expose.
This document lists each gap, states whether it is real, proposes an upstream API,
names the fallback if upstream says no, and defines exactly how furble pins a fork
while a proposal is in flight.

This is a meta plan. It produces pull requests against `m5stack/M5Unified` and
`m5stack/M5PM1`, not against `gkoh/furble`. The only furble change it ever makes
is one line in `platformio.ini`.

## Scope

In scope:

- Gap analysis against the exact library versions furble pins, not against master.
- Upstream API proposals with a stated fallback for each.
- Fork, branch and pin mechanics.
- The dependency edges from furble PRs to each upstream PR.

Out of scope:

- Any furble user visible feature. Those live in their own plans.
- LVGL and NimBLE. Both come through the IDF component manager and neither has a
  gap that blocks a furble plan today.
- Vendoring M5Unified into the furble tree. Rejected. It is 100k lines and the
  maintenance cost is worse than every fallback listed here.

## How furble pulls in the M5Stack libraries

Verified against the current tree.

`platformio.ini`, `[env]` section:

```
lib_deps =
  M5PM1@1.0.6
  M5GFX@0.2.19
  M5Unified@0.2.13
  https://github.com/gkoh/TinyGPSPlus#92ac8c2
```

Three facts follow.

1. The three M5Stack libraries come from the PlatformIO registry with exact
   version pins. `M5Unified@0.2.13`, not a range.
2. `src/idf_component.yml` pulls only `h2zero/esp-nimble-cpp 2.5.0` and
   `lvgl/lvgl 9.4.0` through the IDF component manager. No M5Stack library comes
   through that path. Do not look for M5Unified there.
3. The TinyGPSPlus line is already a git URL pinned to commit `92ac8c2` on a
   `gkoh` fork. So pinning a fork by commit is an established pattern in this
   project and needs no argument with upstream.

Dependency resolution: `M5Unified` 0.2.13 `library.json` declares
`{"name": "M5GFX", "version": ">=0.2.19"}`. furble pins `M5GFX@0.2.19`
explicitly, so the resolved graph is deterministic. Forking M5Unified does not
force a fork of M5GFX. `M5PM1` 1.0.6 declares no dependencies at all.

There is also a precedent for patching a dependency in place rather than forking
it. `extra_scripts = pre:patches/apply.py` runs `patches/apply.py`, which shells
out to `patch` against `framework-espidf` to apply `patches/ble_gap.patch`. That
mechanism can be pointed at a PlatformIO library directory just as easily.

## Fork and pin mechanics

Forks live under `github.com/MaxRink`. One fork per upstream repository. One
branch per proposal, named `feat/<slug>`, cut from the upstream tag furble
currently pins so the diff stays small and reviewable.

While a proposal is open, furble pins the fork commit:

```
lib_deps =
  M5PM1@1.0.6
  M5GFX@0.2.19
  M5Unified=https://github.com/MaxRink/M5Unified.git#<40 char sha>
  https://github.com/gkoh/TinyGPSPlus#92ac8c2
```

The `name=source` prefix is required here and not optional. PlatformIO's package
specification is `[<name>=]<source>`, and the name overrides the package folder
name in storage. Without `M5Unified=`, the fork lands in a folder named after the
repository URL and M5Unified's own `library.json` dependency on M5GFX by name can
resolve to a second copy.

Always pin a full commit sha, never a branch. A branch pin makes the build
non reproducible and CI will drift silently.

When a proposal merges upstream and a release ships, the pin goes back to a
registry version pin and the fork branch is deleted. That revert is its own tiny
furble PR with a one line diff, which is exactly the granularity upstream wants.

Three rules follow from this.

- No furble PR may be merged into `gkoh/furble` while it depends on a fork pin.
  Upstream must not carry a pin to a personal fork. Such a furble PR stays in
  `MaxRink/furble` until the framework change lands.
- Every furble plan that depends on an upstream proposal must have a fallback
  that works against the pinned registry version. The fallback ships first. The
  upstream API replaces it later.
- The fallback is always direct register access through `M5.In_I2C`, which is
  already how furble reaches the M5PM1 at `src/FurblePlatform.cpp:35`.

## Hardware support matrix

Which boards each gap affects. Verified against M5Unified 0.2.13 source, which is
the pinned version.

| Gap | StickC | StickC Plus | StickC Plus2 | StickS3 | Core | Core2 |
|---|---|---|---|---|---|---|
| G1 IMU motion interrupt API | yes, MPU6886 | yes, MPU6886 | yes, MPU6886 | yes, BMI270 | no IMU | yes, MPU6886 |
| G2 touch low power API | no touch | no touch | no touch | no touch | no touch | yes |
| G3 M5PM1 battery current | - | - | - | refuted | - | - |
| G4 getBatteryCurrent unsupported signal | works | works | works | returns a fake 0 | returns a fake 0 | works |
| G5 M5PM1 I2C wake retry | - | - | - | yes | - | - |
| G6 M5PM1 driver duplication | - | - | - | yes | - | - |

## Files to change

In the upstream repositories, not in furble.

| Repository | File | Gap |
|---|---|---|
| M5Unified | `src/utility/IMU_Class.hpp`, `src/utility/IMU_Class.cpp` | G1 |
| M5Unified | `src/utility/imu/IMU_Base.hpp` | G1 |
| M5Unified | `src/utility/imu/BMI270_Class.hpp`, `.cpp` | G1 |
| M5Unified | `src/utility/imu/MPU6886_Class.hpp`, `.cpp` | G1 |
| M5Unified | `src/utility/Touch_Class.hpp`, `.cpp` | G2 |
| M5Unified | `src/utility/Power_Class.hpp`, `.cpp` | G4 |
| M5PM1 | `src/M5PM1.h`, `src/M5PM1.cpp` | G5 |

In furble, the only change ever made by this plan:

| File | Lines | What |
|---|---|---|
| `platformio.ini` | 15-19 | The `lib_deps` block. One line moves between a registry pin and a fork pin |

## New settings

None. This plan adds no furble settings and changes no defaults. It exists to
unblock settings defined in other plans:

- PR17 `IMU_WAKE`, `IMU_TRIG`
- PR20 hardware motion source selection
- PR12 `DISPLAY_OFF`
- PR02 `BATT_STYLE` battery info page rows

## Menu placement

None.

## Gap analysis

### G1. M5Unified has no IMU interrupt or register level API

**Confirmed.** `IMU_Class` in 0.2.13 and in master exposes `begin`, `init`,
`sleep`, `setClock`, `update`, `getImuData`, `setAxisOrder`, `getAccel`,
`getGyro`, `getMag`, `getTemp`, `isEnabled`, `getType`, `setINTPinActiveLogic`,
the calibration offset helpers, `getRawData` and `getImuInstancePtr`.
`setINTPinActiveLogic` only sets pin polarity. There is no way to configure an
interrupt source, enable wake on motion, or read or write a sensor register.

`getImuInstancePtr(int)` returns an `IMU_Base*`, which looks like an escape
hatch but is not. `IMU_Base` declares only `begin`, `getImuRawData`,
`getConvertParam`, `getTempAdc`, `sleep` and `setINTPinActiveLogic`. Its I2C
helpers are not public.

`BMI270_Class` publishes every register address as a public constant, including
`INT1_IO_CTRL_ADDR` 0x53, `INT1_MAP_FEAT_ADDR` 0x56, `INT_MAP_DATA_ADDR` 0x58,
`FEAT_PAGE_ADDR` 0x2F, `FEATURES_REG_ADDR` 0x30, `PWR_CONF_ADDR` 0x7C and
`PWR_CTRL_ADDR` 0x7D. So the addresses are already public. Only the access
methods are missing. `MPU6886_Class` exposes `enableFIFO`, `setGyroAdcOffset` and
`setINTPinActiveLogic`, and no wake on motion path.

Separately, BMI270 tap detection lives in the Bosch legacy feature config, and
M5Unified ships `BMI270_config.inl`, which is the default config. Any tap engine
work needs a different config blob regardless of what M5Unified exposes. That is
a hardware fact, not an M5Unified gap.

**Proposed upstream API.** Two virtuals on `IMU_Base`, forwarded by `IMU_Class`,
implemented per sensor and returning false where unsupported:

```
struct motion_config_t {
  float threshold_g;      // any-motion / no-motion threshold
  uint16_t duration_ms;   // how long the condition must hold
  bool enable_any_motion;
  bool enable_no_motion;
  int int_pin;            // 1 or 2, mapped to INT1 or INT2
};
virtual bool setMotionDetect(const motion_config_t& cfg);
virtual uint32_t getMotionStatus(void);   // bit mask, clears on read
```

This is small, it is portable across BMI270 and MPU6886, and it does not expose
raw registers, which is the objection a maintainer is most likely to raise.

Offer a register accessor as a second, separate PR so the two can be judged
independently:

```
virtual bool writeSensorRegister(uint8_t reg, const uint8_t* data, size_t len);
virtual bool readSensorRegister(uint8_t reg, uint8_t* data, size_t len);
```

**Fallback if upstream declines.** Direct I2C through `M5.In_I2C`, which
`I2C_Class` already exposes as `writeRegister8`, `readRegister8`, `readRegister`,
`bitOn` and `bitOff`. BMI270 sits at 0x68 on the StickS3 internal bus. PR20
already specifies this path in detail and treats it as the primary approach, with
the upstream API as the later cleanup. That ordering is deliberate: the fallback
ships, then the upstream API replaces it.

Two risks the fallback carries and the upstream API removes. First, M5Unified
already ran its own BMI270 init during `M5.begin()`, so a second init from
furble would fight it. Direct register writes after `M5.begin()` avoid that but
depend on M5Unified's init leaving the feature pages alone. Second, changing
`ACC_CONF` or the full scale range behind M5Unified's back would silently break
`getAccel()` scaling and therefore the PR16 spirit level. Touch only the feature
and interrupt registers.

**Depends on it.** PR17 (gestures) uses the fallback today. PR20 (hardware motion
detection) is the main consumer. PR18 (motion adaptive GPS) consumes PR20.

### G2. M5Unified has no touch controller low power mode

**Confirmed.** `Touch_Class` in 0.2.13 and master exposes `getCount`,
`getDetail`, `getTouchPointRaw`, `setHoldThresh`, `setFlickThresh`, `isEnabled`,
`begin`, `update` and `end`. A grep of the 0.2.13 header for `sleep`,
`powerSave` and `monitor` returns nothing. There is no way to put the FT6336 on
Core2 into monitor or hibernate mode, and `end()` drops the panel pointer without
touching the controller.

**Proposed upstream API.**

```
enum touch_power_t { touch_power_active, touch_power_monitor, touch_power_sleep };
bool setPowerMode(touch_power_t mode);
```

`m5gfx::ITouch` would gain a matching virtual, with the FT5x06 and FT6336
implementations writing the controller mode register. This crosses into M5GFX,
which makes it a two repository proposal and therefore harder to land. Say so in
the proposal and offer to do both.

**Fallback if upstream declines.** Leave the touch controller running. PR12
already made this call and states it plainly: idle touch draw is small next to
the backlight, and leaving it awake means a tap still wakes the screen on Core2
and Tough, which is the behaviour a user expects. So G2 blocks nothing. It is a
power refinement, not a feature gate. Rank it last.

**Depends on it.** PR12 (display off), optional refinement only.

### G3. M5PM1 battery current measurement

**Refuted at the hardware level.** The M5PM1 does not measure battery current, so
there is nothing to propose.

Evidence. The M5PM1 library exposes `readVref`, `getRefVoltage`, `readVbat`,
`readVin` and `read5VInOut`. Its ADC has exactly three channels, defined in
`src/M5PM1.h` as `M5PM1_ADC_CH_1` on external GPIO1, `M5PM1_ADC_CH_2` on external
GPIO2, and `M5PM1_ADC_CH_TEMP` for the internal die temperature. The register map
has a single ADC result pair at 0x28 and 0x29 fed by the channel select at 0x2A.
There is no current register, no shunt, and no coulomb counter. The M5Stack
StickS3 low power guide documents power rails and switching and never mentions
current sensing.

**Consequence for furble.** PR02 must not print a current figure on StickS3. It
already plans to probe and hide the row, which is correct. The battery runtime
estimate on StickS3 has to come from the voltage slope over time, not from a
current reading. Record that here so nobody re-opens the question.

**Do not file this upstream.** Filing a request for a register the chip does not
have wastes a maintainer's time and costs credibility for the proposals that are
real.

### G4. Power_Class::getBatteryCurrent cannot say "unsupported"

**Confirmed, and this is the real version of G3.** In 0.2.13 and master,
`int32_t Power_Class::getBatteryCurrent(void)` switches on `_pmic`:

- `pmic_axp192`: returns charge current minus discharge current. Real value.
  Applies to StickC, StickC Plus and Core2.
- `pmic_axp2101` on ESP32: reads INA3221 channel 1. Real value. This is Core2
  v1.1.
- `pmic_axp2101` on ESP32-S3: hardcoded `return 0`, with the comment "for CoreS3".
- `pmic_m5pm1`: no case at all, so it falls through to the board switch and hits
  `default: return 0`. This is StickS3.
- `pmic_ip5306`: no case, same fall through. This is M5Stack Core Basic.

So on StickS3 and Core Basic the function returns 0, which is indistinguishable
from a genuine 0 mA reading. A caller cannot tell "no current flowing" from "this
board cannot measure current".

Note that 0.2.13 already handles StickS3 properly everywhere else.
`getBatteryVoltage` has a `pmic_m5pm1` case reading M5PM1 register 0x22 and 0x23
through `M5.In_I2C`, `getBatteryLevel` derives from it, and `isCharging` reads
M5PM1 register 0x12. Only current is missing, and it is missing because the
hardware cannot do it.

**Proposed upstream API.** Do not propose an S3 current implementation. It is not
possible. Propose a capability query instead:

```
enum power_feature_t {
  power_feature_battery_voltage = 1 << 0,
  power_feature_battery_level   = 1 << 1,
  power_feature_battery_current = 1 << 2,
  power_feature_charging_state  = 1 << 3,
  power_feature_vibration       = 1 << 4,
};
uint32_t getSupportedFeatures(void) const;
```

One switch on `_pmic` and `M5.getBoard()`, no behaviour change, no risk to
existing callers. It is the kind of additive change a maintainer accepts.

**Fallback if upstream declines.** A per board capability table inside furble,
keyed on `M5.getBoard()`, built from the same evidence collected above. PR02
already specifies "feature detection must be by probe result, not by assumption",
so the fallback is a table plus a startup probe. The cost is that the table has
to be revisited whenever a new board is supported.

**Depends on it.** PR02 (battery display), PR05 (diagnostics), PR13 (low battery
policy). All three ship with the fallback.

### G5. M5PM1 driver does not retry after its I2C idle sleep

**Confirmed.** The M5PM1 enters a low power state after an I2C idle timeout set
by `setI2cSleepTime(uint8_t seconds)`. The vendor function reference lists I2C
sleep as a feature and notes the caveats. The documented behaviour is that the
first transaction after the chip has slept fails and only serves to wake it. The
library does not absorb that. Every caller has to retry.

This hits furble twice. `src/FurblePlatform.cpp:86` calls `m_M5PM1.btnGetState()`
on every update and checks for `M5PM1_OK`, so it already sees the failure. And
M5Unified reads M5PM1 registers directly through `M5.In_I2C` in
`getBatteryVoltage`, `getBatteryLevel` and `isCharging` with no retry at all, so
those return 0 or a wrong charging state on the first call after an idle period.

**Proposed upstream API.** No new API. A behaviour fix inside the M5PM1 library:
when a transaction fails and the configured idle sleep time has elapsed since the
last successful transaction, retry once before returning an error. Add
`setAutoWakeRetry(bool)` defaulting to true so anyone depending on the old
behaviour can turn it off. This is a small, self contained PR against
`m5stack/M5PM1` and is the most likely of all of these to be accepted.

A matching M5Unified PR would route its M5PM1 register reads through the same
retry rather than raw `In_I2C` calls. File it second, after the M5PM1 one lands.

**Fallback if upstream declines.** A retry helper in `Platform` wrapping every
M5PM1 access, which PR02 already specifies. It cannot fix M5Unified's internal
reads, so `M5.Power.getBatteryVoltage()` on StickS3 stays unreliable on the first
call. furble works around that by reading the M5PM1 through its own instance
rather than through `M5.Power` on that board.

**Depends on it.** PR02, PR13, PR19.

### G6. Two M5PM1 drivers in one binary

**Confirmed, and it is a convergence problem rather than a gap.** furble pins the
standalone `M5PM1@1.0.6` library and instantiates `M5PM1 m_M5PM1` in
`include/FurblePlatform.h:52`. M5Unified 0.2.13 talks to the same chip directly
through `M5.In_I2C` without that library. M5Unified master has since grown its
own driver at `src/utility/power/M5PM1_Class.hpp` with an `M5pm1` instance, and
master's `Power_Class` now calls `M5pm1.get5VoutVoltage()` and
`M5pm1.getGPIOInput()` where 0.2.13 used raw register reads.

So a future M5Unified release will contain a full M5PM1 driver, and furble will
be carrying a second one. Two drivers on one I2C device with independent notions
of the chip's sleep state is a real hazard, especially combined with G5.

**Proposal.** Nothing to file. This resolves itself when furble bumps M5Unified
to a release that includes `M5PM1_Class`. At that point drop `M5PM1@1.0.6` from
`lib_deps` and move `Platform::powerOff()`, the button state read and the timer
wake path onto the M5Unified class. Check first that `M5PM1_Class` exposes the
calls furble needs: `setSingleResetDisable`, `setDoubleOffDisable`,
`setDownloadLock`, `btnGetState`, `shutdown`, `timerSet` and the RTC RAM
accessors. If any are missing, that is a concrete, well scoped upstream PR with
a working reference implementation to point at.

**Depends on it.** PR19 (deep sleep intervalometer) uses `timerSet` and RTC RAM
and would have to move with the rest. Do this bump between releases, not during
a feature PR.

## Implementation notes

Order the upstream work by likelihood of acceptance, not by how much furble wants
it. Early acceptance buys standing for the harder proposals.

1. G5, M5PM1 auto wake retry. Small, self contained, fixes a documented wart.
2. G4, `getSupportedFeatures`. Additive, no behaviour change.
3. G1 part one, `setMotionDetect` and `getMotionStatus`. Medium size, needs a
   working BMI270 and MPU6886 implementation and a demo sketch.
4. G1 part two, raw register accessors. Likely to be refused on API design
   grounds. File it only if part one lands.
5. G2, touch power mode. Crosses into M5GFX. File last.

Each upstream PR needs a demo sketch under the library's `examples/` directory,
because that is the house style in both repositories and a proposal without one
reads as a drive by.

Every furble PR that would use an upstream API must first ship its fallback and
must state in its PR body which upstream proposal, if any, would replace the
fallback and where that proposal stands. That keeps `gkoh/furble` free of fork
pins and keeps the reasoning visible to the upstream maintainer.

If a proposal stalls with no maintainer response for a month, close the fork
branch and keep the fallback. Do not leave furble pinned to a fork indefinitely.

## Dependencies

```
G5 -> PR02, PR13, PR19
G4 -> PR02, PR05, PR13
G1 -> PR17, PR20 -> PR18
G2 -> PR12 (refinement only)
G6 -> PR19 (at the next M5Unified bump)
G3   refuted, blocks nothing
```

No furble PR is blocked outright. Every one has a shipping fallback. That is the
point of this document.

## Risks

- Fork pins are contagious. A fork pin in `platformio.ini` affects every build of
  every board, not just the PR that wanted it. Never merge one upstream.
- Branch pins break reproducibility. Always pin a 40 character commit sha.
- Without the `M5Unified=` name prefix, a VCS pin can produce two copies of the
  library in the dependency graph, which fails at link time with duplicate
  symbols and a confusing error.
- Upstream may accept a different API shape than proposed. The furble fallback
  then has to be rewritten a second time. Keep the fallback behind a narrow
  internal interface so only one file changes.
- Direct register access behind M5Unified's back can desynchronise it from the
  chip. For the IMU, restrict writes to feature and interrupt registers and never
  touch scale or output data rate configuration.
- The M5Unified bump for G6 changes the M5PM1 access path on StickS3 and touches
  power off, button reading and the deep sleep path at once. That is a whole PR by
  itself and must not be smuggled into a feature PR.
- M5Stack library releases are not on a schedule. A merged proposal may sit
  unreleased for months. Plan for the fallback to be the shipped code for a long
  time.

## Verification

Upstream PRs are verified in the upstream repository's own terms: the library
builds for its declared frameworks and the demo sketch runs. Below is what furble
does on its side.

Fork pin sanity, before any furble work depends on it:

1. Edit `platformio.ini` to the fork pin form with a real commit sha.
2. Delete `.pio/libdeps` to force a clean resolution.
3. `pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3`.
   All five must build clean with `-Wall -Wextra`.
4. Inspect `.pio/libdeps/<env>/`. There must be exactly one `M5Unified` folder and
   exactly one `M5GFX` folder. Two of either means the name prefix is wrong.
5. Flash the S3 and confirm the boot log and the menu are unchanged. A framework
   pin change must be behaviour neutral on its own.

Per gap, on device over USB, Fujifilm cameras only:

- G5. `pio device monitor` on M5StickS3. Set a short M5PM1 I2C sleep time. Leave
  the device idle past it. Confirm the first battery read after idle returns a
  plausible voltage instead of 0. Before the fix it returns 0 or an error on the
  first call.
- G4. Log `getSupportedFeatures()` on every board. Confirm the current bit is set
  on StickC, StickC Plus and Core2, and clear on StickS3 and Core. Cross check
  against the actual `getBatteryCurrent()` values.
- G1. On M5StickS3, configure any-motion through the new API, log
  `getMotionStatus()`, and confirm a bit sets when the device is moved and clears
  when it is read. Repeat on an MPU6886 board. Confirm `getAccel()` still returns
  the same values before and after, which proves the scale configuration was not
  disturbed. Then confirm the PR16 spirit level still reads level when the device
  is flat.
- G2. On Core2, put the display to sleep with the touch controller in monitor
  mode. Confirm a tap still wakes the screen. Log battery voltage every 30 s for
  30 minutes with the display off in each touch power mode and compare the
  slopes. If the difference is not measurable, drop the proposal.
- G6. After an M5Unified bump that includes `M5PM1_Class`, confirm on StickS3:
  power off works, the power button click count is unchanged, and the PR19 timed
  power on still fires. Then confirm only one code path talks to I2C address
  0x6E.

Regression for every gap: erase NVS, boot, confirm behaviour is identical to the
previous pin. A framework change must never move a default.

## References

All links fetched and checked.

### furble build configuration

- PlatformIO package specification, the `[<name>=]<source>` form and VCS pinning
  by branch, tag or commit, with the
  `ESP32=https://github.com/platformio/platform-espressif32.git#084131f...`
  example:
  https://docs.platformio.org/en/latest/core/userguide/pkg/cmd_install.html
- PlatformIO `lib_deps` library dependencies, git URL with `#ref`:
  https://docs.platformio.org/en/latest/librarymanager/dependencies.html
- PlatformIO `lib_deps` option reference:
  https://docs.platformio.org/en/latest/projectconf/sections/env/options/library/lib_deps.html
- ESP-IDF v5.4.2 component manager, the path used for NimBLE and LVGL only:
  https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-guides/tools/idf-component-manager.html
- M5Unified on the PlatformIO registry, the source of the `M5Unified@0.2.13` pin:
  https://registry.platformio.org/libraries/m5stack/M5Unified

### M5Unified, gaps G1, G2, G4, G6

- M5Unified repository: https://github.com/m5stack/M5Unified
- Open pull requests, to gauge maintainer responsiveness before filing:
  https://github.com/m5stack/M5Unified/pulls
- `library.json` on master, showing the `M5GFX >=0.2.19` dependency:
  https://github.com/m5stack/M5Unified/blob/master/library.json
- `IMU_Class.hpp`, the full public API with no interrupt or register access:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/IMU_Class.hpp
- `IMU_Base.hpp`, the virtual set that `getImuInstancePtr` exposes:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/imu/IMU_Base.hpp
- `BMI270_Class.hpp`, public register address constants, no public accessor:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/imu/BMI270_Class.hpp
- `MPU6886_Class.hpp`, no wake on motion path:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/imu/MPU6886_Class.hpp
- `Touch_Class.hpp`, no power or monitor mode:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/Touch_Class.hpp
- `Power_Class.hpp`, the `getBatteryCurrent` and `setVibration` declarations:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.hpp
- `Power_Class.cpp`, the `getBatteryCurrent` PMIC switch and the StickS3 M5PM1
  register reads:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.cpp
- `M5PM1_Class.hpp` on master, the driver that will make the standalone M5PM1
  library redundant:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/power/M5PM1_Class.hpp
- `M5Unified.cpp`, board pin tables and speaker enable callbacks:
  https://github.com/m5stack/M5Unified/blob/master/src/M5Unified.cpp
- M5Unified IMU class API documentation:
  https://docs.m5stack.com/en/arduino/m5unified/imu_class
- M5Unified Power class API documentation:
  https://docs.m5stack.com/en/arduino/m5unified/power_class

### M5PM1, gaps G3, G5, G6

- M5PM1 repository: https://github.com/m5stack/M5PM1
- `library.json`, confirming no declared dependencies:
  https://github.com/m5stack/M5PM1/blob/main/library.json
- Function reference, listing `readVref`, `readVbat`, `readVin`, `read5VInOut`,
  `setI2cSleepTime`, and no current measurement call:
  https://github.com/m5stack/M5PM1/blob/main/README_FUNCTION_EN.md
- M5Stack StickS3 low power guide, power rails and M5PM1 behaviour, with no
  current sensing anywhere:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- M5Stack StickS3 timed wake up, `timerSet` and `shutdown`:
  https://docs.m5stack.com/en/arduino/m5sticks3/wakeup

### Sensor datasheets, for the G1 proposal

- Bosch BMI270 product page:
  https://www.bosch-sensortec.com/products/motion-sensors/imus/bmi270/
- Bosch BMI270 sensor API, feature engine configuration and the default versus
  legacy feature config split:
  https://github.com/boschsensortec/BMI270_SensorAPI
- Bosch BMI270 sensor API header, feature enable and interrupt mapping calls:
  https://github.com/boschsensortec/BMI270_SensorAPI/blob/master/bmi270.h
- InvenSense MPU-6000 and MPU-6050 datasheet, the wake on motion registers the
  MPU6886 inherits:
  https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Datasheet1.pdf
