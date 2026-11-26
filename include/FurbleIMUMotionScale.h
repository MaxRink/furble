#ifndef FURBLE_IMU_MOTION_SCALE_H
#define FURBLE_IMU_MOTION_SCALE_H

#include <cstdint>

// Counts, scales and derived thresholds for the hardware motion engines.
//
// Every hardware threshold in this project is a register count, and the chip's
// document defines what a count means. Those definitions lived in comments, in
// a simulator constant and in a test string, copied by hand. A copy that is
// wrong cannot fail a test, so the code would wake at a sensitivity nobody
// chose. This header is the one definition. The BMI270 backend, the MPU6886
// backend, the simulator double and the host tests all read it, and the tests
// pin the mg and ms each configuration produces.
//
// BMI270, Bosch BST-BMI270-DS000-08 revision 1.6, March 2026, 150 pages,
// section 5.2.40 FEATURES, pages 92 and 93:
//
//   ANYMO_1 at feature page 0x3C holds the duration in bits 12:0 in 50 Hz
//   samples, so one count is 20 ms. ANYMO_2 at feature page 0x3E holds the
//   threshold in bits 10:0 over 0 to 1 g. The documented defaults are 5 counts
//   (100 ms) and 0xAA, which the document states as 83 mg. 170 counts over a
//   0 to 1 g span is 1000 mg / 2048 = 0.4883 mg per count, and 170 of them is
//   83.0 mg, so the default and the resolution agree.
//
//   The no-motion feature uses the same two field layouts and the same scales.
//   Its documented default threshold is 0x090, which is 70.3 mg at this
//   resolution and which the Bosch no_motion_interrupt example states as
//   70 mg.
//
// MPU-6886, TDK DS-000193 revision 1.1, 59 pages, sections 8.20 to 8.22 and
// table 15:
//
//   ACCEL_WOM_X_THR, ACCEL_WOM_Y_THR and ACCEL_WOM_Z_THR at 0x20, 0x21 and
//   0x22 hold the wake-on-motion threshold. The document states no per-count
//   figure for them, so the count size below is this project's assumption, not
//   a datasheet value. See MPU6886_WOM_MG_PER_COUNT_ASSUMED.

