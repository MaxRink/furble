# PR20 - Hardware motion detection

## Goal

Let the IMU watch for motion in hardware and raise an interrupt on a state
change. The CPU then sleeps instead of polling the accelerometer. This replaces
the software variance poll of PR18 where the hardware supports it, and keeps the
software detector as a fallback everywhere else.

## Implementation state

Rebased onto master 6245a301. Base is `master`, not `feat/16-imu-spirit-level`.

Shipped on `feat/20-hw-motion`:

- `IMU::MotionSource`, a singleton with a `MotionBackend` interface and three
  backends: software, BMI270 any-motion and no-motion, MPU6886 wake on motion.
- `Platform::armMotionWake()` and `disarmMotionWake()`, the light-sleep GPIO
  wake source, with the M5PM1 GPIO4 to GPIO13 chain on the M5StickS3 and GPIO35
  on the StickC family.
- The `HW_MOTION` setting, wire id 74, NVS key `hw_motion`, values Auto,
  Software and Hardware, default Auto.
- A Motion Engine roller on the Sensors page and three motion rows on the IMU
  live diagnostics page.
- Console `settings get hw_motion` and `settings set hw_motion`.
- The consumer: motion counts as user activity, so picking the device up wakes
  the panel.
- Simulator coverage: a virtual motion engine per chip, two new seeds, four new
  queries, and nine certified scenarios.
- Host coverage: `tests/host/imu_motion_encoding_test.cpp` pins the register
  sequence and the feature word encoding, and the console suite covers the new
  setting.

### What the first draft got wrong

The draft was written against the pre-rebase base and reviewed against master.
Seven things had to change before it was mergeable. They are recorded because
each one is a trap the next hardware feature can fall into.

1. **The feature was unreachable.** `arm()`, `poll()` and `setCallback()` had no
   callers anywhere. The only reference was the diagnostics label, which
   therefore always read `none`, `inactive` and `0`. The Motion Engine roller
   wrote NVS and changed nothing. A user visible setting with no effect, plus
   roughly 640 lines of dead driver code. Fixed by giving it a consumer, see
   below.
2. **Wire ids.** The draft renumbered `IMU` from 46 to 45 and took 47 for
   `HW_MOTION`. Master pins `IMU` at 46, `include/CLAUDE.md` reserves 45 for the
   companion password, and both settings documents state 46. Wire id 47 belongs
   to the companion-password branches per issue #280. The renumber also created
   an add and add conflict on `tests/protocol/golden/settings/*-45.bin` with
   PR #45, which lands first. The renumber is dropped entirely and `HW_MOTION`
   takes 74, the id issue #280 reserved for it. That is the only wire id this PR
   claims.
3. **`appliesImmediately()` and `isDangerous()`** are exhaustive switches over
   `type_t` with no `default`. `HW_MOTION` was in neither, so it was classified
   by accident through the trailing `return false` and warned under `-Wswitch`.
   Both now name it: not immediate, not dangerous.
4. **The roller was unreachable by navigation.** The draft hand rolled it and
   omitted `addToInputGroup`, so no encoder or button board could focus it. It
   also forced `LV_PCT(90)` that the shared `addRollerItem` helper guards with
   `#if !defined(FURBLE_M5COREX)`, and re-set a flex flow `addMenu()` already
   owns. Twenty six lines replaced by the shared helper.
5. **`CompanionService::settingNeedsRestart()`** was declared, defined and never
   called. `Settings::appliesImmediately()` already answers that question for
   the companion, the console and the UI. Deleted.
6. **BMI270 feature access was not gated on advanced power save.** The part
   requires `PWR_CONF.adv_power_save` cleared before `FEAT_PAGE` and the
   `FEATURES` window are reachable, and the draft never touched `PWR_CONF` at
   0x7C, never checked `INTERNAL_STATUS` at 0x21 for the init-ok message, and
   never checked `PWR_CTRL` at 0x7D for `acc_en`. If M5Unified leaves advanced
   power save on after `M5.begin()`, every feature write is silently dropped and
   the engine never fires. This is exactly the silent breakage the option B
   section below warns about, and it would have been found on hardware or not at
   all.
