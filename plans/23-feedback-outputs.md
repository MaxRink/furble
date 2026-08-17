# PR23 - Audible, visual and haptic feedback

## Goal

Tell the user something happened without looking at the screen. Beep on shutter,
count down the last seconds before an intervalometer frame, signal connect and
disconnect, warn on low battery. Every output is off by default, so current
behaviour is unchanged.

## Scope

In scope:

- New `FurbleFeedback` module. One call site per event, one output backend per
  board capability.
- Outputs: speaker or buzzer through `M5.Speaker`, LED blink, vibration motor.
- Events: shutter fired, intervalometer countdown, connect, disconnect, low
  battery.
- `Settings->Feedback` submenu with output, event mask and volume.
- Runtime capability detection. Unavailable outputs are hidden, not compiled out.

Out of scope:

- Sound files or melodies. Tones only.
- The RGB LED bar on Core2 and the M5GO base. That needs an addressable LED
  driver and its presence per board is not established. Note it and move on.
- Feedback for GPS fix acquired. Add later if wanted.

## Hardware support matrix

Verified against M5Stack product pages and the M5Unified board tables listed in
References.

| Board | Speaker | Passive buzzer | Simple LED | Vibration |
|---|---|---|---|---|
| M5StickC | no | no | red, G10 | no |
| M5StickC Plus | no | yes, G2 | red, G10 | no |
| M5StickC Plus2 | no | yes, G2 | red, G19 (shared with IR) | no |
| M5StickC Plus SE | assume Plus family | assume Plus family | verify | no |
| M5StickS3 | yes, ES8311 codec + AW8737 amp, 8 ohm 1 W | no | green download indicator only, no documented GPIO | no |
| M5Stack Core | yes, 1W-0928 on G25 via DAC | no | no | no |
| M5Stack Core2 | yes, NS4168 I2S, amp enable on AXP_IO2 | no | no | yes, AXP192 LDO3 |

So every supported board has at least one output. StickC is LED only. Core and
Core2 have no simple LED. Core2 is the only board with a motor.

M5Unified speaker pin tables, confirmed in the pinned 0.2.13 tag:

- `board_M5Stack`: `use_dac = true`, `pin_data_out = GPIO_NUM_25`.
- `board_M5StickCPlus` and `board_M5StickCPlus2`: `buzzer = true`,
  `pin_data_out = GPIO_NUM_2`.
- `board_M5StackCore2`: I2S BCK G12, WS G0, DOUT G2, plus an enable callback.
- `board_M5StickS3`: I2S MCLK G18, BCK G17, WS G15, DOUT G14, plus an enable
  callback.
- `board_M5StickC`: no internal speaker entry. External HAT only.

## Files to change

| File | Lines | What |
|---|---|---|
| `include/FurbleFeedback.h` | new | `Feedback` singleton, `event_t` enum, `void signal(event_t)` |
| `src/FurbleFeedback.cpp` | new | Capability probe, output backends, tone table |
| `src/CMakeLists.txt` | 1-10 | Add `FurbleFeedback.cpp` to `furble_sources` |
| `src/FurblePlatform.cpp` | 16-22 | `M5.config()` block. Line 19 is `cfg.internal_spk = false;`. Set it from the loaded setting |
| `src/main.cpp` | 27-28 | Requires the `Settings::init()` before `Platform::init()` reorder from PR16 |
| `include/FurbleSettings.h` | 16-29 | `type_t` enum. Add `FB_OUTPUT`, `FB_EVENTS`, `FB_VOLUME` |
| `include/FurbleSettings.h` | 101-148 | `storage_type<>`. Three `uint8_t` bindings |
| `src/FurbleSettings.cpp` | 11-24 | Setting table. Three rows |
| `src/FurbleSettings.cpp` | 169-230 | Defaults |
| `include/FurbleUI.h` | 161-191 | Add `m_FeedbackStr` and the sub entry strings |
| `include/FurbleUI.h` | 299-346 | Add `addFeedbackMenu()` |
| `src/FurbleUI.cpp` | 53-76 | `m_Menu` grid map. Add the new entries |
| `src/FurbleUI.cpp` | 506-616 | Remote page shutter press and release. Shutter event hook |
| `src/FurbleUI.cpp` | 1169-1227 | `UI::intervalometer()` state machine. Countdown and shutter hooks |
| `src/FurbleUI.cpp` | 1109-1167 | `connectTimerHandler` state handling. Connect event hook |
| `src/FurbleUI.cpp` | 2062-2082 | `addSettingsMenu()`. Call `addFeedbackMenu(menu)` |

