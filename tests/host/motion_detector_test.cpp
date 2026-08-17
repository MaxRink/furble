// Pure stationary detector from include/FurbleMotion.h. The GPS motion timer
// feeds this on hardware, so the thresholds, the hysteresis and the window
// length are pinned here rather than on the device. No hardware dependency, so
// it needs no furble sources on its link line.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "FurbleMotion.h"

using Furble::Motion::Detector;

namespace {

int g_failures = 0;

void check(bool condition, const std::string &name) {
  if (!condition) {
    std::printf("FAIL %s\n", name.c_str());
    g_failures++;
    return;
  }
  std::printf("ok %s\n", name.c_str());
}

constexpr uint64_t SAMPLE_MS = Detector::SAMPLE_MS;

// Feed steady 1 g samples for the given duration and return the final state.
bool feedStill(Detector &detector, uint64_t &now_ms, uint64_t duration_ms) {
  bool stationary = detector.isStationary();
  for (uint64_t elapsed = 0; elapsed < duration_ms; elapsed += SAMPLE_MS) {
    now_ms += SAMPLE_MS;
    stationary = detector.sample(1.0f, now_ms);
  }
  return stationary;
}

// The dwell only starts once the window is full, so entry costs the window plus
// the dwell. Feed one extra sample so the comparison is >= rather than >.
uint64_t entryDurationMs(void) {
  return (Detector::WINDOW_SAMPLES * SAMPLE_MS) + Detector::STATIONARY_MS + SAMPLE_MS;
}

void testEntryNeedsTheFullDwell(void) {
  Detector detector;
  uint64_t now_ms = 0;

  check(!detector.isStationary(), "a fresh detector reports moving");

  // One sample short of the window plus dwell must still be moving.
  const bool early = feedStill(detector, now_ms, entryDurationMs() - (2 * SAMPLE_MS));
  check(!early, "still samples one period short of the dwell stay moving");

  now_ms += SAMPLE_MS;
  const bool late = detector.sample(1.0f, now_ms);
  check(late, "the sample that completes the dwell enters stationary");
  check(detector.isStationary(), "isStationary agrees with the sample result");
}

void testPartialWindowNeverEntersStationary(void) {
  Detector detector;
  uint64_t now_ms = 0;

  // Advance the clock far past the dwell while feeding fewer samples than the
  // window holds. A partial window says nothing about variance, so a device
  // that has only just powered up must not claim to be stationary.
  for (size_t index = 0; index < Detector::WINDOW_SAMPLES - 1; index++) {
    now_ms += Detector::STATIONARY_MS;
    detector.sample(1.0f, now_ms);
  }
  check(!detector.isStationary(), "a partial window never enters stationary");
}

void testSingleSpikeExitsImmediately(void) {
  Detector detector;
  uint64_t now_ms = 0;

  feedStill(detector, now_ms, entryDurationMs());
  check(detector.isStationary(), "the detector is stationary before the spike");

  now_ms += SAMPLE_MS;
  // 0.2 g off the mean is a squared deviation of 0.04, twice the threshold.
  const bool moved = detector.sample(1.2f, now_ms);
  check(!moved, "one sample past the threshold exits stationary");
  check(!detector.isStationary(), "the exit is visible to isStationary");
}

void testSpikeUnderTheThresholdHoldsStationary(void) {
  Detector detector;
  uint64_t now_ms = 0;

  feedStill(detector, now_ms, entryDurationMs());
  check(detector.isStationary(), "the detector is stationary before the nudge");

  now_ms += SAMPLE_MS;
  // 0.1 g off the mean is a squared deviation of 0.01, half the threshold. A
  // tripod nudge below the threshold must not cost the stationary state.
  const bool nudged = detector.sample(1.1f, now_ms);
  check(nudged, "a sample under the threshold keeps stationary");
}

void testReentryPaysTheFullDwellAgain(void) {
  Detector detector;
  uint64_t now_ms = 0;

  feedStill(detector, now_ms, entryDurationMs());
  now_ms += SAMPLE_MS;
  detector.sample(1.2f, now_ms);
  check(!detector.isStationary(), "moving after the spike");

  // Half the dwell is not enough: entry is deliberately slow.
  const bool half = feedStill(detector, now_ms, entryDurationMs() / 2);
  check(!half, "half the dwell does not re-enter stationary");

  const bool full = feedStill(detector, now_ms, entryDurationMs());
  check(full, "the full dwell re-enters stationary");
}

void testSustainedVibrationNeverSettles(void) {
  Detector detector;
  uint64_t now_ms = 0;

  // Alternate 1.0 g and 1.4 g. The mean settles at 1.2 g, so every sample is
  // 0.2 g out, a squared deviation of 0.04 against a 0.02 threshold. A rig
  // shaking that hard never claims to be stationary.
  for (uint64_t elapsed = 0; elapsed < entryDurationMs() * 2; elapsed += SAMPLE_MS) {
    now_ms += SAMPLE_MS;
    detector.sample((elapsed / SAMPLE_MS) % 2 == 0 ? 1.0f : 1.4f, now_ms);
  }
  check(!detector.isStationary(), "sustained vibration never enters stationary");
}

void testVibrationUnderTheThresholdReadsStationary(void) {
  // The documented ceiling of a variance threshold. A steady 0.2 g peak to peak
  // shake settles to a mean of 1.1 g with a squared deviation of 0.01, half the
  // threshold, so the detector calls it stationary. That is the deliberate cost
  // of a threshold that ignores tripod nudges, and it is pinned here so that
  // moving the threshold is a deliberate change with a hardware measurement
  // behind it rather than a silent behaviour swap. See plans/18-gps-motion.md.
  Detector detector;
  uint64_t now_ms = 0;

  for (uint64_t elapsed = 0; elapsed < entryDurationMs() * 2; elapsed += SAMPLE_MS) {
    now_ms += SAMPLE_MS;
    detector.sample((elapsed / SAMPLE_MS) % 2 == 0 ? 1.0f : 1.2f, now_ms);
  }
  check(detector.isStationary(), "vibration under the threshold reads as stationary");
}

void testResetDropsTheWindowAndTheState(void) {
  Detector detector;
  uint64_t now_ms = 0;

  feedStill(detector, now_ms, entryDurationMs());
  check(detector.isStationary(), "stationary before the reset");

  detector.reset();
  check(!detector.isStationary(), "reset returns the detector to moving");

  // The window is empty again, so the dwell restarts from a full window.
  const bool tooSoon = feedStill(detector, now_ms, Detector::STATIONARY_MS);
  check(!tooSoon, "the dwell restarts from an empty window after a reset");
}

void testNonFiniteSampleIsDiscarded(void) {
  Detector detector;
  uint64_t now_ms = 0;

  feedStill(detector, now_ms, entryDurationMs());
  check(detector.isStationary(), "stationary before the bad read");

  now_ms += SAMPLE_MS;
  const bool nan_state = detector.sample(std::numeric_limits<float>::quiet_NaN(), now_ms);
  check(nan_state, "a NaN sample is discarded rather than forcing a transition");

  now_ms += SAMPLE_MS;
  const bool inf_state = detector.sample(std::numeric_limits<float>::infinity(), now_ms);
  check(inf_state, "an infinite sample is discarded too");
}

void testMagnitude(void) {
  check(std::fabs(Detector::magnitude(0.0f, 0.0f, 1.0f) - 1.0f) < 1e-6f, "a flat device reads 1 g");
  check(std::fabs(Detector::magnitude(3.0f, 4.0f, 0.0f) - 5.0f) < 1e-6f,
        "the magnitude is the vector length");
}

void testZeroTimestampStartsTheDwell(void) {
  // now_ms of exactly zero must start the dwell like any other timestamp. A
  // sentinel that treats zero as unset would enter stationary one sample early.
  Detector detector;

  for (size_t index = 0; index < Detector::WINDOW_SAMPLES; index++) {
    detector.sample(1.0f, 0);
  }
  check(!detector.isStationary(), "a window fed entirely at t=0 is not stationary");

  check(!detector.sample(1.0f, Detector::STATIONARY_MS - 1),
        "one millisecond short of the dwell from t=0 stays moving");
  check(detector.sample(1.0f, Detector::STATIONARY_MS),
        "the dwell measured from t=0 completes on time");
}

}  // namespace

int main(void) {
  testEntryNeedsTheFullDwell();
  testPartialWindowNeverEntersStationary();
  testSingleSpikeExitsImmediately();
  testSpikeUnderTheThresholdHoldsStationary();
  testReentryPaysTheFullDwellAgain();
  testSustainedVibrationNeverSettles();
  testVibrationUnderTheThresholdReadsStationary();
  testResetDropsTheWindowAndTheState();
  testNonFiniteSampleIsDiscarded();
  testMagnitude();
  testZeroTimestampStartsTheDwell();

  if (g_failures > 0) {
    std::printf("%d motion detector checks failed\n", g_failures);
    return EXIT_FAILURE;
  }
  std::printf("motion detector checks passed\n");
  return EXIT_SUCCESS;
}
