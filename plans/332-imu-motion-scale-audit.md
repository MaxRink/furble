# 332 - IMU motion wake scale audit

Status: implemented on `vibe/imu-motion-audit`. The firmware register values do
not change, so no device wakes differently after this PR. Every corrected
statement below was checked against the document named in the finding, and the
scale constants the code uses now live in one header that the backends, the
simulator double and a host test all read.

Related: [implemented/20-imu-hw-motion.md](implemented/20-imu-hw-motion.md) (the
feature this audits), [implemented/18-gps-motion.md](implemented/18-gps-motion.md)
(the one motion consumer), `docs/power/gps-imu.md` section 3 and
`docs/power/datasheet-register.md` section 4.

## Why

Three motion engines threshold in the sensor, and every threshold is a register
count, not a physical unit. The conversion from counts to mg lived in comments,
in a simulator constant and in a test string, copied by hand in four places. A
copy that is wrong cannot fail a test: the host test asserts the counts, the
simulator asserts against a g value derived from the same comment, and the
device wakes at whatever the chip does with those counts. That is the exact
shape of a silent sensitivity defect, and it is worth one audit while the
datasheets are open.

## Documents

Opened, revision-checked and hashed by a research pass on 2026-11-25. The
sha256 prefixes below are that pass's: the agent that wrote this plan did not
re-fetch or re-hash any of the three PDFs, so the page and section numbers below
are the pass's too. Every one of them was cross-checked against the register
behaviour in the code before it was used.

| Part | Document | Revision | Bytes | sha256 (prefix) |
| --- | --- | --- | --- | --- |
| Bosch BMI270 | BST-BMI270-DS000-08, `https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf` | 1.6, March 2026 | 150 pages | `0dab4065` |
| QST QMI8658A | 13-52-25, `https://www.qstcorp.com/upload/pdf/202512/13-52-25%20QMI8658A%20Datasheet%20Rev%20D.pdf` | Rev D | 78 pages | `edda798c` |
| TDK MPU-6886 | DS-000193, `https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/MPU-6886-000193%2Bv1.1_GHIC_en.pdf` | 1.1 | 59 pages | `6751256f` |

## Findings

| # | Area | Verdict | Document | Code |
| --- | --- | --- | --- | --- |
| 1 | BMI270 any-motion resolution | nothing in the register value, doc error in the comment | BMI270 1.6 section 5.2.40, pages 92 and 93 | `src/FurbleIMUMotionBMI270.cpp:96-103` |
| 2 | BMI270 duration | nothing | BMI270 1.6 section 5.2.40, pages 92 and 93 | `src/FurbleIMUMotionBMI270.cpp:96-108` |
| 3 | MPU-6886 threshold scale | doc error, assumption presented as datasheet fact; vendor document defect recorded | MPU-6886 1.1 sections 8.20 to 8.22, table 15, section 5.1 step 4 | `src/FurbleIMUMotionMPU6886.cpp:41-43` |
| 4 | QMI8658A motion co-processor | nothing (no backend exists in this tree) | QMI8658A Rev D sections 9.4 to 9.6 | absent |
| 5 | Simulator and boundary doubles | bug, in the double only | BMI270 1.6 page 92 for the resolution, MPU-6886 1.1 sections 8.20 to 8.22 for the missing figure | `sim/FurbleIMUSim.cpp:31-41`, `sim/FurbleIMUSim.cpp:110-127` |

### 1. BMI270 any-motion resolution: the code writes the documented default

BMI270 revision 1.6 section 5.2.40 (FEATURES, pages 92 and 93) documents what
earlier revisions did not: ANYMO_1 at feature page 0x3C holds the duration in
bits 12:0, and ANYMO_2 at feature page 0x3E holds the threshold in bits 10:0
over 0 to 1 g, default 0xAA, which the document states as 83 mg. 170 counts over
a 0 to 1 g span is 1 g / 2048 = 0.4883 mg per count, and 170 x 0.4883 = 83.0 mg,
so the default and the resolution agree.

