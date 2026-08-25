# PR19 - Deep sleep between intervalometer shots

## Goal

Power the device down between intervalometer shots when the gap is long enough,
and bring it back in time for the next frame. This is the largest available
battery win for timelapse work. Off by default.

## Scope

In scope:

- New `IVL_SLEEP` setting, default false.
- New `IVL_SLEEP_THR` setting, threshold in seconds, default 60.
- StickS3 path: M5PM1 timed power on plus `shutdown()`.
- StickC Plus2 path: BM8563 RTC alarm plus the GPIO4 HOLD latch.
- Persisting intervalometer progress across the power cycle.
- Resume on boot through the existing auto connect path.
- Runtime hiding of the feature on boards that cannot self wake.

Out of scope:

- Scheduled start at a wall clock time. That is the deferred
  `plans/90-scheduled-shooting.md`.
- Any change to the intervalometer timing model itself.
- Reducing reconnect time. That is PR09 and PR10.

## Files to change

Verified anchors against the current tree.

| File | Lines | What |
|---|---|---|
| `include/FurbleSettings.h` | 16-29 | `type_t` enum. Add `IVL_SLEEP`, `IVL_SLEEP_THR`. |
| `include/FurbleSettings.h` | 101-148 | `storage_type<>` bindings. `bool` and `uint32_t`. |
| `src/FurbleSettings.cpp` | 11-24 | Setting table. Two new rows. |
| `src/FurbleSettings.cpp` | 169-230 | Defaults. `IVL_SLEEP` joins the false group at 209-215. `IVL_SLEEP_THR` needs a `uint32_t` case next to `GPS_BAUD` at 216-218. |
| `include/FurbleUI.h` | 105-126 | `Intervalometer` class and `state_t`. Add the resume fields. |
| `include/FurbleUI.h` | 173-191 | Menu strings. Add the deep sleep entries. |
| `src/FurbleUI.cpp` | 48-49 | `m_IntervalPageRefresh` and `m_IntervalNext` statics. |
| `src/FurbleUI.cpp` | 53-76 | `m_Menu` map. Add the deep sleep page entries. |
| `src/FurbleUI.cpp` | 304-306 | `m_IntervalTimer` created at 100 ms and paused. |
| `src/FurbleUI.cpp` | 918-926 | Auto connect on first display of the main menu. The resume path hangs off this. |
| `src/FurbleUI.cpp` | 1169-1227 | `UI::intervalometer()` state machine. `STATE_DELAY` at 1205-1214 is where the sleep decision goes. `m_IntervalNext` is set at 1225. |
| `src/FurbleUI.cpp` | 1772-1803 | `addIntervalometerMenu()` and the Start button handler at 1791-1803. |
| `src/FurbleUI.cpp` | 1815-1830 | Stop button. Must clear persisted resume state. |
| `src/FurbleUIIntervalometer.cpp` | 14-18 | `Intervalometer::save()` writes the `INTERVAL` blob. Model the resume blob on it. |
| `src/FurblePlatform.cpp` | 74-80 | `Platform::powerOff()`. S3 uses `m_M5PM1.shutdown()`, others `M5.Power.powerOff()`. Add a timed variant next to it. |
| `include/FurblePlatform.h` | 33-41 | Public API. Add `bool canTimedWake()` and `bool powerOffUntil(uint32_t seconds)`. |
| `src/FurblePlatform.cpp` | 24-32 | Existing `M5.getBoard()` switch. Extend for the capability check. |

## New settings

| Enum | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|
| `IVL_SLEEP` | `ivl_sleep` (9) | `FURBLE_STR` | `bool` | `false` | False keeps the current always-awake intervalometer. |
| `IVL_SLEEP_THR` | `ivl_sleep_thr` (13) | `FURBLE_STR` | `uint32_t` | `60` | Minimum gap in seconds before sleeping. Only read when `IVL_SLEEP` is true, so the default changes nothing. |

Name strings: `"Deep Sleep"` and `"Sleep Threshold"`.

