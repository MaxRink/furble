// Host conformance for the production feedback driver (src/FurbleFeedback.cpp).
//
// The production event-to-pattern mapping, volume handling, speaker session
// lifetime, LED timing and low-battery sampler run unchanged. Only the M5
// speaker/LED/vibration hardware and the LVGL timer are doubles.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "boundary_host.h"
#include "driver/gpio.h"
#include "FurbleFeedback.h"
#include "FurbleSettings.h"
#include "M5Unified.h"
#include "nvs.h"

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FEEDBACK FAIL: " << message << '\n';
    g_Failures++;
  }
}

void initialize(uint8_t output, uint8_t events, uint8_t volume) {
  nvs_test_reset();
  Furble::Settings::init();
  Furble::Settings::save<uint8_t>(Furble::Settings::FB_OUTPUT, output);
  Furble::Settings::save<uint8_t>(Furble::Settings::FB_EVENTS, events);
  Furble::Settings::save<uint8_t>(Furble::Settings::FB_VOLUME, volume);
}

void completeTone(void) {
  M5.Speaker.playing = false;
}

void runSound(void) {
  using Furble::Feedback;

  initialize(Feedback::OUTPUT_SOUND, 0x0f, 77);
  M5.board = m5::board_t::board_M5StackCore2;
  M5.Speaker = m5::Speaker_Class {};
  M5.Power = m5::Power_Class {};

  Feedback::init();
  auto &feedback = Feedback::getInstance();
  check(feedback.supports(Feedback::OUTPUT_SOUND), "Core2 sound support is missing");
  check(!feedback.supports(Feedback::OUTPUT_LIGHT), "Core2 reported an LED it does not have");
  check(feedback.supports(Feedback::OUTPUT_VIBRATE), "Core2 vibration support is missing");
  check(!feedback.supports(Feedback::OUTPUT_SOUND_LIGHT), "Core2 accepted sound+light");

  feedback.signal(Feedback::SHUTTER_FIRED);
  check(M5.Speaker.active, "the speaker was not opened");
  check(M5.Speaker.volume == 77, "the production volume did not reach the speaker");
  check(M5.Speaker.lastFrequency == 4000 && M5.Speaker.lastDuration == 40,
        "the shutter tone is wrong");
  std::cout << "feedback.sound event=shutter frequency=4000 duration=40 volume=77\n";

  completeTone();
  BoundaryHost::advanceTick(50);
  BoundaryHost::serviceTimers();
  check(!M5.Speaker.active, "the speaker session stayed open after the final tone");
  std::cout << "feedback.sound end=1\n";

  feedback.signal(Feedback::CONNECTED);
  check(M5.Speaker.lastFrequency == 2000 && M5.Speaker.lastDuration == 60,
        "the connected first tone is wrong");
  completeTone();
  BoundaryHost::advanceTick(70);
  BoundaryHost::serviceTimers();
  check(M5.Speaker.lastFrequency == 2000, "the tone advanced during its gap");
  BoundaryHost::advanceTick(21);
  BoundaryHost::serviceTimers();
  check(M5.Speaker.lastFrequency == 3000 && M5.Speaker.lastDuration == 60,
        "the connected second tone is wrong");

  feedback.setVolume(42);
  check(M5.Speaker.volume == 42, "the live volume preview did not reach an open speaker");
  completeTone();
  BoundaryHost::advanceTick(70);
  BoundaryHost::serviceTimers();
  check(!M5.Speaker.active, "the connected pattern did not end the speaker session");
  std::cout << "feedback.sound event=connected tones=2 volume_live=42\n";

  feedback.signal(Feedback::COUNTDOWN);
  check(M5.Speaker.lastFrequency == 2000 && M5.Speaker.lastDuration == 40,
        "the countdown tone is wrong");
  completeTone();
  BoundaryHost::advanceTick(50);
  BoundaryHost::serviceTimers();

  feedback.signal(Feedback::DISCONNECTED);
  check(M5.Speaker.lastFrequency == 3000, "the disconnected first tone is wrong");
  for (int step = 0; step < 6 && M5.Speaker.active; step++) {
    completeTone();
    BoundaryHost::advanceTick(70);
    BoundaryHost::serviceTimers();
  }
  check(!M5.Speaker.active, "the disconnected pattern did not end the speaker session");
  check(M5.Speaker.lastFrequency == 2000, "the disconnected second tone is wrong");
  std::cout << "feedback.sound event=disconnected tones=2\n";

  feedback.signal(Feedback::COUNTDOWN);
  completeTone();
  BoundaryHost::advanceTick(50);
  BoundaryHost::serviceTimers();
  const uint8_t events = Furble::Settings::load<uint8_t>(Furble::Settings::FB_EVENTS);
  Furble::Settings::save<uint8_t>(Furble::Settings::FB_EVENTS, 0);
  feedback.reload();
  const size_t before = BoundaryHost::timerFireCount();
  feedback.signal(Feedback::SHUTTER_FIRED);
  check(BoundaryHost::timerFireCount() == before, "an event outside the mask started a pattern");
  feedback.signal(Feedback::SHUTTER_FIRED, true);
  check(M5.Speaker.lastFrequency == 4000, "a forced event bypassing the mask was ignored");
  Furble::Settings::save<uint8_t>(Furble::Settings::FB_EVENTS, events);
  feedback.reload();
  std::cout << "feedback.sound mask=filtered force=applied\n";
}

