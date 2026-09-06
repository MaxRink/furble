#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#include <M5Unified.h>

#include "FurbleUIGesture.h"

using Furble::GestureDetector;

namespace {

void expectNoGesture(GestureDetector &detector,
                     float x,
                     float y,
                     float z,
                     uint32_t now,
                     bool doubleTap = false) {
  GestureDetector::gesture_t gesture = GestureDetector::gesture_t::SHAKE;
  assert(!detector.sample(x, y, z, now, doubleTap, gesture));
}

void expectGesture(GestureDetector &detector,
                   GestureDetector::gesture_t expected,
                   float x,
                   float y,
                   float z,
                   uint32_t now,
                   bool doubleTap) {
  GestureDetector::gesture_t gesture = GestureDetector::gesture_t::TAP;
  assert(detector.sample(x, y, z, now, doubleTap, gesture));
  assert(gesture == expected);
}

void prime(GestureDetector &detector, uint32_t now = 0) {
  expectNoGesture(detector, 0.0f, 0.0f, 1.0f, now);
}

void tap(GestureDetector &detector, uint32_t start, bool doubleTap) {
  expectNoGesture(detector, 0.0f, 0.0f, 2.6f, start, doubleTap);
  expectNoGesture(detector, 0.0f, 0.0f, 1.0f, start + 20, doubleTap);
}

}  // namespace