Low battery hooks into PR13's battery policy check rather than adding a second
poller.

## New settings

| Enum | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|
| `FB_OUTPUT` | `fb_output` (9) | `FURBLE_STR` | `uint8_t` | `0` | 0 Off, 1 Sound, 2 Light, 3 Vibrate, 4 Sound and Light. Off reproduces current silent behaviour |
| `FB_EVENTS` | `fb_events` (9) | `FURBLE_STR` | `uint8_t` | `0x0F` | Bit mask: 0 shutter, 1 countdown, 2 connect and disconnect, 3 low battery. Only consulted when `FB_OUTPUT` is not Off, so the non zero default changes nothing |
| `FB_VOLUME` | `fb_volume` (9) | `FURBLE_STR` | `uint8_t` | `64` | Passed to `M5.Speaker.setVolume()`. Ignored for LED and motor |

Name strings: `"Feedback"`, `"Feedback Events"`, `"Volume"`.

The roller for `FB_OUTPUT` only offers entries the board can do. On StickC it is
`Off\nLight`. On Core it is `Off\nSound`. On Core2 it is
`Off\nSound\nVibrate`. Store the enum value, not the roller index, so the
setting survives a move to different hardware.

## Menu placement

```
Settings
└─ Feedback
   ├─ Output          (roller, board filtered)
   ├─ Events          (four switches)
   └─ Volume          (slider, hidden unless Output includes Sound)
```

`Feedback` is a new submenu created by this PR. Take the next free `{col,row}`
cell on the Settings page grid at `src/FurbleUI.cpp:53-76`. PR01, PR05, PR08,
PR16 and PR22 also add cells, so the final grid is settled by whichever lands
last.

## Implementation notes

### M5.Speaker covers both the buzzer and the codec

`Speaker_Class` exposes `begin()`, `end()`, `isEnabled()`, `setVolume()`,
`tone(float frequency, uint32_t duration)` and `stop()`. M5Unified selects the
backend per board from the pin table. On StickC Plus and Plus2 it sets
`buzzer = true` and drives G2. On Core it uses the DAC on G25. On Core2 and
StickS3 it drives I2S into an external amplifier. One `M5.Speaker.tone()` call
therefore works on all four speaker or buzzer boards. This claim is verified
against the M5Unified 0.2.13 source, which is the version furble pins.

The M5Stack StickC Plus2 buzzer documentation page confirms the same thing from
the other side: the Plus2 buzzer driver is `Speaker_Class` from M5Unified.

`cfg.internal_spk` is currently hardcoded `false` at
`src/FurblePlatform.cpp:19`. It has to become the loaded setting, which means
`Settings::init()` must run before `Platform::init()`. That reorder is already
specified and justified in PR16, so this PR depends on it rather than repeating
it. Changing `FB_OUTPUT` between Off and a sound mode needs a restart, same as
the IMU toggle and the Theme page precedent at `src/FurbleUI.cpp:1984-1992`.

### Amplifier power and the IR receiver conflict

On StickS3 the ES8311 codec and the AW8737 amplifier sit on the L3B rail and are
enabled through M5PM1 GPIO3. M5Unified does this inside its speaker enable
callback: `_speaker_enabled_cb_sticks3` sets the M5PM1 register `0x11` bit 3 on
`Speaker.begin()` and clears it on `Speaker.end()`, and writes the ES8311 power
up sequence over I2C.

The M5Stack low power guide states plainly: "When using the IR receive function,
the SPK amplifier must be turned off." That is a real mutual exclusion on this
board. furble does not use IR receive today. PR22 only transmits, and transmit is
not covered by that note. If a future IR learning PR lands, it must call
`M5.Speaker.end()` first. Record the constraint here so it is not rediscovered.