There is no counts conversion anywhere in the driver, and none is needed. The
code writes `ANY_MOTION_THRESHOLD = 0xAA`, so the sensitivity a user gets is
83.0 mg, exactly the value the comment claims. A 4 mg-per-count model would have
produced 21 counts for the same 83 mg and a 10 mg threshold, which is eight
times more sensitive rather than eight times less, so the shape of the hinted
defect requires a conversion the code does not have. The value is correct.

What is wrong is the surrounding prose. `src/FurbleIMUMotionBMI270.cpp:20` and
`tests/host/imu_motion_encoding_test.cpp:18-22` state that the any-motion and
no-motion feature layout "is not in the datasheet" and is "only described by
that API". Revision 1.6 section 5.2.40 documents both features at feature pages
0x3C to 0x3F, so that claim is now false, and the datasheet is the primary
source the code should cite first. The same comment and
`docs/power/datasheet-register.md:52` quote 0.48 mg per count, which is 1.7
percent below 1 g / 2048. Both are corrected.

### 2. BMI270 duration: one count is 20 ms, and the code is exact

ANYMO_1 bits 12:0 count 50 Hz samples, 20 ms each, default 5 (100 ms). The code
writes 1 (20 ms) for any-motion and 3000 for no-motion, and 3000 x 20 ms is
60000 ms, which is exactly the `STATIONARY_HOLD_MS` the software backend and the
MPU-6886 hold use. Nothing to fix. The conversion is now a constant with a
`static_assert` tying it to the shared 60 s hold, so the three engines cannot
drift apart silently.

### 3. MPU-6886: the 4 mg per count is our assumption, not a datasheet figure

DS-000193 revision 1.1 sections 8.20 to 8.22 describe `ACCEL_WOM_X_THR`,
`ACCEL_WOM_Y_THR` and `ACCEL_WOM_Z_THR` at registers 0x20, 0x21 and 0x22 as the
wake-on-motion threshold and give no per-count figure. Table 15 lists the same
addresses. So "one threshold count is 4 mg" at
`src/FurbleIMUMotionMPU6886.cpp:41` is an assumption, and 0x10 is 64 mg only if
that assumption holds.

The assumption has no document behind it in this tree, not even a secondary one:
it is the figure the backend was written with. It is at least self-consistent,
because the driver writes one byte per threshold register and at 4 mg that field
spans 0 to 1020 mg, a sensible design point for a threshold that should cover
normal handling and ignore table vibration. It is not a measurement, and a wrong
count size scales the wake threshold proportionally with no other symptom. A device that will not wake on a gentle lift, or one that
wakes on a passing lorry, is the observable, and only a drop and lift test on a
StickC, a Plus or a Core2 can settle it. The value stays 0x10: changing it would
change wake sensitivity on the strength of an unverified figure.

Vendor document defect, recorded rather than worked around: section 5.1 step 4
of the same document says to write `ACCEL_WOM_THR` at 0x1F, while table 15 and
sections 8.20 to 8.22 put the three threshold registers at 0x20 to 0x22, and
table 15 has no 0x1F entry at all. Our driver writes 0x20, 0x21 and 0x22, which
matches the register map and the M5StickC-Plus vendor driver, so the code is
right and the document's procedure step is wrong. Anyone following section 5.1
literally configures a register that is not the threshold.

The driver comment also cited "revision 1.2" of the MPU-6886 datasheet. No such
revision exists in this tree's register; revision 1.1 is the current copy, which
`docs/power/datasheet-register.md:260` already records. Corrected.

### 4. QMI8658A: there is no backend to audit

The QMI8658A has no driver in `lib/hal`, no `IMU::MotionBackend` and no entry in
`Furble::IMU::Backend`. `lib/hal` holds `AXP2101`, `BQ27220`, `SX1262` and
`XL9555` only. M5Unified 0.2.20 `IMU_Class.hpp:13-20` knows `imu_sh200q`,
`imu_mpu6886` and `imu_bmi270` and nothing else, so no board can reach a
QMI8658 through this stack, and no board in `boards/` or `platformio.ini` fits
one. The part appears only as an alternative-hardware candidate:
`plans/41-alternative-hardware.md:147` and `plans/88-alternate-hardware.md:84`
name it on the LilyGO T-Beam Supreme, and
`plans/implemented/322-hardware-abstraction.md:105,238` records it as unsupported.