Wire ids: `IVL_SLEEP` uses 42 and `IVL_SLEEP_THR` uses 43. These were renumbered
during the rebase onto the current ledger from the provisional 32 and 33, which
now collide with the merged `IR_PROTO` (32) and `FB_OUTPUT` (33). They are free
at the rebased base and are frozen for this slice.

A `uint32_t` is used rather than a `uint16_t` because `Settings` already has
`load<uint32_t>` and `save<uint32_t>` specialisations
(`src/FurbleSettings.cpp:57-60`, `src/FurbleSettings.cpp:143-146`). Adding a
`uint16_t` specialisation would be extra surface for no gain. The UI clamps the
value to a sane range.

Resume state is not a `Settings` entry. It is runtime state, not configuration.
See the implementation notes.

## Menu placement

```
Settings
└─ Timer
   ├─ Count            (existing)
   ├─ Delay            (existing)
   ├─ Shutter          (existing)
   ├─ Wait             (existing)
   ├─ Deep Sleep       (switch, hidden on boards without timed wake)
   └─ Sleep Threshold  (spinner, seconds, hidden with the switch)
```

`Timer` is the existing `m_IntervalometerStr` menu at `src/FurbleUI.cpp:1772`.
No new submenu.

Hide both entries at runtime when `Platform::canTimedWake()` is false, using the
same hide pattern as the GPS entries at `src/FurbleUI.cpp:1550-1553`. Nothing is
compiled out.

Board support:

| Board | Timed wake | Reason |
|---|---|---|
| StickS3 | yes | M5PM1 `timerSet` with `M5PM1_TIM_ACTION_POWERON` |
| StickC Plus2 | yes | BM8563 RTC alarm drives the power on signal |
| StickC, StickC Plus, Core, Core2, Tough | no | AXP192 or IP5306 power off has no self wake, and the AXP192 quiescent floor is around 2 mA anyway |

ESP deep sleep is not usable on the Plus2 for this. Its power path cuts the ESP32
rail, so an ESP timer wake source does not survive. The RTC alarm is the only
self wake there.

## Implementation notes

Decision point. In `UI::intervalometer()`, `STATE_DELAY`
(`src/FurbleUI.cpp:1205-1214`) computes the next interval into `next` and the
handler sets `m_IntervalNext` at line 1225. Add the check there:

```
if (IVL_SLEEP && next >= IVL_SLEEP_THR * 1000 && canTimedWake()) -> sleep path
```

Subtract a wake margin from the sleep duration. The device needs time to boot,
scan, connect and be ready to fire. Budget it from measurement, not a guess.
Start with 15 s and tune. If the measured reconnect time exceeds the margin, the
frame is late, so the margin must be generous rather than tight.

Resume state. Before sleeping, persist:

- shot count so far (the `count` static at `src/FurbleUI.cpp:1174`, which must
  become a member),
- target count and the interval values already in the `INTERVAL` blob,
- a resume magic and version,
- the intended wake time.

Two storage options:

- NVS through the existing `Preferences` wrapper. Simple, matches the rest of the
  codebase, survives a battery pull. Costs a flash write per shot. At one write
  per frame and a few thousand frames per night, this is within NVS wear limits,
  but it is not free.
- M5PM1 RTC RAM on StickS3. `writeRtcRAM(uint8_t offset, const uint8_t *data,
  uint8_t len)` and `readRtcRAM(uint8_t offset, uint8_t *data, uint8_t len)`,
  32 bytes total at registers 0xA0 to 0xBF. No flash wear. Lost if the battery is
  removed. Not available on the Plus2.

Take NVS as the portable path, and use RTC RAM on StickS3 as an optimisation only
if the flash write proves to cost measurable time or power. Keep one interface so
the state machine does not care which is used.

Clear the resume state on Stop (`src/FurbleUI.cpp:1815-1830`), on
`STATE_FINISHED` (`src/FurbleUI.cpp:1216-1220`), and whenever the user starts a
new run from the Start button (`src/FurbleUI.cpp:1791-1803`). Stale resume state
that survives into an unrelated boot must not silently start shooting.