7. **A wrong feature constant.** `NO_MOTION_WORD_1` was 0xB690, which decodes to
   a threshold of 1680 counts, about 806 mg, against an any-motion threshold of
   170 counts, about 83 mg. A no-motion threshold eight times looser than the
   any-motion threshold reports stationary almost always. Checked against the
   Bosch source rather than guessed: `bmi270_examples/no_motion_interrupt`
   states the no-motion default is 70 mg, which is 0x090, one nibble away from
   what the draft carried.

The fix for 6 and 7 was to stop baking whole 16 bit words at all. The engines
now read the feature page, set only the documented fields, and write it back,
which is what `bmi2.c` itself does. Bits 14:11 of each threshold word are the
output configuration, which Bosch never rewrites; preserving them removes four
magic constants and the question of what they were supposed to contain.

Two findings were recorded and deliberately not acted on:

- Neither hardware backend needed a general I2C retry for correctness: a failed
  read makes `poll()` return false and the next attempt is one second later, so
  the worst case is one lost second, not a lost event. The retry is there anyway
  because the internal bus is shared with the M5PM1, whose first transaction
  after idle sleep fails. `Platform::m5pm1Access` already retries once and every
  new PMIC call goes through it.
- The companion app metadata gains no `hw_motion` entry. Master has no entry for
  `imu` either, so the Sensors group is uniformly absent and unknown ids already
  render as read-only rows. Adding one half of the pair would be worse than
  adding neither.

### The consumer

A motion source with no consumer is dead code, so this PR wires one. On master
today that is the panel:

- The source arms at boot when the `IMU` setting is on, and is polled from the
  existing one second housekeeping timer at `src/FurbleUI.cpp:398`. No new
  timer: a hardware engine only needs its status register read, and the software
  backend thresholds one sample.
- On `MOVING` the callback calls `lv_display_trigger_activity()`. That is all it
  needs: `processInactivity()` already wakes a sleeping panel as soon as the idle
  clock is reset. The draft also called `wakeDisplay()`, which the mutation
  testing below proved redundant, so it is gone.
- On `STATIONARY` nothing happens. The inactivity timeout already owns the sleep
  decision.

### Interrupt path corrections from the PR48 review

The abstraction survived review. The interrupt path did not: as first written it
could not have worked on either board, and none of it would have been visible
without hardware.

1. **The BMI270 wake line carried data ready, not motion.** M5Unified writes
   `INT_MAP_DATA` (0x58) 0xFF during `begin()`
   (`BMI270_Class.cpp:60`), which maps data ready to both interrupt pins.
   Enabling the INT1 output without clearing that turns the wake line into a
   roughly 100 Hz pulse train, so the wake source fires continuously and light
   sleep never settles. `arm()` now clears the INT1 nibble before the output is
   enabled, and `disarm()` hands the original value back.
2. **Nothing cleared the PMIC IRQ status after a wake.** `irqClearGpioAll()` ran
   only at arm and disarm, so a latched GPIO4 status holds PYG1_IRQ asserted, and
   a level-triggered wake source that never releases stops light sleep entirely.
   `Platform::clearMotionWake()` now runs after each consumed event, through
   `m5pm1Access` so the retry-once rule still applies.
3. **The MPU6886 WOM status is clear-on-read and everyone reads it.**
   `INT_STATUS` (0x3A) clears on read, and M5Unified's IMU update reads it on
   every sample (`MPU6886_Class.cpp:253`). The spirit level, the IMU live page,
   the console probe and this project's own software backend all call that, so
   using the register as the source of truth means any open sensor page eats the
   motion events. The interrupt is now latched and the pin level is the primary
   signal, with the register read as the acknowledgement. Where no pin is wired
   the register is all there is and the race remains; gate step 3 measures it.
