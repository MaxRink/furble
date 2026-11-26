// Scale coverage for the IMU motion engines and for the simulator double.
//
// Every threshold in the motion path is a register count, and the document that
// defines what a count means is named in include/FurbleIMUMotionScale.h. This
// test pins the mg and ms each configuration produces, so a wrong count, a
// wrong count size or a wrong field width fails here instead of shipping as a
// wake that needs a shove or that fires on passing traffic.
//
// It also drives sim/imu_motion_model.h, which is the code the simulator's
// virtual engines run, with the configuration FurbleIMUSim.cpp builds. The
// double is a certification surface for the hardware gate, so its thresholds
// and its quiet window are pinned here rather than trusted. A double that
// reports stationary for vibration the chip ignores is a false pass on the only
// signal the motion consumer reads.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include "FurbleIMUMotionScale.h"
#include "imu_motion_model.h"

namespace {

int g_Failures = 0;
int g_Checks = 0;

bool check(bool condition, const std::string &message) {
  g_Checks++;
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

bool checkNear(float actual, float expected, float tolerance, const std::string &message) {
  const bool ok = std::fabs(actual - expected) <= tolerance;
  if (!ok) {
    std::cerr << "  (expected " << expected << ", got " << actual << ")\n";
  }
  return check(ok, message);
}

bool checkEqual(unsigned long actual, unsigned long expected, const std::string &message) {
  const bool ok = actual == expected;
  if (!ok) {
    std::cerr << "  (expected " << expected << ", got " << actual << ")\n";
  }
  return check(ok, message);
}

using Furble::IMU::Scale::BMI270_ANY_MOTION_THRESHOLD_COUNTS;
using Furble::IMU::Scale::BMI270_ANY_MOTION_THRESHOLD_MG;
using Furble::IMU::Scale::BMI270_NO_MOTION_DURATION_COUNTS;
using Furble::IMU::Scale::BMI270_NO_MOTION_THRESHOLD_COUNTS;
using Furble::IMU::Scale::BMI270_NO_MOTION_THRESHOLD_MG;
using Furble::IMU::Scale::bmi270DurationMs;
using Furble::IMU::Scale::bmi270ThresholdG;
using Furble::IMU::Scale::bmi270ThresholdMg;
using Furble::IMU::Scale::MPU6886_WOM_MG_PER_COUNT_ASSUMED;
using Furble::IMU::Scale::MPU6886_WOM_THRESHOLD_COUNTS;
using Furble::IMU::Scale::MPU6886_WOM_THRESHOLD_MG_ASSUMED;
using Furble::IMU::Scale::mpu6886WomThresholdMgAssumed;
using Furble::IMU::Scale::STATIONARY_HOLD_MS;

// Tolerance for a float conversion of an exact product. A tenth of a mg is far
// below the resolution of either part.
constexpr float MG_TOLERANCE = 0.1f;

/** Samples with no slope at all, one per second, for @p seconds. */
void feedQuiet(Furble::Sim::ImuMotionModel &model, uint32_t &now, int seconds) {
  const float accel[3] = {0.0f, 0.0f, 1.0f};
  for (int second = 0; second < seconds; second++) {
    now += 1000;
    model.step(now, accel);
  }
}

/**
 * Continuous vibration at @p stepG per axis, one sample per second.
 *
 * The engine thresholds the change between consecutive samples, so a constant
 * value is a zero slope. Alternating between 0 and @p stepG produces exactly
 * @p stepG on every sample.
 */
void feedVibration(Furble::Sim::ImuMotionModel &model, uint32_t &now, float stepG, int seconds) {
  for (int second = 0; second < seconds; second++) {
    const float accel[3] = {0.0f, 0.0f, 1.0f + (((second % 2) == 0) ? stepG : 0.0f)};
    now += 1000;
    model.step(now, accel);
  }
}

// Test 1. The counts the backends write, converted to the mg the documents
// define.
void testConfiguredScales(void) {
  std::cerr << "test: the configured counts produce the documented thresholds\n";

  // BMI270 any-motion. Section 5.2.40 pages 92 and 93: bits 10:0 over 0 to 1 g,
  // default 0xAA, stated as 83 mg.
  checkEqual(BMI270_ANY_MOTION_THRESHOLD_COUNTS, 0xAA,
             "the BMI270 any-motion threshold count is the documented default");
  checkNear(bmi270ThresholdMg(BMI270_ANY_MOTION_THRESHOLD_COUNTS), 83.0f, MG_TOLERANCE,
            "0xAA counts is 83.0 mg at 1 g over 2048 counts");
  checkNear(BMI270_ANY_MOTION_THRESHOLD_MG, bmi270ThresholdMg(0xAA), 0.0001f,
            "the published any-motion mg matches its own conversion");

  // BMI270 no-motion. Same field layout and scale, documented default 0x090.
  checkEqual(BMI270_NO_MOTION_THRESHOLD_COUNTS, 0x090,
             "the BMI270 no-motion threshold count is the documented default");
  checkNear(bmi270ThresholdMg(BMI270_NO_MOTION_THRESHOLD_COUNTS), 70.3f, MG_TOLERANCE,
            "0x090 counts is 70.3 mg");
  checkNear(BMI270_NO_MOTION_THRESHOLD_MG, bmi270ThresholdMg(0x090), 0.0001f,
            "the published no-motion mg matches its own conversion");

  // The two engines are not the same threshold. A model or a doc that treats
  // them as one is wrong, so pin the gap.
  check(BMI270_ANY_MOTION_THRESHOLD_MG > BMI270_NO_MOTION_THRESHOLD_MG,
        "any-motion is less sensitive than no-motion, as the chip is configured");

  // The field spans 0 to 1 g. A code that quoted 4 mg per count would put the
  // top of this field at 8 g, which is not what the document says.
  checkNear(bmi270ThresholdMg(2047), 999.5f, MG_TOLERANCE,
            "the threshold field tops out one count below 1 g");
  checkNear(bmi270ThresholdMg(2048), 1000.0f, 0.001f, "2048 threshold counts is exactly 1 g");

  // Durations: 50 Hz samples, one count is 20 ms. Section 5.2.40 states the
  // default as 5 counts, 100 ms.
  checkEqual(bmi270DurationMs(1), 20, "one any-motion duration count is 20 ms");
  checkEqual(bmi270DurationMs(5), 100, "the documented duration default is 100 ms");
  checkEqual(bmi270DurationMs(BMI270_NO_MOTION_DURATION_COUNTS), STATIONARY_HOLD_MS,
             "the no-motion duration is the shared 60 s window");
  checkEqual(bmi270DurationMs(0x1FFF), 163820, "the 13 bit duration field tops out at 163.82 s");

  // MPU6886 wake on motion. The count size is an assumption, so pin it as one
  // and pin the span that makes it plausible: one byte at 4 mg is about 1 g.
  checkEqual(MPU6886_WOM_THRESHOLD_COUNTS, 0x10, "the MPU6886 wake threshold count is 0x10");
  checkNear(MPU6886_WOM_MG_PER_COUNT_ASSUMED, 4.0f, 0.0001f,
            "the assumed MPU6886 count size is 4 mg, not a datasheet figure");
  checkNear(mpu6886WomThresholdMgAssumed(MPU6886_WOM_THRESHOLD_COUNTS),
            MPU6886_WOM_THRESHOLD_MG_ASSUMED, 0.0001f,
            "the published MPU6886 mg matches its own conversion");
  checkNear(MPU6886_WOM_THRESHOLD_MG_ASSUMED, 64.0f, MG_TOLERANCE, "16 assumed counts is 64 mg");
  checkNear(mpu6886WomThresholdMgAssumed(255), 1020.0f, MG_TOLERANCE,
            "a one byte field at 4 mg spans about 1 g, which is why the assumption is plausible");
}

// Test 2. The simulator double's configuration, which must be the firmware's
// own conversion and not a hand copied g value.
void testDoubleConfig(void) {
  std::cerr << "test: the simulator double models the documented resolution\n";

  const auto bmi270 = Furble::Sim::bmi270MotionConfig();
  checkNear(bmi270.anyMotionThresholdG * 1000.0f, 83.0f, MG_TOLERANCE,
            "the virtual BMI270 any-motion threshold is the documented 83.0 mg");
  checkNear(bmi270.noMotionThresholdG * 1000.0f, 70.3f, MG_TOLERANCE,
            "the virtual BMI270 no-motion threshold is its own 70.3 mg, not the any-motion one");
  checkNear(bmi270.anyMotionThresholdG, bmi270ThresholdG(BMI270_ANY_MOTION_THRESHOLD_COUNTS), 0.0f,
            "the double converts the same counts the backend writes");
  checkNear(bmi270.noMotionThresholdG, bmi270ThresholdG(BMI270_NO_MOTION_THRESHOLD_COUNTS), 0.0f,
            "the double converts the no-motion counts too");
  checkEqual(bmi270.stationaryHoldMs, STATIONARY_HOLD_MS, "the virtual quiet window is 60 s");
  checkEqual(bmi270.motionSamples, 1, "any-motion fires on one sample, one 20 ms count");

  const auto mpu6886 = Furble::Sim::mpu6886MotionConfig();
  checkNear(mpu6886.anyMotionThresholdG * 1000.0f, MPU6886_WOM_THRESHOLD_MG_ASSUMED, MG_TOLERANCE,
            "the virtual MPU6886 wake threshold is the assumed 64 mg");
  // That part has no no-motion engine and no separate quiet threshold, so one
  // threshold is correct here and only here.
  checkNear(mpu6886.noMotionThresholdG, mpu6886.anyMotionThresholdG, 0.0f,
            "the MPU6886 double uses one threshold because the part has one engine");
  checkEqual(mpu6886.stationaryHoldMs, STATIONARY_HOLD_MS, "the MPU6886 hold is the same 60 s");
}

// Test 3. Any-motion edges, driven through the model the simulator runs.
void testModelAnyMotion(void) {
  std::cerr << "test: the double leaves stationary at the configured any-motion threshold\n";

  const auto config = Furble::Sim::bmi270MotionConfig();
  Furble::Sim::ImuMotionModel model;
  model.configure(config);
  model.arm();

  uint32_t now = 1000;
  feedQuiet(model, now, 62);
  check(!model.moving(), "60 s of no slope at all reports stationary");

  float accel[3] = {0.0f, 0.0f, 1.0f + (config.anyMotionThresholdG - 0.00025f)};
  now += 1000;
  check(!model.step(now, accel), "half a count below the threshold does not report motion");
  check(!model.moving(), "the state stays stationary below the threshold");

  // Exactly at the threshold, from a fresh model so the sample before it is
  // exactly 1 g and the difference is exactly the configured threshold. The
  // chip compares against the configured count, so the boundary is inclusive.
  Furble::Sim::ImuMotionModel boundary;
  boundary.configure(config);
  boundary.arm();
  uint32_t boundaryNow = 1000;
  feedQuiet(boundary, boundaryNow, 62);
  float exact[3] = {0.0f, 0.0f, 1.0f + config.anyMotionThresholdG};
  boundaryNow += 1000;
  check(boundary.step(boundaryNow, exact), "a slope at the threshold reports motion");
  check(boundary.moving(), "the state is moving at the threshold");
}

// Test 4. No-motion edges. The chip has a second, tighter threshold, and a
// slope between the two restarts its counter without reporting anything.
void testModelNoMotion(void) {
  std::cerr << "test: the double needs its own no-motion threshold to report stationary\n";

  const auto config = Furble::Sim::bmi270MotionConfig();

  // Vibration above the no-motion threshold and below any-motion is not quiet,
  // no matter how long it lasts.
  Furble::Sim::ImuMotionModel jitter;
  jitter.configure(config);
  jitter.arm();
  uint32_t now = 1000;
  feedVibration(jitter, now, 0.075f, 130);
  check(jitter.moving(),
        "75 mg of slope for 130 s stays moving: the chip needs below 70.3 mg to count quiet");

  // Below the no-motion threshold the window runs and stationary arrives.
  Furble::Sim::ImuMotionModel quiet;
  quiet.configure(config);
  quiet.arm();
  now = 1000;
  feedVibration(quiet, now, 0.060f, 62);
  check(!quiet.moving(), "60 mg of slope for 62 s reports stationary");

  // One sample above the no-motion threshold restarts the window, so
  // stationary cannot arrive on the old count.
  Furble::Sim::ImuMotionModel restart;
  restart.configure(config);
  restart.arm();
  now = 1000;
  feedQuiet(restart, now, 59);
  check(restart.moving(), "59 s of quiet is not 60 s");

  float accel[3] = {0.0f, 0.0f, 1.0f + 0.075f};
  now += 1000;
  restart.step(now, accel);
  feedQuiet(restart, now, 59);
  check(restart.moving(), "a slope above the no-motion threshold restarts the 60 s window");

  feedQuiet(restart, now, 3);
  check(!restart.moving(), "three more quiet samples complete a fresh 60 s window");
}

// Test 5. The MPU6886 engine, which has one threshold and a software hold.
void testModelMpu6886(void) {
  std::cerr << "test: the double's MPU6886 engine uses its assumed wake threshold\n";

  const auto config = Furble::Sim::mpu6886MotionConfig();
  Furble::Sim::ImuMotionModel model;
  model.configure(config);
  model.arm();

  uint32_t now = 1000;
  feedVibration(model, now, 0.060f, 62);
  check(!model.moving(), "60 s below the 64 mg wake threshold reports stationary");

  // One milli-g above the assumed 64 mg threshold. The exact boundary is
  // pinned on the BMI270 engine above; this only has to leave stationary.
  float accel[3] = {0.0f, 0.0f, 1.0f + config.anyMotionThresholdG + 0.001f};
  now += 1000;
  check(model.step(now, accel), "a step above the wake threshold reports motion");
}

}  // namespace

int main(void) {
  testConfiguredScales();
  testDoubleConfig();
  testModelAnyMotion();
  testModelNoMotion();
  testModelMpu6886();

  std::cerr << g_Checks << " checks, " << g_Failures << " failures\n";
  return (g_Failures == 0) ? 0 : 1;
}