void runLight(void) {
  using Furble::Feedback;

  initialize(Feedback::OUTPUT_LIGHT, 0x0f, 50);
  M5.board = m5::board_t::board_M5StickCPlus;
  M5.Speaker = m5::Speaker_Class {};
  M5.Power = m5::Power_Class {};

  Feedback::init();
  auto &feedback = Feedback::getInstance();
  check(feedback.supports(Feedback::OUTPUT_LIGHT), "StickC Plus light support is missing");
  check(BoundaryHost::gpioDirection(10) == GPIO_MODE_OUTPUT, "the LED pin was not configured");
  check(BoundaryHost::gpioLevel(10) == 1, "the active-low LED was not left off at boot");

  feedback.signal(Feedback::SHUTTER_FIRED);
  check(BoundaryHost::gpioLevel(10) == 0, "the shutter did not drive the active-low LED on");
  BoundaryHost::advanceTick(50);
  BoundaryHost::serviceTimers();
  check(BoundaryHost::gpioLevel(10) == 1, "the shutter LED did not turn off");
  std::cout << "feedback.light event=shutter on=1 off=1\n";

  feedback.signal(Feedback::CONNECTED);
  check(BoundaryHost::gpioLevel(10) == 0, "the connected first blink did not start");
  BoundaryHost::advanceTick(70);
  BoundaryHost::serviceTimers();
  check(BoundaryHost::gpioLevel(10) == 1, "the connected first blink did not stop");
  BoundaryHost::advanceTick(90);
  BoundaryHost::serviceTimers();
  check(BoundaryHost::gpioLevel(10) == 0, "the connected second blink did not start");
  std::cout << "feedback.light event=connected blinks=2\n";

  feedback.setDisplayOff(true);
  check(BoundaryHost::gpioLevel(10) == 1, "display off left the LED lit");
  BoundaryHost::advanceTick(100);
  BoundaryHost::serviceTimers();
  feedback.signal(Feedback::SHUTTER_FIRED);
  check(BoundaryHost::gpioLevel(10) == 1, "display off still drove the LED");
  feedback.setDisplayOff(false);
  feedback.signal(Feedback::SHUTTER_FIRED);
  check(BoundaryHost::gpioLevel(10) == 0, "display on did not restore LED feedback");
  std::cout << "feedback.light display_off=suppressed display_on=restored\n";
}

