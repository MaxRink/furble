# PR26: M5PM1 hardware watchdog on the StickS3

## Goal

Feed the M5PM1 hardware watchdog on the M5StickS3 so a wedged firmware resets
itself instead of leaving a device that looks dead. Scoped to the S3 only,
because only the S3 has the PM1. No change on any other board.

All line anchors below were read at commit `2b79ce8` on `master`.

## Motivation

A StickS3 running a development build locked up. The screen went dark, the
buttons did nothing, and the USB port stopped enumerating. There was no way to
reset it from the outside.

That is not bad luck, it is a direct consequence of how furble sets the device
up. At boot furble disables every power button gesture the PM1 offers, so it can
use the button as a plain input for the UI:

- `src/FurblePlatform.cpp:36`, `setSingleResetDisable(true)`, kills single click
  reset
- `src/FurblePlatform.cpp:37`, `setDoubleOffDisable(true)`, kills double click
  power off
- `src/FurblePlatform.cpp:38`, `setDownloadLock(true)`, kills long press into
  download mode

The M5StickS3 product page documents exactly those three gestures as the only
button behaviours: "Long press: Enter download mode", "Double click: Power off",
"Single click: Power on / Reset". furble turns all three off. Once the firmware
stops running, nothing is left to turn them back on. The ESP32 task watchdog
does not help either. It is enabled with a 5 s timeout but
`CONFIG_ESP_TASK_WDT_PANIC` is not set (`sdkconfig.m5stick-s3:1473-1477`), so it
prints a backtrace and carries on. It never resets anything.

The PM1 is a separate chip on its own power domain with its own watchdog. It
keeps counting when the ESP32 is wedged. Using it turns a bricked-looking device
into one that reboots in ten seconds.

## Draft issue

Open this before any code. Motivation only, no design.

> **StickS3 can lock up with no way to reset it**
>
> I managed to wedge a StickS3 while developing against furble. The screen
> stayed dark, the buttons did nothing, and the USB port stopped enumerating, so
> I could not reset it or reflash it in the normal way. Looking at the code,
> furble disables all three M5PM1 power button gestures at boot so it can use
> the button as a UI input, which means once the firmware stops running there is
> no button path left. The only way back was holding the side button while
> replugging USB to force download mode, which is not obvious and not documented
> anywhere. The PM1 has a hardware watchdog that could recover this
> automatically. Would a PR wiring that up be welcome?

## Scope

In scope:

- Arm the PM1 watchdog at boot on the S3 and feed it from the running firmware.
- Disarm it before any deliberate shutdown or restart.
- One setting to turn it off, needed for JTAG debugging.
- Document the manual rescue procedure for a locked-up StickS3.

Out of scope:

- Any change to the three `set*Disable` calls at `src/FurblePlatform.cpp:36-38`.
  Changing which button gestures furble claims is a separate discussion and a
  separate PR.
- Any change to the ESP32 task watchdog config on any board. See the
  alternatives note.
- Anything on the StickC, StickC Plus, Core or Core2. They have no PM1.

## Files to change

- `src/FurblePlatform.cpp:34-39`, the `FURBLE_M5STICKS3` block inside
  `Platform::getInstance()`. Line 35 is `m_M5PM1.begin(&M5.In_I2C)`, which must
  happen before any watchdog register access. Lines 36-38 are the three gesture
  disables. Arm the watchdog immediately after line 38.
- `src/FurblePlatform.cpp:74-80`, `Platform::powerOff()`. Under
  `FURBLE_M5STICKS3` it calls `m_M5PM1.shutdown()` at line 76. Disarm the
  watchdog before that call.
- `src/FurblePlatform.cpp:82-101`, `Platform::update()`. It runs every UI loop
  iteration and already talks to the PM1 over I2C at line 86
  (`m_M5PM1.btnGetState`). This is the natural place for the feed, since it is
  already the one function that touches the PM1 on every pass.
- `include/FurblePlatform.h:7-58`, `class Platform`. Add the public feed and
  enable entry points next to `update()` (line 31), `powerOff()` (line 36) and
  `setSleep()` (line 41). `M5PM1 m_M5PM1` is the private member at line 52.
- `src/FurbleUI.cpp:2123-2134`, `UI::task`. Line 2125 calls
  `Platform::getInstance().update()` and line 2133 is the `vTaskDelay(5 ms)`
  that bounds the loop. This is the loop whose death is the failure being
  guarded against.