Sleep, StickS3:

```
m_M5PM1.timerSet(seconds, M5PM1_TIM_ACTION_POWERON);
m_M5PM1.shutdown();
```

This is a true power cycle, not a sleep. The application restarts from
`app_main()`. M5Unified also wraps this as `M5.Power.timerSleep(seconds)`. Prefer
the M5Unified call where it works on both supported boards, and drop to the
direct M5PM1 calls only if the wrapper does not expose what is needed.

Sleep, StickC Plus2:

Program the BM8563 alarm, then release the HOLD latch. The M5Stack documentation
states that after the wake signal the program must set the HOLD pin (G4) high to
keep power on, otherwise the device shuts down again, and that power on happens
by a long press of button C or by the RTC IRQ signal. So the sequence is: set the
alarm, then drive G4 low to drop power. On the next boot, drive G4 high early,
before anything else, or the device dies mid boot. That means the HOLD assert has
to happen in `Platform::getInstance()` before `M5.begin()` if M5Unified does not
already do it. Verify what M5Unified does on this board before adding a second
assert.

Boot and resume:

1. `app_main()` runs. `Settings::init()` and `Platform::init()`
   (`src/main.cpp:27-28`, order changed by PR16).
2. Read the resume state. If absent or stale, boot normally.
3. If present, set a resume flag on the UI and skip straight to reconnect.
4. Reconnect uses the existing auto connect path at `src/FurbleUI.cpp:918-926`:
   `CameraList::load()`, `CameraList::get(0)`, `setActive(true)`, `doConnect(e)`.
   Do not add a second connect path.
5. On a successful connect, restore the intervalometer counters, set the state to
   `STATE_SHUTTER_OPEN` and resume `m_IntervalTimer`.
6. Fire the frame, then decide about the next sleep as usual.

Failure handling. If the reconnect fails, do not silently sleep again forever.
Retry a bounded number of times, then either finish the sequence with an error
state on screen or keep the device awake so the user can see what happened.
Record the missed frames. Write the chosen policy in the PR body.

The camera has to tolerate the disconnect. Some bodies drop out of remote mode or
sleep themselves after a BLE disconnect. This is the main functional risk and is
per vendor. Only Fujifilm hardware is available, so only Fujifilm is verified.
State that plainly in the PR body and keep every other vendor path untouched.

Auto connect interaction. The resume path is only correct when auto connect can
find the right camera. If more than one camera is saved, resume must target the
camera that was connected when the sleep started, not `CameraList::get(0)`.
Persist the camera index in the resume blob and use it.

GPS interaction. If GPS is on, each wake starts from a cold or warm receiver
state. PR18 and PR15 policies must not run during a deep sleep interval run.
Give this feature priority and disable the motion policy while it is active.

## Dependencies

- PR06 and PR07 for the power module and the sleep infrastructure, per the
  index dependency graph.
- Existing auto connect setting (`Settings::AUTOCONNECT`,
  `src/FurbleUI.cpp:918-926`). The resume path uses it, so the PR must handle the
  case where the user has auto connect off but deep sleep on. Either force the
  behaviour internally for resume, or require auto connect and say so in the UI.
- PR09 (reconnect backoff) is not required, but its behaviour must not fight the
  bounded retry policy here if both are merged.
- Independent of PR16, PR17 and PR18, except for the GPS interaction above.

## Risks

- Missed frames. Wake, boot, scan and connect all take time. If the total exceeds
  the wake margin, the frame is late or lost. This is the reason for the overnight
  verification run with a shot count check.
- Camera drops remote mode over the disconnect. Vendor dependent. Only Fujifilm
  is testable here.
- Stale resume state starting an unexpected sequence after an unrelated reboot.
  Guard with a magic, a version and a wake time sanity check.
- Plus2 HOLD latch. If G4 is not driven high early enough on boot, the device
  powers off mid boot and looks bricked. Recovery is a long press of button C or
  USB power. Document the recovery in the PR body.