void runOvercurrent(void) {
  using Furble::Feedback;

  initialize(Feedback::OUTPUT_SOUND, 0x0f, 60);
  M5.board = m5::board_t::board_M5StackCore2;
  M5.Speaker = m5::Speaker_Class {};
  M5.Power = m5::Power_Class {};
  M5.Speaker.beginFailures = 2;

  Feedback::init();
  auto &feedback = Feedback::getInstance();
  feedback.signal(Feedback::SHUTTER_FIRED);
  check(!M5.Speaker.active, "the speaker stayed open after two begin failures");
  check(M5.Speaker.lastFrequency == 0, "a tone was queued after the amplifier failed");
  check(BoundaryHost::logContains("Speaker begin failed, dropping tone pattern"),
        "the production speaker failure log is missing");
  std::cout << "feedback.overcurrent begin_failures=2 dropped=1\n";

  M5.Speaker.beginFailures = 0;
  feedback.signal(Feedback::SHUTTER_FIRED);
  check(M5.Speaker.active && M5.Speaker.lastFrequency == 4000,
        "the speaker did not recover after the overcurrent condition cleared");
  completeTone();
  BoundaryHost::advanceTick(50);
  BoundaryHost::serviceTimers();
  std::cout << "feedback.overcurrent recovered=1\n";
}

void runVibration(void) {
  using Furble::Feedback;

  initialize(Feedback::OUTPUT_VIBRATE, 0x0f, 50);
  M5.board = m5::board_t::board_M5StackCore2;
  M5.Speaker = m5::Speaker_Class {};
  M5.Power = m5::Power_Class {};

  Feedback::init();
  auto &feedback = Feedback::getInstance();
  check(feedback.supports(Feedback::OUTPUT_VIBRATE), "Core2 vibration support is missing");
  feedback.signal(Feedback::SHUTTER_FIRED);
  check(M5.Power.vibration == 255, "the shutter vibration did not start");
  BoundaryHost::advanceTick(70);
  BoundaryHost::serviceTimers();
  check(M5.Power.vibration == 0, "the shutter vibration did not stop");
  std::cout << "feedback.vibration event=shutter pulses=1\n";

  feedback.signal(Feedback::CONNECTED);
  check(M5.Power.vibration == 255, "the connected vibration did not start");
  BoundaryHost::advanceTick(110);
  BoundaryHost::serviceTimers();
  check(M5.Power.vibration == 0, "the connected vibration did not stop");
  std::cout << "feedback.vibration event=connected pulses=1\n";
}

void runLowBattery(void) {
  using Furble::Feedback;

  initialize(Feedback::OUTPUT_VIBRATE, Feedback::EVENT_LOW_BATTERY_MASK, 55);
  M5.board = m5::board_t::board_M5StackCore2;
  M5.Speaker = m5::Speaker_Class {};
  M5.Power = m5::Power_Class {};
  M5.Power.batteryVoltage = 3000;

  Feedback::init();
  auto &feedback = Feedback::getInstance();
  for (int sample = 0; sample < 5; sample++) {
    feedback.updateBattery(5, false);
  }
  check(M5.Power.vibration == 0, "the low-battery warning fired before six samples");
  feedback.updateBattery(5, false);
  check(M5.Power.vibration == 255, "the low-battery vibration did not start");
  std::cout << "feedback.brownout low_battery_samples=6 vibration=255 voltage_mv=3000\n";

  feedback.updateBattery(80, false);
  feedback.updateBattery(5, false);
  std::cout << "feedback.brownout hysteresis=sampled\n";
}

}  // namespace

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "sound";

  if (mode == "sound") {
    runSound();
  } else if (mode == "light") {
    runLight();
  } else if (mode == "overcurrent") {
    runOvercurrent();
  } else if (mode == "low-battery") {
    runLowBattery();
  } else if (mode == "vibration") {
    runVibration();
  } else {
    std::cerr << "unknown feedback boundary mode: " << mode << '\n';
    return 2;
  }

  furbleHostStopTasks();
  if (g_Failures != 0) {
    std::cerr << "feedback_driver_test: " << g_Failures << " failure(s)\n";
    return 1;
  }
  std::cout << "feedback_driver_test: PASS (" << mode << ")\n";
  return 0;
}