Power policy: do not leave the amplifier powered. Call `M5.Speaker.begin()`
before a tone and `M5.Speaker.end()` after `isPlaying()` goes false. On StickS3
that keeps the L3B amplifier rail off between beeps. The cost is a few
milliseconds of I2C per beep. Never hold the speaker open across an intervalometer
gap, because the amplifier draw would swamp the sleep savings that PR07 and PR19
buy.

Light sleep interacts with this. I2S output blocks light sleep while a tone is
playing. Keep tones short, 40 to 120 ms, and let the device fall back to sleep.
For the countdown, three separate short beeps beat one long one.

### LED backend

There is no M5Unified LED abstraction for the sticks. `pin_name_t::rgb_led` maps
to G15 on Core and G25 on Core2, which are addressable strip pins on the M5GO
base and the Core2 side bars, not a plain LED. Do not use it.

Use a small per board table:

- `board_M5StickC` and `board_M5StickCPlus`: G10, active low.
- `board_M5StickCPlus2`: G19, active low, shared with the IR emitter.
- Everything else: no LED.

Blink from an `lv_timer` one shot so nothing blocks. Active low needs checking on
device, since the sticks pull the LED cathode.

On StickC Plus2 the LED shares G19 with the IR emitter from PR22. If both PRs
land, the LED backend must ask `IR::isBusy()` before touching the pin and skip
the blink if a frame is in flight. Whichever PR lands second adds the guard.

### Vibration backend

`M5.Power.setVibration(uint8_t level)` is implemented for `board_M5StackCore2`
only among furble's boards. It drives AXP192 LDO3 at `480 + level * 12` mV. Call
it with a level, start a one shot `lv_timer`, and call it with 0 on expiry.
Anything above about 100 ms is unpleasant. Use 60 ms for a shutter tick and two
150 ms pulses for a warning.

### Event mapping

| Event | Sound | Light | Vibrate |
|---|---|---|---|
| Shutter fired | 4 kHz, 40 ms | 40 ms blink | 60 ms |
| Countdown, last 3 s | 2 kHz, 40 ms, once per second | 40 ms blink | none |
| Countdown, frame due | 4 kHz, 80 ms | 80 ms blink | 60 ms |
| Connected | 2 kHz then 3 kHz, 60 ms each | two blinks | 100 ms |
| Disconnected | 3 kHz then 2 kHz, 60 ms each | three blinks | two 100 ms pulses |
| Low battery | 1 kHz, 200 ms, twice | three slow blinks | two 150 ms pulses |

Call sites:

- Shutter: the remote page handlers at `src/FurbleUI.cpp:506-616` and the
  intervalometer `STATE_SHUTTER_OPEN` case at `src/FurbleUI.cpp:1197-1204`.
- Countdown: `m_IntervalPageRefresh` already runs every 333 ms and computes
  `remaining` from `m_IntervalNext` at `src/FurbleUI.cpp:1835-1845`. Fire the
  countdown beep when `remaining` crosses each of 3000, 2000 and 1000 ms. Track
  the last announced second so a 333 ms timer does not beep three times per
  second.
- Connect and disconnect: `connectTimerHandler` around
  `src/FurbleUI.cpp:1109-1167`, on the transition into `STATE_ACTIVE` and into
  `STATE_IDLE`.
- Low battery: PR13 owns the threshold and the hysteresis. Add one `signal()`
  call there.

`Feedback::signal()` checks `FB_OUTPUT` first and returns immediately when Off.
That keeps the default path to a single load of a cached bool.

Cache the settings in the module at init. Do not read NVS on every shutter.

## Dependencies

- PR16, for the `Settings::init()` before `Platform::init()` reorder that lets
  `cfg.internal_spk` come from a setting.
- PR13, for the low battery event. If PR13 is not merged, ship without that event
  bit and add it in a follow up.
- PR22 interacts on StickC Plus2 through the shared G19 pin.
- PR05 is useful for a Diagnostics page that fires each output on demand, but not
  required.

## Risks

- Enabling the speaker costs current even when silent on the I2S boards, which is
  why the amplifier is opened and closed per beep rather than held. If the open
  and close latency turns out to be audible as a click, the fallback is to hold
  the amplifier open for 2 s after the last tone and then close it. Measure
  before choosing.