- Battery removal loses RTC RAM state on StickS3. NVS does not have this problem.
- NVS wear from one write per frame. Bounded, but worth stating.
- Sleeping while the user is looking at the running intervalometer page is
  surprising. Show a clear "sleeping until HH:MM:SS" state before the power drops.
- Published sleep current figures for the M5PM1 power levels are not in the
  M5Stack documentation. Do not quote numbers that have not been measured on the
  device with on-board instrumentation.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Defaults regression: fresh NVS boot. `IVL_SLEEP` false. Intervalometer behaves
exactly like master, including the Start, Stop, count and state labels.

On device, M5StickS3 over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Confirm Deep Sleep and Sleep Threshold appear in Settings -> Timer.
3. With `IVL_SLEEP` off, run a 5 shot sequence with a 90 s delay. Confirm the
   device stays awake and behaves as before.
4. Turn `IVL_SLEEP` on, threshold 60. Run the same 5 shot sequence. Confirm the
   log shows a sleep decision, the device powers down, and it comes back.
5. Check the wake margin: log the time from boot to shutter for each frame.
   Report minimum, maximum and mean.
6. Run with a delay below the threshold, for example 30 s. Confirm no sleep
   happens.
7. Press Stop mid sequence, power cycle, confirm no resume happens.
8. Pull the battery mid sleep, restore power, confirm the device boots into the
   normal menu and does not shoot.

On device, StickC Plus2 (if available):

1. Repeat steps 2 to 6.
2. Confirm the HOLD latch behaviour: the device stays on after RTC wake, and
   recovery by long press of button C works if it does not.
3. If a Plus2 is not available, say so in the PR body and mark the path as
   untested.

Runtime hiding:

1. Flash an AXP192 board and a Core. Confirm the two entries are hidden and no
   sleep path is reachable.

Camera checks, Fujifilm only:

1. Pair a Fujifilm body. Run 10 shots with a 90 s delay and deep sleep on.
   Compare the frame count on the card against the configured count. They must
   match.
2. Check EXIF timestamps for even spacing. Report the worst deviation.
3. Force a reconnect failure by powering the camera off for one interval. Confirm
   the retry policy behaves as documented and the device does not hang.

Overnight timelapse, the acceptance test:

1. StickS3 on battery, unplugged, Fujifilm body, 8 hours, 120 s interval, deep
   sleep on. Expected frame count is 240.
2. Next morning: compare the frame count on the card against 240. Report the
   exact number and any gaps found in the EXIF timestamps.
3. Report the battery percent at start and at end.
4. Run the same 8 hours with deep sleep off on another night, same settings, and
   report the battery percent delta. This is the headline number for the PR.

Battery measurement uses on-board instrumentation only. No external power meter
is available. Log battery percent and voltage to the console every 30 s while
awake, and dump the log after the run. Readings while USB powered reflect
charging, so all drain runs happen unplugged.

## Implementation state

Implemented on branch `feat/19-interval-deep-sleep`.

- Added `IVL_SLEEP` and `IVL_SLEEP_THR` with persistent NVS settings.
- Added the intervalometer menu controls and runtime hiding on unsupported boards.
- Added NVS resume state with a wake marker, camera index, interval values, shot count,
  target count, magic, version and intended wake time.
- Added the StickS3 M5PM1 timer and shutdown path. All explicit M5PM1 accesses use
  the retry helper. The PM1 watchdog is disarmed before the timed shutdown.
- Added the StickC Plus2 BM8563 timer and GPIO4 HOLD path. The RTC IRQ remains
  available long enough to identify a timed wake during boot.
- `powerOffUntil()` now reports whether timer setup and the power-off request
  were accepted. The UI keeps the resume record when the request succeeds and
  only clears it on a setup failure, so a returning boot can restore the run.
