# 90 - Scheduled Shooting (deferred)

Status: deferred sketch. Not scheduled for any phase. Revisit trigger is at the
end of this document.

## Idea

Two related features that both need a real time clock.

**Start at a time.** The user sets a wall clock time, puts the device down, and
walks away. furble powers off or sleeps, wakes at the set time, reconnects to
the camera, and starts the intervalometer. This is the classic sunrise timelapse
case. Today the user has to be present at the start.

**Camera time sync without GPS.** furble already sends date and time to cameras,
but only as part of the geodata update, and only when a GPS fix is present
(`src/FurbleGPS.cpp:174-187`). A device with a valid RTC could keep camera
clocks correct with no GPS unit attached and no satellite fix.

## Why it is deferred

**It sits on top of PR19.** PR19 is the deep sleep between intervalometer shots
work. It builds exactly the machinery scheduled shooting needs: persist state
across a power cycle, wake from the PMIC or the RTC, reconnect on boot through
autoconnect, and resume shooting. Scheduled shooting is PR19 with a different
wake source. Building it first would mean building that machinery twice, and
building it worse, because PR19 has the harder problem of doing it repeatedly in
a loop.

**Time entry on a 135 by 240 screen.** The existing spin value widget
(`include/FurbleSpinValue.h`, `UI::Intervalometer::Spinner`) handles a number
plus a unit. A wall clock time needs hours, minutes, and a date, plus a decision
about what happens if the time has already passed today. The intervalometer
spinner is three digit rollers and a unit roller, which is already close to the
limit of what fits. This needs a new widget, and a new widget is the kind of
thing that should not be designed in the same PR as a new wake path.

**RTC drift without GPS.** This is the part that makes the feature less useful
than it sounds.

- The M5StickS3 has no RTC chip. The StickS3 documentation lists the
  ESP32-S3-PICO-1, the M5PM1 PMIC, the BMI270 IMU, the ES8311 codec and a
  250 mAh battery. There is no RTC part. Timekeeping across a power cycle would
  have to come from the M5PM1 timer, which counts down a duration, not from a
  calendar clock.
- The AXP192 boards do have one. The M5StickC and M5StickC Plus2 both carry a
  BM8563.
- A BM8563 running from its own crystal drifts on the order of a minute per
  month at room temperature, and worse at the temperatures a device left outside
  overnight will see. That is fine for "start at 05:30" and it is not fine for
  syncing a camera clock to the second.

So on the primary target board there is no clock to schedule against, and on the
boards that have a clock the accuracy is only good enough for one of the two
features. That is a poor basis for a feature that has to work the same way
everywhere, and the platform principle in the roadmap says features target all
boards unless the hardware physically lacks the capability.

**Time sync is not separable from geotagging in the vendor protocols.** There is
no time only API. `Camera::updateGeoData(const gps_t &, const timesync_t &)`
takes both (`lib/furble/Camera.h:111`). Sony packs latitude, longitude and the
timestamp into one `sony_geo_t` structure and sends it as a single write
(`lib/furble/Sony.cpp:145-160`). Fujifilm only sends geodata when the camera has
asked for it (`lib/furble/Fujifilm.cpp:132-137`). Sending time without a
position means either sending a fake position, which will end up in the user's
EXIF, or adding a vendor by vendor time only path. Neither is a small change,
and only Fujifilm hardware is available for testing.

## Sketch

If this is built, it splits into two independent pieces.

### Wake at a time

**M5StickS3, using the M5PM1.** The M5PM1 provides a timer that performs a power
on when it expires. The documented call is
`pm1.timerSet(seconds, M5PM1_TIM_ACTION_POWERON)` followed by `pm1.shutdown()`.
`Platform::powerOff()` already calls `m_M5PM1.shutdown()`
(`src/FurblePlatform.cpp:76`), so half of the sequence exists.

Because the timer takes a duration and not a time of day, the sequence is:

1. User picks a target wall clock time.
2. furble computes the delta from its current notion of time. Without an RTC
   that notion comes from the last GPS fix, or from a companion app time sync,
   or from the user setting the clock by hand.
3. Store the intervalometer parameters and a "scheduled start" marker in NVS.
4. `timerSet(delta_seconds, POWERON)`, then `shutdown()`.
5. On the next boot, read the marker, let autoconnect reconnect the camera, and
   start the intervalometer.

The error in step 2 is the error in furble's clock, which without an RTC is
unbounded across a long power off. This is why the S3 path realistically needs
the companion app or a GPS fix to set the clock before scheduling.

**M5StickC Plus2, using the BM8563.** The RTC has a calendar alarm, so the
target time is set directly with no delta computation and no drift accumulated
over the wait beyond the RTC's own drift. The alarm output drives the power hold
latch, which is the same mechanism PR19 uses on this board. Same NVS marker,
same resume path.

**Boards with no path.** The M5Stack Core and Core2 with AXP192 do not get a
timed power on. On those boards the feature is either hidden or implemented as
light sleep until the target time, which costs battery but works. Hiding it is
the honest option and matches how PR19 handles the same boards.

### Time sync from the RTC

Only worth doing after the wake path exists, and only with a decision about the
vendor problem above. The least invasive version is a setting that says "send
the RTC time with geodata when there is no GPS fix, using the last known
position". That is a small change and it is also a lie in the EXIF. The correct
version is a vendor by vendor time only write, which is a research task per
vendor with no hardware to test most of them on.

If this is built at all, it should probably be built on the companion app
instead. A phone has an accurate clock and a network, and section 3.3 of
`plans/50-companion-app-design.md` already carries a full timestamp in the fix
record.

## Revisit trigger

Revisit when PR19 has shipped and been verified on hardware, specifically the
overnight timelapse test where the device wakes, reconnects, shoots, and sleeps
again without user intervention. At that point the wake and resume machinery is
proven and this feature is a new entry point into it rather than a new
subsystem.

Do not revisit the camera time sync half until either a vendor time only
protocol is confirmed on real hardware, or the companion app has landed and can
supply the timestamp.

## References

- [M5Stack StickS3 hardware, no RTC listed](https://docs.m5stack.com/en/core/StickS3)
- [M5Stack StickS3 timed wake up with M5PM1, timerSet and shutdown](https://docs.m5stack.com/en/arduino/m5sticks3/wakeup)
- [M5Stack StickS3 M5PM1 power levels](https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1)
- [M5Stack M5StickC Plus2, BM8563 RTC](https://docs.m5stack.com/en/core/M5StickC%20PLUS2)
- [M5Stack M5StickC, BM8563 RTC](https://docs.m5stack.com/en/core/m5stickc)
- Furble source: `lib/furble/Camera.h:111`, `lib/furble/Sony.cpp:145-160`,
  `lib/furble/Fujifilm.cpp:132-137`, `src/FurbleGPS.cpp:174-187`,
  `src/FurblePlatform.cpp:76`