- `src/main.cpp:21-40`, `app_main`. `Furble::Platform::init()` is at line 27 and
  `vUITask(NULL)` at line 39. The UI runs in the `app_main` host task, so if
  that task stops, nothing feeds the watchdog.
- `include/FurbleSettings.h:16-29`, `type_t`. Add `WATCHDOG`.
- `include/FurbleSettings.h:141-148`, the bool `storage_type` specialisations.
  Add one for `WATCHDOG`.
- `src/FurbleSettings.cpp:11-24`, `m_Setting`. Add the name, key and namespace
  row.
- `src/FurbleSettings.cpp:209-215`, the `Settings::init` default block. The bool
  settings all share one `case` fallthrough and default to `false` at line 214.
  `WATCHDOG` needs its own case defaulting to `true`, see New settings below.
- `src/FurbleUI.cpp:1613-1620`, `UI::addFeaturesMenu`. Lines 1616-1619 add the
  Auto-Connect, FauxNY, Infinite-ReConnect and Multi-Connect switches. Add the
  Watchdog switch here, under `FURBLE_M5STICKS3` only.
- `README.md`, add the rescue procedure. Users who hit this will look there
  first.

## New settings

| Setting | Type | Default | Boards |
|---|---|---|---|
| `WATCHDOG` | bool | `true` | M5StickS3 only |

Default `true` is a behaviour change, and it is the right one.

Argument for `true`: the failure it prevents is a device that appears dead and
that most users cannot recover, because the rescue path is an undocumented
button and USB dance. The failure it introduces is a spurious reset if some
legitimate operation blocks the feed for longer than the timeout. With a 10 s
timeout and a 5 ms feed loop, the margin is three orders of magnitude. A safety
net that is off by default protects nobody, because the people who need it are
exactly the people who do not know it exists.

Argument for `false`: it is new behaviour landing silently, and a bug in the
feed path turns a working device into one that reboots every ten seconds. That
is a worse user experience than the lockup it prevents, because it happens to
everybody rather than to one developer.

Recommend `true` and say so plainly in the PR body, with the reasoning above and
the measured feed margin from Verification. If upstream pushes back, default
`false` costs one line in `src/FurbleSettings.cpp` and the PR still lands. Do
not argue about it.

Note also `src/FurbleSettings.cpp:209-215` groups all the bool defaults into one
`case` fallthrough. Adding a bool that defaults to `true` means splitting that
group. Keep the diff small: add `case WATCHDOG: save<bool>(setting.type, true);
break;` above the group rather than restructuring it.

## Menu placement

```
Settings
+- Features
   +- Auto-Connect
   +- FauxNY
   +- Infinite-ReConnect
   +- Multi-Connect
   +- Watchdog      (new, S3 only)
```

One switch, in the list of switches that already exists, on the one board that
has the hardware. On every other board the item is not compiled in, so there is
no dead control anywhere.

This does add a UI element, which cuts against keeping the interface small. It
is justified because attaching a JTAG debugger halts the CPU, the feed stops,
and the PM1 resets the device mid-session. PR00 already documents that
breakpoints during a connection drop the camera link. Without a switch, the S3
becomes undebuggable. If upstream would rather have no switch at all, the
fallback is to compile the watchdog out of the `-debug` environments instead.
Offer both.

## Implementation notes

### API

`M5PM1` exposes three calls, verified in the vendored library at
`.pio/libdeps/m5stick-s3/M5PM1/src/M5PM1.h`, version `M5PM1@1.0.6` pinned at
`platformio.ini:16`:

```
m5pm1_err_t wdtSet(uint8_t timeout_sec);   // 0 disables
m5pm1_err_t wdtFeed();                     // writes 0xA5 to the key register
m5pm1_err_t wdtGetCount(uint8_t *count);   // seconds remaining
```

They are thin register accesses. `wdtSet` writes `M5PM1_REG_WDT_CNT` (0x0A),
`wdtFeed` writes `M5PM1_WDT_FEED_KEY` (0xA5) to `M5PM1_REG_WDT_KEY` (0x0B), and
`wdtGetCount` reads 0x0A back. The register comment says the value is a
countdown in seconds, range 1 to 255, 0 disables, and "System resets when
countdown reaches 0".

Wrap them in `Platform` so nothing outside `FurblePlatform.cpp` includes
`M5PM1.h` for this:

```
void watchdogEnable(bool enable);   // wdtSet(WDT_TIMEOUT_S) or wdtSet(0)
void watchdogFeed(void);            // rate limited wdtFeed()
```