- Beeps block light sleep. A 1 Hz countdown beep for a long intervalometer would
  keep the device awake. Restrict the countdown to the last three seconds, which
  is what the setting name says, and never beep during the wait state.
- StickS3 amplifier and IR receive cannot both be on. Not hit today. Documented
  so a later IR receive PR does not break audio silently.
- Plus2 LED and IR share a pin. Without the guard, an IR frame and an LED blink
  will fight over the pin and both will look wrong.
- Active low versus active high on the stick LEDs is not documented per board.
  Get it wrong and the LED is on all the time, which drains the battery. Verify
  on device before merge.
- Vibration on Core2 runs off the same AXP192 LDO3 rail as other loads on some
  board revisions. Confirm nothing else moves when the motor runs.
- Feedback in the shutter path adds latency. Keep every call non blocking. The
  tone starts and returns. Never call `rmt_tx_wait_all_done` style blocking waits
  from the shutter handler.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

All five clean with `-Wall -Wextra`.

Defaults regression:

1. Erase NVS, flash master, note that the device is silent and dark.
2. Flash this branch on fresh NVS. It must still be silent and dark. The only
   visible change is the new `Settings->Feedback` entry. `cfg.internal_spk` must
   still resolve to false, so no I2S or DAC peripheral is brought up.

On device, M5StickS3 over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Feedback output to Sound, restart. Confirm the log shows the ES8311 enable
   path when a tone plays and the disable path after it ends.
3. Fire the shutter on the Remote page with no camera. Confirm one beep.
4. Confirm the roller does not offer Light or Vibrate on this board.
5. Set volume to 0, 64 and 255 and confirm the difference is audible.

On device, M5StickC Plus or Plus2:

1. Output to Sound. Confirm the G2 buzzer sounds, proving the buzzer backend.
2. Output to Light. Confirm the red LED blinks and is dark between blinks.
3. On Plus2 with PR22 merged, fire IR and a shutter beep in quick succession.
   Neither must leave G19 stuck on.

On device, M5StickC:

1. Confirm the roller offers only Off and Light.

On device, M5Stack Core2:

1. Output to Vibrate. Confirm the motor pulses and stops.
2. Output to Sound. Confirm the NS4168 path works and the amplifier enable
   through AXP_IO2 is released after the tone.

Intervalometer countdown:

1. Set interval wait to 10 s, count 5. Start. Confirm exactly three countdown
   beeps per frame, at 3, 2 and 1 seconds remaining, then one shutter beep.
2. Confirm no beeps during the rest of the wait.

Camera check, Fujifilm only:

1. Connect, confirm the connect signal fires once. Disconnect, confirm the
   disconnect signal fires once. No repeat on reconnect attempts.
2. Run a 30 minute connection with feedback on. No disconnects.

Battery impact, on board instrumentation only:

1. Unplug USB. Log battery voltage every 30 s.
2. 30 minutes connected and idle with feedback Off, then 30 minutes with feedback
   Sound and no events firing. The slopes must match. If they do not, the
   amplifier is being left powered.
3. Repeat on Core2 with Vibrate selected and idle.

## Implementation status

Implemented on `feat/23-feedback-outputs`.

Rebase notes:

- `FB_OUTPUT` is wire_id 33, `FB_EVENTS` 34 and `FB_VOLUME` 35, continuing
  after `IR_PROTO` (32) from PR 29.
- `src/FurbleCompanion.cpp` settingType and settingValue cover all three as
  SETTING_U8.
- Master's intervalometer state atomics coexist with this branch's countdown
  announcement fields; both variable sets are kept in `FurbleUI`.

- Added cached feedback settings for output, event masks, and volume. Output
  remains Off by default, so existing silent behavior is unchanged until a user
  selects an output.
- Added nonblocking sound, LED, and vibration drivers with board capability
  filtering. Sound uses M5Unified `M5.Speaker`; StickS3 speaker power is active
  only while a tone is playing, including the M5PM1 amplifier callback path.
- Added the Settings > Feedback menu with an output roller, event switches, a
  sound volume slider, and a restart action for output changes that affect the
  M5Unified speaker configuration.