So the premise in the request, a QMI8658 backend that polls the accelerometer
instead of using the co-processor, does not exist. Nothing polls it, and no
significant-motion or any/no-motion defect can be hiding in code that is not
there.

What the co-processor would buy, if a board with one is ever supported: the
three engines run without CPU wakeups, and the significant-motion engine has no
equivalent in any current backend, which covers the case this project actually
wants, a device that was put down and then picked up. Configuration is a CTRL9
command write of code 0x0E, enable bits 1, 2 and 3 in CTRL8 with bit 6 routing
to the interrupt pin, and status in STATUS1 bits 7:5 (QMI8658A Rev D sections 9.4
to 9.6). It fits `MotionBackend` well: `poll()` becomes one STATUS1 read,
`arm()` and `disarm()` become CTRL8/CTRL9 writes, and `usesInterrupt()` maps to
the CTRL8 bit 6 routing, which is the same shape as the BMI270 backend.

It is not implemented here. It is not contained: a CTRL9 command needs a
completion wait on the command ack, the part needs address auto-increment
handling that the current register helpers do not do, no board in the tree
carries the part, and there is no hardware to verify one byte of it. A backend
with no device and no register test that can be checked against a physical part
is dead code that would still have to pass the same gate as the BMI270 path.

### 5. The simulator double: bug, and it was the only place a scale was modelled

`sim/FurbleIMUSim.cpp:31-41` turned the firmware counts into g by hand:
0.0816 g for the BMI270 (0xAA at 0.48 mg) and 0.064 g for the MPU-6886 (0x10 at
4 mg). Two defects followed.

First, 0.0816 g is 81.6 mg, not the 83.0 mg the chip produces from 0xAA. The
double was 1.7 percent more sensitive than the hardware it certifies, so a
scenario could pass at a slope the device ignores.

Second, and worse, one threshold served both engines. The BMI270 no-motion
engine has its own threshold, 0x090, 70.3 mg, and it counts quiet time only
while the filtered slope stays below that. The double treated everything below
the any-motion 83 mg threshold as quiet. A device on a vibrating surface at
75 mg: the hardware never reaches stationary, the double reported stationary
after 60 s. That is a false pass on the one signal the motion consumer cares
about, and no test could see it because the double had no test of its own.

The fix puts every scale in `include/FurbleIMUMotionScale.h`, has the double
derive both thresholds from the same constants the backends write, models the
no-motion threshold separately, and moves the decision logic into
`sim/imu_motion_model.h` so a host test can drive it. The MPU-6886 double keeps
a single threshold on purpose: that part has no no-motion engine and its 60 s
hold starts from the last wake-on-motion event, so its quiet threshold is its
wake threshold.

## Sensitivity impact

No firmware register value changes. `0xAA`, `0x090`, 1 and 3000 on the BMI270
and `0x10`, three times, on the MPU-6886 are the same bytes after this PR as
before it, so a device wakes at exactly the same acceleration. No sensitivity
default needs retuning, and the `HW_MOTION` default stays Software.

What changes is the simulator: the BMI270 virtual engine is 1.7 percent less
sensitive at 83.0 mg instead of 81.6 mg, and it now needs a slope below 70.3 mg
to accumulate quiet time instead of below 83.0 mg. Both directions make the
simulator stricter than it was, so no existing scenario result is invalidated:
every committed scenario steps the injected accelerometer by at least 1 g, and
the parity and light-sleep legs are unaffected. The next simulator run is the
evidence.

The one place a user-facing sensitivity could be wrong is the MPU-6886 64 mg
figure, and that is a hardware question, not a code one. See "Needs hardware".

## Changes

- `include/FurbleIMUMotionScale.h` is new and is the single definition of the
  documented resolution, the configured counts and the derived mg, ms and g
  values for both parts. It cites the datasheet section for each number and
  marks the MPU-6886 count size as an assumption.
- `src/FurbleIMUMotionBMI270.cpp` takes its counts, field masks and duration
  from that header, and its comment now cites the datasheet first and the Bosch
  API as corroboration.
- `src/FurbleIMUMotionMPU6886.cpp` takes its count and the assumed count size
  from that header, states the assumption as one, records the section 5.1
  register defect and cites revision 1.1.