Both compile to nothing outside `FURBLE_M5STICKS3`. `include/FurblePlatform.h`
already includes `M5PM1.h` at line 4 unconditionally, so the header needs no
change beyond the two declarations.

### Where to feed

Feed from `Platform::update()` (`src/FurblePlatform.cpp:82-101`), rate limited
to once per second using `Platform::tick()` (`src/FurblePlatform.cpp:51-53`).

`Platform::update()` is called from `UI::task` at `src/FurbleUI.cpp:2125`, once
every loop, and the loop delays 5 ms at line 2133. So the feed is attempted
about 200 times a second and actually issued once a second. It also already does
an I2C transaction on the S3 at line 86, so the feed adds one more register
write to a bus transaction that was going to happen anyway.

Rate limiting matters. Every feed is an I2C write, and the PM1 has an I2C idle
sleep timeout (`M5PM1_REG_I2C_CFG`, register 0x09, field `SLP_TO`) which puts
the PM1 itself to sleep after an idle period. Feeding at 200 Hz would keep the
PM1 awake continuously and cost power for nothing. Once a second against a 10 s
timeout is a ten times margin, which is plenty. Confirm the current `SLP_TO`
value on device with a register read before merging, and note whether the button
polling at line 86 already keeps the PM1 awake regardless.

What this catches: anything that stops the `app_main` host task. That is the
observed failure and it covers a hung LVGL callback, a deadlock on `UI::m_Mutex`
(`src/FurbleUI.cpp:42`), a hung I2C transaction, and a hard CPU hang.

What it does not catch: the control task (`src/main.cpp:32`) or a per camera
target task (`src/FurbleControl.cpp:246-262`) dying while the UI keeps drawing.
Those are already visible to the user, because the UI stops responding to
shutter presses. Do not try to cover them here. A dedicated low priority feed
task would catch a UI wedge too but would miss a UI-only starvation, so it
trades one blind spot for another and adds a task. The UI loop is the simpler
choice and it covers the failure that was actually seen.

### Timeout choice and light sleep

Use 10 s. The register accepts 1 to 255 seconds, so this is well inside range.

Automatic light sleep is on. `Platform::setSleep(true)` is called at
`src/FurblePlatform.cpp:14`, before `M5.begin`, and it calls `esp_pm_configure`
with `light_sleep_enable` at lines 65-72. `CONFIG_PM_ENABLE=y`
(`sdkconfig.m5stick-s3:1359`) and `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`
(`sdkconfig.m5stick-s3:1680`) back this up.

Automatic light sleep does not break the feed. The ESP-IDF power management
documentation states that sleep duration is computed from FreeRTOS tasks blocked
with finite timeouts, and the chip wakes before the nearest one. `UI::task`
blocks on a 5 ms `vTaskDelay` (`src/FurbleUI.cpp:2133`), so today the longest
the chip can sleep between two feeds is 5 ms. The 10 s timeout has enormous
margin.

The invariant to write down and keep: **the feed period must stay at or below
one third of the watchdog timeout, and the longest possible gap between two
calls to `Platform::update()` must stay well under the timeout.** Later PRs
stretch this. PR07, BLE and light sleep while connected, and PR12, true display
off with longer timeouts, both aim to make the device idle for much longer
between wakes. If any of them lengthens the UI loop delay past a second or two,
this feed and this timeout both have to be revisited. Put a comment next to
`WDT_TIMEOUT_S` saying so.

Two things the PM1 watchdog is not affected by, worth stating because they are
easy to get wrong:

- The PM1 counts in its own time, not ESP32 time. Light sleep on the ESP32 does
  not slow the countdown. That is exactly what makes it useful.
- Waking from light sleep does not need the watchdog. The PM1 resets the ESP32
  on timeout, it does not wake it.

### Deliberate shutdown and restart

Disarm before anything that intentionally stops the firmware, otherwise the PM1
resets a device the user asked to switch off:

- `Platform::powerOff()` (`src/FurblePlatform.cpp:74-80`). Call
  `watchdogEnable(false)` before `m_M5PM1.shutdown()` at line 76.
- The Theme page restart button, which calls `esp_restart()` at
  `src/FurbleUI.cpp:1990`. A reset re-runs `Platform::getInstance()` and
  re-arms, so this is belt and braces, but do it anyway.
- Touch calibration, which takes over the screen. On the S3 there is no touch,
  so this does not apply, but check the code path exists before claiming so.

The M5PM1 header notes that some registers auto-clear on reset, download mode or
shutdown, specifically `M5PM1_REG_HOLD_CFG`. `M5PM1_REG_WDT_CNT` carries no such
note. Do not assume it clears. Verify on device: arm the watchdog, power off
through the menu, and confirm the device stays off for at least 30 s.