- Added shutter, countdown, connection, and low battery event hooks. Countdown
  feedback fires once at each of 3, 2, and 1 seconds remaining before every
  frame: the initial wait and every inter-frame delay both arm the countdown,
  matching the verification expectation of three beeps per frame. The
  announced-second latch resets on every intervalometer state entry so each
  frame's 3-2-1 fires exactly once.
- The low battery implementation uses a 10 percent threshold and six
  consecutive battery samples, then latches until charging. This local policy
  was needed because the planned battery warning dependency was not present in
  this branch.

### Event mapping

Output Off produces `none` for every event on every board. The table shows the
available event output after capability filtering. On boards with multiple
outputs, the selected output controls which listed output is used.

| Board | Shutter fired | Countdown | Connect | Disconnect | Low battery |
| --- | --- | --- | --- | --- | --- |
| M5StickC | LED blink | LED blink | LED blink | LED blink | LED blink |
| M5StickC Plus | beep and/or LED blink | beep and/or LED blink | beep and/or LED blink | beep and/or LED blink | beep and/or LED blink |
| M5StickC Plus2 | beep and/or LED blink | beep and/or LED blink | beep and/or LED blink | beep and/or LED blink | beep and/or LED blink |
| M5StickS3 | beep | beep | beep | beep | beep |
| M5Stack Core | beep | beep | beep | beep | beep |
| M5Stack Core2 | beep or vibration | beep or none | beep or vibration | beep or vibration | beep or vibration |

The StickC Plus and Plus2 expose Sound, Light, and Sound and Light. StickC
exposes Light only. StickS3 and Core expose Sound only. Core2 exposes Sound and
Vibrate. Countdown has no vibration pattern by design. Plus2 LED output uses
G19, which remains subject to the documented IR receive conflict.

### Review fixes and deviations (fork PR 30)

- Restart contract: the output selection is frozen inside `Feedback` at boot
  and `reload()` refreshes only the event mask and volume. Re-reading
  FB_OUTPUT on any event switch toggle used to half-apply a pending output
  change (selected Sound with a silent speaker). The output roller still
  saves immediately and the Restart button applies it; that button now also
  disables the S3 watchdog first, matching the theme restart precedent.
- Tone sequencing: the speaker session stays open across a whole pattern and
  ends only after the final tone. Ending it between tones power-cycled the
  amplifier mid-pattern, popping audibly and missing the 20 ms gap deadlines.
- StickS3 first-beep reliability: the amplifier enable runs through the M5PM1
  and `M5.Speaker.begin()` does not surface the I2C status, so `Feedback`
  pre-wakes the PMIC with a harmless retried read (`Platform::wakeM5PM1()`)
  and retries `begin()` once. A failed begin logs a warning and drops the
  pattern.
- LED boot safety: the LED GPIO is configured only when the boot output
  actually includes Light, and `setLight()` refuses to drive an unconfigured
  pin. Risk note: on the Plus2 G19 is shared with the IR emitter and the LED
  polarity is unverified, so an unconditional `setLight(false)` at boot could
  have keyed the IR diode with the feature off.
- Disconnect feedback: signaled on any drop out of `STATE_ACTIVE`, not only on
  `STATE_IDLE`. With infinite reconnect the control re-enters connecting
  without passing through idle, so a real link drop never signaled and the
  stale connected flag suppressed the reconnect chirp. The flag guard keeps
  manual disconnects from double-signaling.
- Console and companion reload: `settings set fb_events|fb_volume` dispatches
  `UI::Request::FEEDBACK_RELOAD` to the UI task. Deviation: the companion
  calls `Feedback::reload()` directly (like its GPS case) instead of using
  the request queue, because the queue is compiled only with FURBLE_CONSOLE
  and the companion also runs in release builds. With the output frozen,
  `reload()` is two byte stores and task-safe.
- Volume slider: live preview via `Feedback::setVolume()` on value change,
  NVS persist on release only, following the brightness slider precedent.
- Low battery triggers on the smoothed mean level instead of the raw sample,
  which jitters across the 10 percent threshold.