int main() {
  // A normal stationary orientation never emits a gesture, including at the
  // exact quiet threshold.
  {
    GestureDetector detector;
    prime(detector);
    for (uint32_t now = 20; now < 1000; now += 20) {
      expectNoGesture(detector, 0.0f, 0.0f, 1.0f, now);
    }
  }

  // A tap is immediate when double-tap mode is disabled.
  {
    GestureDetector detector;
    prime(detector);
    expectNoGesture(detector, 0.0f, 0.0f, 2.6f, 20);
    expectGesture(detector, GestureDetector::gesture_t::TAP, 0.0f, 0.0f, 1.0f, 40, false);
    expectNoGesture(detector, 0.0f, 0.0f, 1.0f, 60);
  }

  // Exercise the inclusive tap/release boundaries and exclusive shake
  // boundary. The baseline is 1 g, so these values are exact threshold cases.
  {
    GestureDetector detector;
    prime(detector);
    expectNoGesture(detector, 0.0f, 0.0f, 2.5f, 20);  // deviation == 1.5
    expectGesture(detector, GestureDetector::gesture_t::TAP, 0.0f, 0.0f, 1.5f, 40, false);

    detector.reset();
    prime(detector);
    expectNoGesture(detector, 0.0f, 0.0f, 1.6f, 20);  // deviation == 0.6
    expectNoGesture(detector, 0.0f, 0.0f, 1.6f, 40);
    expectNoGesture(detector, 0.0f, 0.0f, 1.6f, 60);
  }

  // Two released impulses 80..400 ms apart produce exactly one double tap.
  {
    GestureDetector detector;
    prime(detector);
    tap(detector, 20, true);
    expectNoGesture(detector, 0.0f, 0.0f, 2.6f, 140, true);
    expectGesture(detector, GestureDetector::gesture_t::DOUBLE_TAP, 0.0f, 0.0f, 1.0f, 160, true);
    expectNoGesture(detector, 0.0f, 0.0f, 1.0f, 180, true);
  }

  // A lone tap in double-tap mode is resolved as a wake tap after the window.
  {
    GestureDetector detector;
    prime(detector);
    tap(detector, 20, true);
    expectNoGesture(detector, 0.0f, 0.0f, 1.0f, 440, true);
    expectGesture(detector, GestureDetector::gesture_t::TAP, 0.0f, 0.0f, 1.0f, 441, true);
  }

  // Three sustained high-deviation samples report one shake and hold off
  // duplicate reports until the signal returns to quiet.
  {
    GestureDetector detector;
    prime(detector);
    expectNoGesture(detector, 0.0f, 0.0f, 1.8f, 20);
    expectNoGesture(detector, 0.0f, 0.0f, 1.8f, 40);
    expectGesture(detector, GestureDetector::gesture_t::SHAKE, 0.0f, 0.0f, 1.8f, 60, false);
    for (uint32_t now = 80; now < 700; now += 20) {
      expectNoGesture(detector, 0.0f, 0.0f, 1.8f, now);
    }
    expectNoGesture(detector, 0.0f, 0.0f, 1.0f, 720);
  }

  // Wrap-safe elapsed time applies to the double-tap window and refractory
  // period, matching the virtual-clock contract used by the rest of furble.
  {
    GestureDetector detector;
    const uint32_t start = std::numeric_limits<uint32_t>::max() - 40U;
    prime(detector, start);
    tap(detector, start + 20U, true);
    expectNoGesture(detector, 0.0f, 0.0f, 2.6f, start + 140U, true);
    expectGesture(detector, GestureDetector::gesture_t::DOUBLE_TAP, 0.0f, 0.0f, 1.0f, start + 160U,
                  true);
  }

  // NaN, infinity, and an implausible magnitude are ignored without poisoning
  // the detector or creating a shutter event.
  {
    GestureDetector detector;
    prime(detector);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    expectNoGesture(detector, nan, 0.0f, 1.0f, 20);
    expectNoGesture(detector, inf, 0.0f, 1.0f, 40);
    expectNoGesture(detector, 100.0f, 0.0f, 0.0f, 60);
    expectNoGesture(detector, 0.0f, 0.0f, 1.0f, 80);
  }

  // Reset clears pending double-tap state and starts calibration afresh.
  {
    GestureDetector detector;
    prime(detector);
    tap(detector, 20, true);
    detector.reset();
    expectNoGesture(detector, 0.0f, 0.0f, 2.6f, 140, true);
    expectNoGesture(detector, 0.0f, 0.0f, 1.0f, 160, true);
  }

  // Walking with the device in a pocket: a 2 Hz 0.3 g sway for five seconds.
  // This is the false-positive case the shake threshold exists for.
  {
    GestureDetector detector;
    prime(detector);
    for (uint32_t now = 20; now < 5000; now += 20) {
      const float sway = 0.3f * std::sin(2.0f * 3.14159265f * 2.0f * (now / 1000.0f));
      expectNoGesture(detector, 0.0f, 0.0f, 1.0f + sway, now);
    }
  }

  // Sensor calibration drift: the resting magnitude ramps from 1.0 to 1.1 g
  // over 200 samples. The baseline must follow it, so the tap threshold stays
  // an effective 1.5 g above rest rather than eroding to 1.4 g.
  {
    GestureDetector detector;
    prime(detector);
    uint32_t now = 20;
    for (uint32_t step = 0; step < 200; ++step, now += 20) {
      expectNoGesture(detector, 0.0f, 0.0f, 1.0f + (0.1f * step / 199.0f), now);
    }
    // 1.4 g above the drifted rest is still under the bar.
    expectNoGesture(detector, 0.0f, 0.0f, 2.5f, now);
    now += 20;
    expectNoGesture(detector, 0.0f, 0.0f, 1.1f, now);
    now += 20;
    // 1.5 g above the drifted rest clears it.
    expectNoGesture(detector, 0.0f, 0.0f, 2.6f, now);
    now += 20;
    expectGesture(detector, GestureDetector::gesture_t::TAP, 0.0f, 0.0f, 1.1f, now, false);
  }

  // The console calibration scale raises and lowers the amplitude thresholds.
  // A tap that fires at 1.0 must not fire at 2.0, and must fire again at 0.5.
  {
    assert(GestureDetector::getScale() == 1.0f);
    GestureDetector::setScale(2.0f);
    assert(GestureDetector::getScale() == 2.0f);
    GestureDetector detector;
    prime(detector);
    expectNoGesture(detector, 0.0f, 0.0f, 2.6f, 20);
    expectNoGesture(detector, 0.0f, 0.0f, 1.0f, 40);

    GestureDetector::setScale(0.5f);
    detector.reset();
    prime(detector, 100);
    expectNoGesture(detector, 0.0f, 0.0f, 1.9f, 120);
    expectGesture(detector, GestureDetector::gesture_t::TAP, 0.0f, 0.0f, 1.0f, 140, false);

    // Out-of-range and non-finite scales are ignored or clamped, never applied
    // raw: a zero scale would fire the shutter on sensor noise.
    GestureDetector::setScale(0.0f);
    assert(GestureDetector::getScale() == 0.25f);
    GestureDetector::setScale(std::numeric_limits<float>::quiet_NaN());
    assert(GestureDetector::getScale() == 0.25f);
    GestureDetector::setScale(100.0f);
    assert(GestureDetector::getScale() == 4.0f);
    GestureDetector::setScale(1.0f);
  }

  // poll() is the hardware path: it must refuse to read a disabled sensor and
  // must classify a real sample through the same state machine.
  {
    GestureDetector detector;
    GestureDetector::gesture_t gesture = GestureDetector::gesture_t::SHAKE;
    M5.Imu.enabled = false;
    assert(!detector.poll(false, gesture));
    M5.Imu.enabled = true;
    M5.Imu.z = 1.0f;
    assert(!detector.poll(false, gesture));
    M5.Imu.z = 2.6f;
    assert(!detector.poll(false, gesture));
    M5.Imu.z = 1.0f;
    assert(detector.poll(false, gesture));
    assert(gesture == GestureDetector::gesture_t::TAP);
  }

  return 0;
}