### Deep sleep is a hard conflict

PR19 plans ESP32 deep sleep between intervalometer shots. During deep sleep the
firmware cannot feed anything, so an armed 10 s PM1 watchdog resets the device
part way through the interval. Whoever implements PR19 must disarm before
sleeping and re-arm on wake, or use the PM1 timer (`M5PM1_REG_TIM_*`, documented
on the StickS3 low power page) instead of ESP32 deep sleep. Write this into
PR19's Risks section as well as here, so it cannot be missed.

### Alternatives considered

- **Set `CONFIG_ESP_TASK_WDT_PANIC=y`.** Cheaper, one sdkconfig line. Rejected
  as the primary fix for two reasons. It changes committed sdkconfig files for
  all five boards, which PR00 explicitly avoids. And the task watchdog runs on
  the ESP32, so it cannot fire when the ESP32 is the thing that is stuck. It
  only catches idle task starvation. Worth raising with upstream as a separate
  small question, not bundled here.
- **Re-enable double click power off.** Leaving `setDoubleOffDisable(false)`
  (`src/FurblePlatform.cpp:37`) would let a user at least power a wedged device
  off by hand. Much cheaper than a watchdog. Rejected as the primary fix because
  it gives up a button gesture furble uses, and the recovery is still manual.
  But it is a genuinely good partial fix and upstream may prefer it. Mention it
  in the issue and let the maintainer choose.
- **Report the reset cause at boot.** Reading `wdtGetCount` at startup does not
  tell you anything, the counter is reloaded on reset. `esp_reset_reason()`
  after a PM1 reset most likely reports an external reset, not a watchdog.
  Confirm what it actually returns during on-device testing and log one line at
  boot. Do not guess in the PR body.

## Manual rescue for a locked-up StickS3

This belongs in the README as part of the PR, because it is the answer for
anybody whose device is already wedged and who cannot flash a build that has the
watchdog in it.

Case 1, device is off and will not turn on:

1. Single click the side button. The StickS3 documents single click as "Power on
   / Reset". This works whenever the firmware is not running, because the
   gesture disables at `src/FurblePlatform.cpp:36-38` are only applied once
   furble boots.

Case 2, device is wedged, screen dark, USB not enumerating:

1. Unplug the USB cable.
2. Press and hold the side button for about two seconds.
3. The green LED inside the device blinks. Release the button. The device is in
   download mode.
4. Plug the USB cable back in. The port enumerates.
5. Reflash: `pio run -e m5stick-s3 -t upload`.

The two M5Stack pages describe the order slightly differently. The programming
page says hold until the green LED blinks, release, then connect USB. The
product page says connect the cable then hold the reset button. Try USB
unplugged first, since a wedged device that is confusing the host is the case
being recovered from.

Open question to settle on hardware before this PR is written up: furble sets
`setDownloadLock(true)` at `src/FurblePlatform.cpp:38`, which sets the `DL_LOCK`
bit in `M5PM1_REG_BTN_CFG_1` (register 0x49) and disables the long press into
download mode. The header documents `M5PM1_REG_HOLD_CFG` as auto-clearing on
reset, download mode and shutdown, but says nothing of the sort about
`M5PM1_REG_BTN_CFG_1`. If `DL_LOCK` is sticky across a power cycle, the rescue
above does not work on a device that has ever run furble, and the whole recovery
story changes. Test it explicitly: flash furble, let it boot, then attempt the
case 2 procedure. Report the result in the PR body either way. If `DL_LOCK`
turns out to be sticky, that is a much bigger problem than this PR and it should
become its own issue.

## Dependencies

None hard.

PR00, the debug environments, makes this easier to test and is where the
"compile the watchdog out of debug builds" fallback would live.

PR19, interval deep sleep, conflicts. See the deep sleep section.

PR07 and PR12 change how long the device idles between wakes and must respect
the feed period invariant.

## Risks

- A bug in the feed path bricks the device into a ten second reboot loop for
  every S3 user. Mitigation: the setting, plus the fact that the feed sits in a
  function that already runs unconditionally on every loop. Test the failure
  mode deliberately, see Verification step 5.
- The PM1 reset may not restore the device cleanly. `M5PM1_REG_HOLD_CFG`
  auto-clears on reset, which drops the 3.3 V LDO and 5 V rail hold bits. Verify
  what a watchdog reset actually looks like versus a normal reboot, especially
  for the Grove 5 V rail the GPS unit runs on.