- `feedback test <event>` console command bypasses the event mask (but honors
  the output selection) so the owed hardware verification is scriptable.
- `fb_output` console values are validated against the enum range 0-4.
- M5Tough is an explicit no-output entry in the capability table and the
  Feedback menu is not built when Off is the only available output.
- Measure later on hardware: per-beep I2S RAM churn. Each pattern still runs
  one `M5.Speaker.begin()`/`end()` cycle, which allocates and frees the i2s
  channel.

### Build verification

- `m5stick-s3`: PASS on the final-state rebuild.
- `m5stick-c-plus`: PASS on the final-state rebuild. The earlier parallel build
  hit a shared dependency materialization race and passed on its one retry.
- `m5stack-core2`: PASS on the final-state rebuild.
- Review-fix state: `m5stick-s3`, `m5stick-s3-debug`, `m5stick-c` (no-sound
  path) and `m5stack-core` (DAC speaker path) all PASS.

Hardware verification is still owed for the StickS3: beep patterns per event,
first-beep-after-idle reliability, countdown cadence per frame, volume slider
live preview, and the restart-to-apply flow.

## References

All links fetched and checked.

- M5StickS3 product page, ES8311 codec, AW8737 amplifier, 8 ohm 1 W speaker, and
  the green LED being a download mode indicator with no documented GPIO:
  https://docs.m5stack.com/en/core/StickS3
- M5StickS3 low power guide, power rails L0 to L3B, the amplifier on M5PM1 GPIO3,
  and the statement that the speaker amplifier must be off when using IR receive:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- M5StickS3 speaker example, `M5.Speaker.tone()` usage:
  https://docs.m5stack.com/en/arduino/m5sticks3/speaker
- M5StickC product page, red LED on G10, no buzzer and no speaker:
  https://docs.m5stack.com/en/core/m5stickc
- M5StickC Plus product page, passive buzzer on G2, red LED on G10:
  https://docs.m5stack.com/en/core/m5stickc_plus
- M5StickC Plus2 product page, passive buzzer on G2, red LED and IR emitter both
  on G19: https://docs.m5stack.com/en/core/M5StickC%20PLUS2
- M5StickC Plus2 buzzer guide, confirms the buzzer is driven by the M5Unified
  `Speaker_Class`: https://docs.m5stack.com/en/arduino/m5stickc_plus2/buzzer
- M5Stack Core Basic product page, 1W-0928 speaker on G25:
  https://docs.m5stack.com/en/core/basic
- M5Stack Core2 product page, NS4168 amplifier on I2S G12/G0/G2, speaker enable
  on AXP_IO2, vibration motor on AXP192 LDO3:
  https://docs.m5stack.com/en/core/core2
- M5Unified Speaker class API, `begin`, `end`, `tone`, `setVolume`, `isPlaying`:
  https://docs.m5stack.com/en/arduino/m5unified/speaker_class
- M5Unified `Speaker_Class` header, confirms the API surface:
  https://github.com/m5stack/M5Unified/blob/master/src/utility/Speaker_Class.hpp
- M5Unified board speaker pin tables and the StickS3 amplifier enable callback:
  https://github.com/m5stack/M5Unified/blob/master/src/M5Unified.cpp
- M5Unified `Power_Class::setVibration`, implemented for Core2 through AXP192
  LDO3: https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.cpp
- M5Unified Power class API reference:
  https://docs.m5stack.com/en/arduino/m5unified/power_class

## Hardware verification, 2026-08-17

Verified on the combined image. Boot logged
`feedback: capabilities: sound=1 light=0 vibration=0` with `fb_output 1`. All
five events (`shutter`, `countdown`, `connect`, `disconnect`, `battery`)
queued via `feedback test` and the user confirmed they are audible. User
preference: the sounds are annoying, so `fb_events 0` and `fb_output 0` are
now set. `fb_events` and `fb_volume` apply immediately, `fb_output` on reboot,
as designed.

## Simulator note, 2026-08-17

The host simulator shadows `FurbleFeedback.h` with `sim/shim/FurbleFeedback.h`,
the same pattern as the IR shim. The fake reports no capability beyond Off, so
the Feedback settings menu stays hidden and the scripted menu routes keep
their existing positions.