- Resume reconnects through the existing connection path with bounded retries. The
  resume drives `connectAll(false)`, so `Control::connectAll(void)` runs its
  non-infinite branch: it retries while the session-local failure count is below
  two and now waits
  `CONNECT_RETRY_GAP_MS` (3 s) in interruptible slices before each retry. A wake
  that misses the camera gets two spaced retries rather than hammering the radio
  or failing on the first miss. After the bounded retries a still-failed reconnect
  clears the resume state and leaves an error on screen.
- PENDING HARDWARE RETEST: the bounded retry gap needs on-device verification. A
  genuine deep-sleep wake that fails the first reconnect must show two spaced
  retries in the serial log and then either recover or land on the resume error.
- Host simulator coverage now exercises the production resume validator and
  reconnect path. `sim/scripts/run-deep-sleep.sh` persists a resume record and
  timed-wake marker, exits at the simulated power-off boundary, and launches a
  fresh process that consumes the marker and continues the shot count. Dedicated
  scenarios reject invalid metadata and stale wake times, verify the failed
  timed-power-off fallback, and assert unsupported-board gating. The unsupported
  scenario runs on the M5StickC simulator build. `.github/workflows/sim-e2e.yml`
  runs this complete runner on every simulator CI job, including the two-process
  persistence check. The start fixture uses a 16 second delay with a 10 second
  threshold and waits through the full delay before the simulated shutdown. The
  runner verifies a host-only evidence sidecar showing that both the resume blob
  and timed-wake marker were readable after the first process exited, and bounds
  that process with a portable 30 second watchdog.
- Timer layout coverage is capability-aware in both deterministic scenarios and
  the fuzz invariant. At Large text size the StickS3 must scroll after the Deep
  Sleep and Sleep Threshold rows are added. The unsupported StickC hides those
  rows and must still fit, while compact interactive pages always require no
  overflow.
- The simulator cannot prove the PMIC timer, RTC alarm, GPIO4 HOLD latch, actual
  rail removal, boot-time marker source, camera remote-mode retention, or real
  wake-to-shutter latency. Those remain attached-board checks.
- Timer layout coverage is capability-aware in both deterministic scenarios and
  the fuzz invariant. At Large text size the StickS3 must scroll after the Deep
  Sleep and Sleep Threshold rows are added. The unsupported StickC hides those
  rows and must still fit, while compact interactive pages always require no
  overflow. The simulator exposes `platform.timed_wake` so each scenario first
  proves it is exercising the intended capability class.
- The base tree has no GPS motion-policy hook, so no separate GPS policy change was
  made.
- The sandboxed worktree could not run PlatformIO. The
  `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` build was run on the
  harvest machine at commit time and succeeded.
- Hardware testing is pending.

## References

All links checked.

- StickS3 low power guide, M5PM1 power levels, `timerSet`,
  `M5PM1_TIM_ACTION_POWERON`, `shutdown`, `ldoSetPowerHold`:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- StickS3 wakeup guide, `M5.Power.timerSleep(5)` and the
  `pm1.timerSet(10, M5PM1_TIM_ACTION_POWERON)` plus `pm1.shutdown()` example:
  https://docs.m5stack.com/en/arduino/m5sticks3/wakeup
- M5PM1 Arduino library: https://github.com/m5stack/M5PM1
- M5PM1 function reference, `writeRtcRAM`, `readRtcRAM`, `timerSet`,
  `timerClear`, `shutdown`, `ldoSetPowerHold`:
  https://github.com/m5stack/M5PM1/blob/main/README_FUNCTION_EN.md
- M5Unified Power class, `timerSleep`, `deepSleep`, `lightSleep`, `powerOff`:
  https://docs.m5stack.com/en/arduino/m5unified/power_class
- StickC-Plus2 docs, GPIO4 HOLD pin and RTC IRQ power on:
  https://docs.m5stack.com/en/core/M5StickC%20PLUS2
- StickS3 product page: https://docs.m5stack.com/en/core/StickS3
- ESP-IDF sleep modes, wake sources and RTC memory retention:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/sleep_modes.html
