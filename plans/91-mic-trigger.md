# 91 - Microphone Trigger (deferred)

Status: deferred sketch. Not scheduled for any phase. Revisit trigger is at the
end of this document.

## Idea

Fire the shutter on a sound. Point the camera at a balloon, arm the trigger,
pop the balloon, get the frame. The same mechanism covers a hand clap, a shot
timer beep, a splash, or a breaking object.

Every supported board except the M5Stack Core Basic already has a microphone.
The hardware is sitting there unused, in the same way the IMU and the RTC are
unused. That is the appeal.

## Board coverage

| Build environment | Microphone |
|---|---|
| `m5stick-s3` | MEMS microphone through an ES8311 codec, I2S, 65 dB SNR |
| `m5stick-c` | SPM1423 PDM |
| `m5stick-c-plus` | SPM1423 PDM (same family as the StickC and Plus2) |
| `m5stack-core2` | SPM1423 PDM |
| `m5stack-core` | none |

Two different capture paths. The AXP192 and Core2 boards read a PDM microphone.
The S3 reads an I2S codec that has to be brought up over I2C first. M5Unified
abstracts both behind `M5.Mic`, but they do not have the same power profile and
they do not have the same startup latency.

## Why it is deferred

**It fights the power work.** This is the main reason. The roadmap spends
phases 1 and 2 on getting the M5StickS3 down to roughly 3.3 mA connected idle,
mostly by letting the chip enter automatic light sleep. A sound trigger has to
sample continuously to be useful, because a sound that has already happened
cannot be captured retroactively. Continuous sampling means the I2S peripheral
runs, the DMA runs, and a task wakes on every buffer. On the S3 it also means
the ES8311 codec stays powered, which per the M5PM1 power level documentation
sits on the L3B rail together with the LCD backlight and the speaker. Holding
that rail up and holding a no light sleep lock removes the saving that PR07 and
PR12 exist to create.

The honest framing is that a mic trigger is a "plugged in or short session"
feature, not something that can be left armed for hours on a 250 mAh battery.
That is fine, but it means the feature has to be designed around an explicit arm
and disarm with a timeout, and that design should be done after the power model
exists rather than before.

**False triggers.** A threshold on a microphone in the field will fire on wind
across the mic port, on the user's hand touching the device, on the device being
set down, on a car passing, and on the camera's own shutter and mirror. A remote
that fires on its own is worse than no remote, because the user only finds out
later that the card is full of frames of nothing. This is the same objection
already recorded against the double tap shutter in PR17, and that one at least
has a hardware tap engine with its own filtering.

Making this acceptable needs more than a threshold. It needs a noise floor
estimate, a rise time requirement so that a sharp transient is distinguished
from a slow increase in ambient level, and a refractory period. That is real
signal processing work and it needs field testing to tune, not bench testing.

**No obvious safe default.** Every other setting in the roadmap defaults to
current behaviour. This one has no current behaviour, so it defaults to off,
which is correct but also means it gets very little real use and therefore very
little real feedback. Features nobody exercises tend to rot.

## Sketch

If this is built:

**Detection.** Read fixed size blocks from `M5.Mic` at a low sample rate. 16 kHz
is more than enough for a transient detector. For each block compute a simple
energy measure, either RMS or peak absolute amplitude. Maintain an exponentially
weighted moving average of the block energy as the noise floor, with a slow time
constant so a real transient does not pull the floor up with it. Trigger when
the block energy exceeds the noise floor by a user set margin in dB, and when it
did so within one or two blocks rather than over many. Then enter a refractory
period, a fixed debounce of a few hundred milliseconds, during which no further
trigger is possible.

Reuse the EWMA helper from PR02's battery current estimate rather than writing a
second one.

**Firing.** On trigger, `Control::sendCommand(CMD_SHUTTER_PRESS)` followed by
`CMD_SHUTTER_RELEASE` after the configured shutter duration, which is the same
sequence the intervalometer already uses. The mic trigger must not bypass the
control queue.

**UI.** A page on the Connected menu, next to Remote, Bulb and Interval. It
shows a live level bar, the current threshold as a line on that bar, and an
arm and disarm button. Live level is what makes the threshold settable at all,
because a number in dB means nothing without seeing the ambient level next to
it. Arming starts a countdown so the user can get clear of the device before it
becomes live, and arming has a maximum duration after which it disarms itself,
for the battery reason above.

**Settings.** `MIC_TRIGGER` bool, default false, under Settings, Sensors, next
to the IMU settings from PR16. Threshold and arm timeout stored alongside it.
The page is hidden entirely on the M5Stack Core Basic, using the same runtime
`M5.getBoard()` capability check the roadmap uses elsewhere.

**Power.** The microphone is only powered and only sampled while armed. Arming
acquires a named no light sleep lock from the PR06 power module and disarming
releases it. The lock is visible on the power state debug page from PR05, so the
cost is not hidden from the user.

## Revisit trigger

Revisit if users ask for it after the power work has landed, meaning PR07,
PR12, PR13 and PR19 are shipped and the battery behaviour of the device is
understood and measurable. At that point the cost of arming the microphone can
be stated in numbers rather than guessed at, and the arm timeout can be chosen
from data.

Do not build this before PR16 and PR17. Those two establish the Settings,
Sensors submenu, the capability gating pattern for sensor features, and the
project's position on triggers that can fire by accident. This feature should
follow whatever they decide, not decide it first.

## References

- [M5Stack StickS3, ES8311 codec and MEMS microphone](https://docs.m5stack.com/en/core/StickS3)
- [M5Stack StickS3 M5PM1 power levels, microphone on the L3B rail](https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1)
- [M5Stack M5StickC, SPM1423 microphone](https://docs.m5stack.com/en/core/m5stickc)
- [M5Stack M5StickC Plus2, SPM1423 microphone](https://docs.m5stack.com/en/core/M5StickC%20PLUS2)
- [M5Stack Core2, SPM1423 PDM microphone](https://docs.m5stack.com/en/core/core2)
- [M5Stack Core Basic, no microphone listed](https://docs.m5stack.com/en/core/basic)
- Furble source: `include/FurbleControl.h:13-22`, `src/FurbleUIIntervalometer.cpp`