- `src/FurbleIMU.cpp` takes the software backend's quiet window from the same
  header, so the `static_assert` on the BMI270 duration covers all three
  backends rather than two.
- `sim/imu_motion_model.h` is new: the virtual engines' slope, duration and
  quiet-window decision, plus the per-chip configuration, with no simulator or
  ESP dependency, so a host test can drive the same code the scenarios drive.
- `sim/FurbleIMUSim.cpp` delegates to that model and takes both thresholds from
  the per-chip configuration, so it converts no count of its own.
- `tests/host/imu_motion_scale_test.cpp` is new: it pins the mg and ms each
  configuration produces, the full-scale span of each field, the assumption
  boundary on the MPU-6886, and the four edges of the double's model.
- `tests/host/imu_motion_encoding_test.cpp` gains a check that decodes the mg
  from the bytes the driver actually wrote, and its header comment now cites the
  datasheet revision and section rather than claiming the layout is undocumented.

Docs updated: `docs/power/gps-imu.md` section 3.1 (the counts, the documented
scales and the assumption), `docs/power/datasheet-register.md` section 4 (both
part rows and a new QMI8658A row), `sim/CLAUDE.md` (the modeled row),
`tests/host/CLAUDE.md` (test ownership), `include/CLAUDE.md` (the one-definition
rule), `src/CLAUDE.md` (the same rule for the backends that read it),
`plans/README.md` (this plan) and
`plans/implemented/20-imu-hw-motion.md` (a follow-up section correcting the
layout and scale statements that plan made).

## Tests

`tests/host/imu_motion_scale_test.cpp`, registered as `imu-motion-scale` in
`tests/host/CMakeLists.txt`. Red before the fix, green after it. The model
header was introduced with the values the double shipped, so the failing run
below is the shipped behaviour with the logic moved and nothing else changed.

```
$ cmake --build /tmp/furble-host-imu --target imu_motion_scale_test && /tmp/furble-host-imu/imu_motion_scale_test
test: the simulator double models the documented resolution
  (expected 83, got 81.6)
  FAIL: the virtual BMI270 any-motion threshold is the documented 83.0 mg
  (expected 70.3, got 81.6)
  FAIL: the virtual BMI270 no-motion threshold is its own 70.3 mg, not the any-motion one
  (expected 0.0830078, got 0.0816)
  FAIL: the double converts the same counts the backend writes
  (expected 0.0703125, got 0.0816)
  FAIL: the double converts the no-motion counts too
test: the double leaves stationary at the configured any-motion threshold
  FAIL: a slope at the threshold reports motion
  FAIL: the state is moving at the threshold
test: the double needs its own no-motion threshold to report stationary
  FAIL: 75 mg of slope for 130 s stays moving: the chip needs below 70.3 mg to count quiet
  FAIL: a slope above the no-motion threshold restarts the 60 s window
test: the double's MPU6886 engine uses its assumed wake threshold
39 checks, 8 failures
```

After the scale fix, the same binary reports `39 checks, 0 failures`. The two
"at the threshold" checks also failed for the old value because 0.0816 is not
exactly representable, so the drawn slope landed a rounding step below it; with
the documented 85/1024 all three terms are exact, and the check now pins the
inclusive boundary rather than a rounded one.

`imu-motion-encoding` grew from 103 to 114 checks and still passes.
`console-commands`, which links `src/FurbleIMU.cpp` and therefore the new
header, still passes.

## Needs hardware

- MPU-6886 wake-on-motion count size. Build a firmware that writes a known
  count, tilt the device about one axis in measured steps, and find the angle at
  which the interrupt asserts. That gives mg per count and closes the only
  unverified scale in the motion path. This is the item that could show a real
  sensitivity error on the StickC, Plus and Core2.
- MPU-6886 threshold registers versus the section 5.1 procedure. Confirm on a
  part that writing 0x20 to 0x1F changes nothing while 0x20 to 0x22 do change
  the trigger point, and report it upstream so the vendor document can be fixed.
- BMI270 any-motion at 83 mg on the StickS3 as a sanity check that the
  configured default behaves as the document says. No code change depends on it.
- Nothing for the QMI8658A until a board with one is supported.