4. **`disarm()` did not restore what it repurposed.** `ACCEL_CONFIG2` and
   `SMPLRT_DIV` were left at the wake-on-motion values, so the spirit level would
   keep reading a 16-sample average at 50 Hz for the rest of the session.
   M5Unified's init values are 0x00 and 3 respectively
   (`MPU6886_Class.cpp` init table); `INT_PIN_CFG` goes back to 0xC0.
5. **`setCallback` was a single slot.** `include/CLAUDE.md` declares
   `MotionSource` the shared API for PR45 and PR65, and a single slot means
   whichever consumer registers last silently unsubscribes the others. It is now
   a small fixed registry with `addCallback` and `removeCallback`, bounded by
   `MAX_CALLBACKS`. Callbacks run on the task that calls `poll()`, which is the
   UI task; add and remove from that task, do not block, do not re-enter.

Also corrected:

- **Cross-task access.** `state()`, `backend()`, `backendName()`,
  `usesInterrupt()` and `interruptCount()` are read by the diagnostics timer and
  the simulator queries while `poll()` may be replacing the backend. Every one
  of them now reads an atomic that `publish()` updates; none dereferences the
  backend pointer.
- **Bus serialisation.** Every engine sequence is a read-modify-write on shared
  registers. All of them now hold `g_IMUMutex`, the same lock the spirit level,
  the IMU live page and the console probe already take. Its declaration moved
  from `FurbleUI.h` to `FurbleIMU.h` so the engines can take it without
  depending on the UI, which is also where PR65 should rebase onto it. Its
  type is `Furble::imu_mutex_t`, which is `std::mutex` everywhere except a
  `FURBLE_SIM` build where it is `Sim::SchedulerMutex`. A simulator task
  blocking on the bus has to stop being runnable or the host-clock deadlock
  breaker times it out instead (issue #279), and PR286 established that
  pattern for `connect_mutex_t`.
- **Wake pin pulls.** Both interrupt sources are open drain active low, so the
  line needs a pull-up to return to idle. GPIO13 on the StickS3 takes the
  internal one and now enables it. GPIO35 on the StickC family is input only on
  the ESP32 with no internal pull of any kind and depends entirely on the board's
  external pull-up on the shared `SYS_INT` net, which the BM8563 RTC also drives:
  an RTC alarm there looks like motion. Both facts are in the code comment and
  both are why gate step 1 counts edges while the device is still.

Two claims in the first version of this plan were wrong and are withdrawn:

- The advanced power save bracket was cited to `bmi2.c bmi2_set_regs`. It is not
  there; `bmi270.c` brackets its own feature writes that way. Worse, on the
  M5StickS3 the bracket is inert: M5Unified writes `PWR_CONF` 0x00 during
  `begin()` (`BMI270_Class.cpp:55`) and never turns power save back on. The
  bracket is kept because nothing guarantees that stays true and a silently
  dropped feature write is indistinguishable from a dead interrupt, but it is
  not what was fixing anything.
- The power model was offered as evidence. It cannot express this feature: the
  modelled current is 81.24 mA with the engine armed and with it off, because the
  model has no term for an IMU or for a wake source. See issue #285. The hardware
  gate is the only measurement of the power claim.

### Hardware gate

Run on the M5StickS3 unless stated. `motion status` on the USB console is the
readout throughout.

1. **No spurious wake traffic.** Arm the BMI270 engine, put the device down, and
   count GPIO13 edges over 10 s while still. Expect **0**. Any edge rate near the
   accelerometer ODR means the data-ready mapping is still on INT1; a slow
   irregular rate on the StickC means the RTC is driving the shared net.
2. **The line releases.** Shake once, then read the GPIO13 level 30 s later. It
   **must** be released. A line still asserted means the PMIC IRQ status was not
   cleared, and light sleep is dead for the rest of the session.
3. **Page-open race.** On the M5StickC Plus, shake with the IMU live page closed
   and confirm the motion event lands. Repeat with the page open. If the second
   case loses events, M5Unified's IMU update is consuming the WOM status and the
   pin latch is not covering it.
4. **Wake and quiescence.** Wake from light sleep on motion; no wake while still.
5. **Current.** Idle draw with the BMI270 engine armed versus the software
   detector. This is the whole justification for the feature, and the power model
   cannot answer it.
6. **Retry once.** Confirm the first I2C access after the PMIC's idle sleep is
   retried rather than reported as a failure.

### MotionSource is the shared API

The #65 review changed the sequencing: this PR lands before #65, and #65's
motion-adaptive GPS consumes `IMU::MotionSource` rather than keeping its own
magnitude-variance detector. One detector, one IMU poller, one definition of
stationary. That makes the interface a contract rather than an internal detail,
so it stays small and stable:

- `arm()`, `disarm()`, `poll()`, `setCallback()`, `state()`, and `MOVING` or
  `STATIONARY`. Nothing else is required to consume it.
- The 60 s quiet window and the slope threshold are the same in all three
  backends, so a consumer's policy behaves identically whichever one armed.
- `setScale()` and `getScale()` are the runtime calibration knob for the
  software backend's 0.20 g threshold, clamped to 0.25 to 4.0 and reachable from
  the console as `motion scale`. Accelerometers differ in noise floor between
  parts and a cased device damps differently from a bare board; the shipped
  0.20 g is a starting point, not a constant. The hardware engines threshold in
  the chip and ignore the scale.
- The source is polled from the existing UI housekeeping timer. It adds no
  `lv_timer` of its own, so nothing new has to be registered with
  `FURBLE_SIM_TIMER_FIRE` and the power model already sees every tick that
  drives it. A consumer must not add one either.
- Nothing in the motion path touches GPS. A motion setting change must never
  route through `GPS::reloadSetting()` or `GPS::enable()`, which the #65 review
  found reset the receiver.

The panel consumer in this PR is the interim one. When #45 lands, re-point the
callback at `IMU_WAKE` shake and drop the panel call if that subsumes it.

## Simulator coverage

The simulator has no I2C bus and no interrupt controller, so it cannot run
either hardware engine. What a scenario needs to exercise is not the registers
but the event semantics, so `sim/FurbleIMUSim.cpp` supplies one virtual backend
per chip that models those and nothing else. It reads the injected
accelerometer through `Sim::imuGetAccel`, the same boundary the software backend
and the spirit level read, and applies the thresholds and durations decoded from
the constants the firmware writes, so the simulator and the device share one
source of truth. `createBMI270Backend()` returns a backend only when the
modelled board carries a BMI270, which is what makes the fallback case testable.

Backend selection became a probe rather than a board table as part of this.
Each hardware backend already identifies its own chip from the bus and refuses
to arm on anything else, so the `M5.Imu.getType()` switch was redundant. Dropping
it costs one register read at boot on the wrong board and makes the simulator
path identical to the firmware path.

### Seams added

| Seam | Where | Why |
|---|---|---|
| `seed hw_motion N` | `sim/driver.cpp` byte seeds, `applyScenarioSettings()`, `settingByteValue()` | The user's Auto, Software or Hardware choice, and `assert setting.hw_motion` for free. |
| `seed imu_chip bmi270\|mpu6886\|none` | `sim/driver.cpp` validated string seeds and `applyScenarioSettings()` | Which engine the modelled board carries. Default `none`, so a scenario that does not ask for a chip exercises the software fallback. |
| `ui.motion_backend`, `ui.motion_state`, `ui.motion_wake`, `ui.motion_interrupts` | `src/FurbleUI.cpp` sim query block | Read from `IMU::MotionSource`, not from the diagnostics labels, so the selection is assertable even when the IMU live page was never opened. |
| `ui.display` | `src/FurbleUI.cpp` sim query block | Panel sleep state. Motion wake had no other observable. |
| `Platform::armMotionWake()` / `disarmMotionWake()` | `sim/FurblePlatformSim.cpp` | The board answer, modelled: the wake path exists on the Stick boards and not on the Core. |

No new action. Motion is driven by the existing `imu.accel` plus virtual time.
An engine that needed a motion-specific action would not be modelling the
sensor.

### Scenarios

All nine are in `sim/scenarios/e2e/`, certified, board `m5stick-s3`. They test
logic, not layout; the layout coverage is the board matrix below. Nothing goes
in `sim/scenarios/` top level, because the power gate iterates that directory
and demands a committed baseline per file.

Every killing mutation below was applied, built and run. Four of them are
recorded because the first draft of the scenario did not have the teeth it
claimed, and the mutation run is what proved it.

| Ask | File | Killing mutation | Result |
|---|---|---|---|
| (a) selection | `hw-motion-select-software.txt` | `arm()` ignores `HW_MOTION_SOFTWARE` and always probes | kills |
| (a) | `hw-motion-select-hardware.txt` | the MPU6886 factory ignores the seeded chip, and the probe order is swapped | kills |
| (a) | `hw-motion-select-auto.txt` | the `HW_MOTION` default in `Settings::init()` becomes `HW_MOTION_SOFTWARE` | kills |
| (b) consumer | `hw-motion-display-wake.txt` | delete `lv_display_trigger_activity()` from the `MOVING` handler | kills |
| (b) parity | `hw-motion-parity.txt`, `hw-motion-parity-software.txt` | the virtual engine's no-motion window becomes 30 s | kills |
| (c) wake source | `hw-motion-light-sleep.txt` | `Platform::armMotionWake()` returns false | kills |
| (c) setting off | `hw-motion-no-wake-when-off.txt` | remove both the `imuEnabledForUI()` gate and the backend's own sensor check | kills |
| (d) fallback | `hw-motion-fallback.txt` | `arm()` returns false instead of falling back | kills |
| (e) settings row | `bughunt/page-matrix.txt`, `bughunt/stick-notouch-layout-135.txt`, `bughunt/core-notouch-layout.txt`, `bughunt/text-size-overflow-small.txt` extended, plus the new `bughunt/hw-motion-text-size.txt` | put the roller back on the Sensors page with `addRollerItem` | kills |
| (f) console | `tests/host/console_commands_test.cpp` | change the accepted range in `FurbleConsole::setValue` | kills |
| (f) calibration | `tests/host/console_commands_test.cpp` | widen or drop the 0.25 to 4.0 clamp in `MotionSource::setScale` | kills |

### The roller does not fit on the Sensors page

The draft put the roller inline on Sensors. It does not fit: 45 px past the
135x240 panel, and the button layout renders that overflow under the floating
navigation indicators. `page-matrix`, `stick-notouch-layout-135` and
`overflow-sweep` all caught it, which is what those files are for.

It now lives on its own page, reached from Sensors, the same shape the GPS
rollers use. That needed four things, each of which is a table a new page has to
be added to and none of which fails at compile time:

- `m_Menu`, the grid map `addMenu()` looks the name up in. A missing entry
  throws `std::out_of_range` at startup.
- The page identity array behind `ui.page`, whose length is a template argument
  and was the only one of the four the compiler caught.
- The two `nav` and `page` name maps in `src/FurbleUI.cpp`.
- The four page-name whitelists in `sim/scenario_action.cpp`, plus the two
  vocabularies in `docs/sim.md` that `check-doc-tokens.sh` greps.

Two smaller layout findings came out of the same work:

- The Sensors restart notice was a label wrapped in its own menu container. That
  container's padding was the last 3 px that pushed the page off 135x240 at
  Large text once the Motion Engine row joined it. It is now a plain label on
  the page, which is what the Features page already does for the same kind of
  static hint.
- At Large text on the 80x160 panel the Sensors page has four rows and legitimately
  scrolls. `hw-motion-text-size.txt` asserts both scroll ends are reachable there
  rather than a fit, and asserts the fit on the roller page, which is the
  assertion that keeps the roller off Sensors.

That scenario is deliberately narrow. An earlier attempt added the Sensors leg
to the shared `text-size-overflow-large.txt`, which meant seeding the IMU on;
that adds a Level row to the Connected page, which at Large text pushes that
page off the 80x160 panel. Regressing an unrelated page to cover this one is not
a trade worth making, so the shared file is untouched and the coverage lives in
a file this feature owns.

### Four assertions that had no teeth

The first mutation build passed every scenario, which was the most useful result
of the whole exercise:

- `wakeDisplay()` could be deleted from the motion callback and the panel still
  woke, because `lv_display_trigger_activity()` resets the idle clock and
  `processInactivity()` wakes the panel on the next tick by itself. The call was
  redundant, not the assertion. Removed from the code, and the mutation moved to
  the call that actually matters.
- `Platform::armMotionWake()` could return false with nothing failing, because
  the scenario asserted the interrupt counter, which the backend increments
  whether or not a wake source armed. Fixed by adding `ui.motion_wake` and
  asserting that instead.
- Removing the `imuEnabledForUI()` gate changed nothing, because every backend
  also refuses to arm on a sensor that reports itself disabled. That is a real
  second guard, so the mutation is now correctly stated as removing both.
- Shortening the virtual no-motion window from 60 s to 30 s changed nothing,
  because the parity scenario asserted `moving` at 30 s and the quiet timer
  starts about a second late. The margin moved to 45 s.

### What the simulator cannot cover

The register programming, which is why
`tests/host/imu_motion_encoding_test.cpp` exists. It builds both engine
translation units against a recording bus and a stub platform, and asserts the
write sequence and the decoded feature fields against the datasheet and Bosch
API values cited in its header: 81 checks. Flipping `NO_MOTION_THRESHOLD` back
to the draft's wrong 0x690 fails two of them.

Two further things neither layer can reach, and only hardware can settle:

- Whether a BMI270 INT1 edge on M5PM1 GPIO4 asserts PYG1_IRQ on ESP32-S3 GPIO13
  while the system is running, rather than only waking the M5PM1 from its own
  sleep. If it does not, `armMotionWake()` still returns true and nothing wakes.
- Whether the non-latched interrupt pulse is long enough for the GPIO wake latch
  to catch it. Both engines are configured non-latched on purpose, so the pin
  releases without a bus transaction and light sleep stays re-entrant, but that
  trades a held level for a short pulse.

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

One key, shipped. The plan originally argued for none, on the grounds that
`IMU_WAKE` and `GPS_MOTION` already express user intent and the detection
mechanism is a platform detail. That argument still holds for the user, but it
assumed the hardware path would be proven. It is not: the M5StickS3 interrupt
chain is unverified, and a bug report that says "the battery got worse" has to
be narrowable to a backend without a rebuild.

| Enum | Wire id | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|---|
| `HW_MOTION` | 74 | `hw_motion` | `FURBLE_STR` | `uint8_t` | 0 | 0 Auto, 1 Software, 2 Hardware. |

Name string: `"Motion Engine"`. Auto preserves current behaviour on any board
where the hardware path is not proven, because a chip backend that fails to arm
falls back to software. Not immediate: the engine is chosen when the source is
armed, which is at boot, so the Sensors page carries the existing Restart
button. Not dangerous.

Wire id 74 is the id issue #280 reserved for this setting. This PR claims no
other id, and in particular it leaves `IMU` at 46.

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

- PR #45 (`feat/17-imu-gestures`). Not a code dependency. Its `GestureDetector`
  is a tap, shake and double tap classifier over accelerometer samples; this
  PR's `IMU::MotionSource` is a moving and stationary source with hardware
  backends. No shared file, no shared type, no call in either direction. What
  they share is textual conflict on `FurbleSettings`, the Sensors menu, the
  source lists and `plans/README.md`, and both branches originally added the
  same golden `*-45.bin` files. Dropping the `IMU` renumber from this PR removes
  the last of those. The base is `master` and this PR is not stacked. It lands
  last of the backlog, so one more rebase is expected; at that rebase, reuse
  #45's `tests/host/gesture_stubs/` rather than keeping `tests/host/imu_stubs/`
  as a second copy.
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