- Attaching JTAG halts the CPU and trips the watchdog. Documented, and the
  setting exists for it. Say so in the README next to the debug instructions.
- The I2C bus is shared. If the bus itself wedges, `wdtFeed` fails silently,
  which is the correct behaviour: the device resets. But it also means a
  transient I2C error at exactly the wrong moment could cause a reset. The 10 s
  timeout and 1 Hz feed give ten chances to succeed before that happens.
- `wdtSet` and `wdtFeed` return `m5pm1_err_t` and both can fail. Do not ignore
  the return of `wdtSet`. If arming fails at boot, log it and set the internal
  enable flag to false so the code does not pretend it is protected.
- Only one person has one StickS3. Everything here is untestable by anybody else
  reviewing the PR. Be explicit about that in the PR body.

## Verification

Build matrix, all five environments must still build:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Confirm the four non-S3 binaries are byte identical to master, or that the only
diff is the version string. Nothing in this PR should reach them.

On device, M5StickS3 over USB:

1. Flash. Confirm normal boot, and confirm the boot log states the watchdog was
   armed and at what timeout.
2. Read `wdtGetCount` from a temporary debug hook once a second for a minute.
   Confirm it stays near the timeout and never walks down past the feed margin.
   Record the minimum observed value. That number is the real margin and it goes
   in the PR body.
3. Leave the device idle for ten minutes with the display timeout expired, so
   automatic light sleep is active. Confirm no reset. This is the light sleep
   check.
4. Connect to the Fujifilm camera and leave it connected for ten minutes.
   Confirm no reset. Fire the shutter, run the intervalometer for twenty shots.
   Confirm no reset.
5. Deliberately wedge the firmware. Add a temporary command or a build flag that
   spins in an infinite loop inside an LVGL event callback with interrupts
   enabled. Confirm the device resets after about the timeout. This is the whole
   point of the PR and it must be demonstrated, not assumed.
6. Repeat step 5 with the Watchdog setting turned off. Confirm the device does
   not reset, and stays wedged. Then recover it using the manual rescue
   procedure, and confirm the procedure as written actually works.
7. Power off through the menu. Confirm the device stays off for at least 30 s
   and does not restart itself.
8. Power off, then single click the side button. Confirm it powers on.
9. Log `esp_reset_reason()` at boot. Record what it reports after a normal boot,
   after `esp_restart()`, and after a watchdog reset in step 5. Put all three in
   the PR body.
10. Toggle the Watchdog setting off, reboot, confirm `wdtGetCount` reads 0 and
    step 5 no longer resets. Toggle back on, reboot, confirm it resets again.
11. Confirm the GPS unit still gets its 5 V after a watchdog reset, by
    connecting the GPS/BDS unit and checking for a fix following a reset from
    step 5.

Battery drain: the feed adds one I2C register write per second on a bus that is
already polled every loop for the button state (`src/FurblePlatform.cpp:84-88`).
Measure idle current with the watchdog on and off over ten minutes each and
report both numbers. If the difference is not in the noise, investigate the PM1
I2C idle sleep interaction before merging.

Camera coverage: not applicable. This PR touches no BLE code. A Fujifilm
connection is used only as a realistic load in step 4.

## References

- M5Stack StickS3 product page. Documents the power button gestures: "Long
  press: Enter download mode", "Double click: Power off", "Single click: Power
  on / Reset":
  https://docs.m5stack.com/en/core/StickS3
- M5Stack StickS3 Arduino program compilation and upload. Documents entering
  download mode: hold the side reset button about two seconds until the internal
  green LED blinks, release, then connect USB:
  https://docs.m5stack.com/en/arduino/m5sticks3/program
- M5Stack StickS3 low power configuration. PM1 power levels, manual sleep, I2C
  idle sleep and the PM1 timer. Does not cover the watchdog:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- M5PM1 driver library, source for `wdtSet`, `wdtFeed`, `wdtGetCount` and the
  register map:
  https://github.com/m5stack/M5PM1
- ESP-IDF v5.4, Watchdogs. Task watchdog, `CONFIG_ESP_TASK_WDT_TIMEOUT_S`,
  `CONFIG_ESP_TASK_WDT_PANIC`, and the interrupt watchdog:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/wdts.html
- ESP-IDF v5.4, Power Management. Automatic light sleep, `esp_pm_configure`, and
  how sleep duration is derived from FreeRTOS task timeouts:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/power_management.html
- PlatformIO, device monitor, for the boot log during watchdog testing:
  https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html