namespace Furble {
namespace IMU {
namespace Scale {

/**
 * The quiet window every backend shares.
 *
 * The BMI270 no-motion duration, the MPU6886 software hold and the software
 * backend's window are all this value, so all three report stationary at the
 * same time on the same device. The static_asserts below keep them equal.
 */
constexpr uint32_t STATIONARY_HOLD_MS = 60 * 1000;

// The BMI270 duration and threshold fields, from the register detail above.
constexpr uint16_t BMI270_DURATION_FIELD_MASK = 0x1FFF;
constexpr uint16_t BMI270_THRESHOLD_FIELD_MASK = 0x07FF;
constexpr uint32_t BMI270_DURATION_MS_PER_COUNT = 20;
constexpr uint16_t BMI270_THRESHOLD_MG_FULL_SCALE = 1000;

// One threshold count. This is 1000/2048 exactly, not the 0.48 mg the earlier
// comments quoted, which was 1.7 percent low.
constexpr float BMI270_THRESHOLD_MG_PER_COUNT =
    static_cast<float>(BMI270_THRESHOLD_MG_FULL_SCALE) / (BMI270_THRESHOLD_FIELD_MASK + 1);

/**
 * Any-motion: the shortest duration the chip offers, so exit from stationary is
 * immediate, at the documented default threshold.
 *
 * The threshold is the chip's own default, so the sensitivity is the one Bosch
 * documents rather than a project choice: 83.0 mg.
 */
constexpr uint16_t BMI270_ANY_MOTION_DURATION_COUNTS = 1;
constexpr uint16_t BMI270_ANY_MOTION_THRESHOLD_COUNTS = 0xAA;

/**
 * No-motion: the shared 60 s window at the documented default threshold.
 *
 * 3000 counts of 20 ms is exactly STATIONARY_HOLD_MS, so the state the chip
 * reports matches the software backend's window on the same device.
 */
constexpr uint16_t BMI270_NO_MOTION_DURATION_COUNTS = 3000;
constexpr uint16_t BMI270_NO_MOTION_THRESHOLD_COUNTS = 0x090;

/** A BMI270 threshold field value in mg. */
constexpr float bmi270ThresholdMg(uint16_t counts) {
  return static_cast<float>(counts) * BMI270_THRESHOLD_MG_PER_COUNT;
}

/** A BMI270 duration field value in ms. */
constexpr uint32_t bmi270DurationMs(uint16_t counts) {
  return static_cast<uint32_t>(counts) * BMI270_DURATION_MS_PER_COUNT;
}

/** A BMI270 threshold field value in g, for the simulator's slope model. */
constexpr float bmi270ThresholdG(uint16_t counts) {
  return bmi270ThresholdMg(counts) / 1000.0f;
}

constexpr float BMI270_ANY_MOTION_THRESHOLD_MG =
    bmi270ThresholdMg(BMI270_ANY_MOTION_THRESHOLD_COUNTS);
constexpr float BMI270_NO_MOTION_THRESHOLD_MG =
    bmi270ThresholdMg(BMI270_NO_MOTION_THRESHOLD_COUNTS);
constexpr uint32_t BMI270_NO_MOTION_DURATION_MS =
    bmi270DurationMs(BMI270_NO_MOTION_DURATION_COUNTS);

/**
 * The MPU6886 wake-on-motion threshold count.
 *
 * 0x10 was chosen to sit near the BMI270 any-motion default, and that is still
 * the intent. The value is unchanged by the scale audit: the count size below
 * is unverified, and moving the count would move wake sensitivity on the
 * strength of a figure no document states.
 */
constexpr uint8_t MPU6886_WOM_THRESHOLD_COUNTS = 0x10;

/**
 * ASSUMPTION, not a datasheet value.
 *
 * DS-000193 revision 1.1 sections 8.20 to 8.22 describe the three threshold
 * registers as the wake-on-motion threshold and give no per-count figure, and
 * table 15 repeats the addresses without one. 4 mg per count is the working
 * figure this project has carried since the backend was written, and it has no
 * document behind it in this tree. It is at least self-consistent: the driver
 * writes one byte per register, and at 4 mg that field spans 0 to 1020 mg, a
 * sensible range for a threshold that should notice a lift and ignore table
 * vibration. It is not verified, and no source in this tree states it.
 *
 * A different count size scales the wake threshold with no other symptom, so a
 * device that will not wake on a gentle lift, or one that wakes on passing
 * traffic, is the observable. Only a measured tilt test on a StickC, a Plus or
 * a Core2 can settle it; see plans/332-imu-motion-scale-audit.md.
 */
constexpr float MPU6886_WOM_MG_PER_COUNT_ASSUMED = 4.0f;

/**
 * The MPU6886 stationary hold.
 *
 * The part has no no-motion engine, so the backend decides stationary itself
 * from the last wake-on-motion event. The hold matches the shared window, and
 * therefore the BMI270 no-motion duration, exactly.
 */
constexpr uint32_t MPU6886_STATIONARY_HOLD_MS = STATIONARY_HOLD_MS;

/** An MPU6886 threshold count in mg, under MPU6886_WOM_MG_PER_COUNT_ASSUMED. */
constexpr float mpu6886WomThresholdMgAssumed(uint8_t counts) {
  return static_cast<float>(counts) * MPU6886_WOM_MG_PER_COUNT_ASSUMED;
}

/** The same in g, for the simulator's slope model. */
constexpr float mpu6886WomThresholdGAssumed(uint8_t counts) {
  return mpu6886WomThresholdMgAssumed(counts) / 1000.0f;
}

constexpr float MPU6886_WOM_THRESHOLD_MG_ASSUMED =
    mpu6886WomThresholdMgAssumed(MPU6886_WOM_THRESHOLD_COUNTS);

static_assert((BMI270_THRESHOLD_FIELD_MASK + 1) == 2048,
              "the BMI270 threshold field spans 0 to 1 g over 2048 counts");
static_assert(BMI270_ANY_MOTION_THRESHOLD_COUNTS <= BMI270_THRESHOLD_FIELD_MASK,
              "the any-motion threshold must fit bits 10:0");
static_assert(BMI270_NO_MOTION_THRESHOLD_COUNTS <= BMI270_THRESHOLD_FIELD_MASK,
              "the no-motion threshold must fit bits 10:0");
static_assert(BMI270_ANY_MOTION_DURATION_COUNTS <= BMI270_DURATION_FIELD_MASK,
              "the any-motion duration must fit bits 12:0");
static_assert(BMI270_NO_MOTION_DURATION_COUNTS <= BMI270_DURATION_FIELD_MASK,
              "the no-motion duration must fit bits 12:0");
static_assert(BMI270_NO_MOTION_DURATION_MS == STATIONARY_HOLD_MS,
              "the BMI270 no-motion duration is the shared stationary window");
static_assert(MPU6886_STATIONARY_HOLD_MS == STATIONARY_HOLD_MS,
              "the MPU6886 hold is the shared stationary window");

}  // namespace Scale
}  // namespace IMU
}  // namespace Furble

#endif
