# 98b - power follow-up audit, idle wake sources and lock churn

## Goal

Extend plan 98 with the drain sources it did not enumerate. Plan 98 ranked
the big levers (defaults, display, deep sleep, GPS duty). This document
audits the fine structure underneath: every esp_pm lock transition, every
periodic wakeup at 1 Hz or faster, and the BLE idle profile, against the
"battery still feels poor in real use" report.

Audit base: fork master `7fbfff5`, 2026-08-21. Static code audit only. No
firmware changes, no hardware touched. All current figures are estimates
anchored to the plan 98 model values (MCU active 40.2 mA, light sleep
residual ~0.3 mA, connected radio floor 3.3 mA, GPS receiver 23.0 mA on
the S3). Every claim cites file and line at the audit base.

## Corrections to plan 98

Two plan 98 statements are stale at `7fbfff5`:

- "connect (50 ms) ... timers keep firing" is no longer true. The connect
  timer is created paused (`src/FurbleUI.cpp:546-547`) and only runs while
  a connect is in flight (`src/FurbleUI.cpp:2855-2863`).
- The diagnostics 1 s timer is also created paused
  (`src/FurbleUI.cpp:393-394`) and resumes only while its page is open
  (`src/FurbleUI.cpp:1710-1713`). Neither contributes to idle drain.

The battery (5 s) and inactivity (1 s) timers do keep firing as plan 98
says. The feedback timer (10 ms) self-pauses when no pattern is active
(`src/FurbleFeedback.cpp:509`), so it is also clean.

## Lock inventory at the audit base

Complete list of esp_pm lock call sites:

| Lock | Owner | Acquire | Release |
|---|---|---|---|
| NO_LIGHT_SLEEP | control | `src/FurbleControl.cpp:633`, on STATE_ACTIVE with SLEEP_CONN off | `src/FurbleControl.cpp:635`, on leaving STATE_ACTIVE |
| NO_LIGHT_SLEEP | gps | `src/FurbleGPS.cpp:234-245`, S3 only, per burst window | `src/FurbleGPS.cpp:247-252`, per burst end |
| APB_FREQ_MAX | display | `src/FurbleUI.cpp:252` (boot), `src/FurbleUI.cpp:925` (wake) | `src/FurbleUI.cpp:908` (panel sleep) |
| CPU_FREQ_MAX | none | never acquired, diagnostic only | n/a |

The control and display locks behave as plans 07 and 12 designed. The GPS
lock is the churn source investigated below.

## Findings, prioritized

### 1. GPS lock is held about 65 percent of every second at default settings

Drain source. With GPS on at defaults (GPS_NMEA prune off, GPS_BAUD 9600,
GPS_RATE 0, all from `src/FurbleSettings.cpp:306-322`), the AT6668 emits
its full default sentence set every second. At 9600 baud (960 chars/s) a
full epoch of roughly 400 to 600 characters occupies 400 to 600 ms of
line time. The burst window design (`src/FurbleGPS.cpp:38-47`) holds
NO_LIGHT_SLEEP from the first byte until 75 ms (BURST_GAP_MS) after the
last sentence, and re-acquires it 50 ms (WINDOW_DEFAULT_MS) before the
next predicted burst (`src/FurbleGPS.cpp:283-293, 364-366, 409-448`).
Total hold is roughly 550 to 750 ms per second. This is the "churns every
~1 s" observation: the lock cycles once per second and light sleep is only
possible in the remaining 250 to 450 ms.

Estimated cost. About 0.6 x 40 mA of avoidable MCU-awake draw in the
GPS-on screen-off states, roughly 20 to 26 mA, on top of the 23 mA
receiver. The plan 98 GPS scenarios never see this because the model
charges MCU by task activity, not by lock residency.

Concrete change. Three independent steps, in order of value:
- Default GPS_NMEA (sentence pruning) to true. GGA plus RMC is about 140
  characters, cutting line time per epoch to about 150 ms at 9600 baud.
  The CASIC $PCAS03 command already does this (`src/FurbleGPS.cpp:655-659`).
- Prefer 115200 baud when furble configures the receiver. At 115200 the
  pruned epoch is about 12 ms of line time and the hold shrinks to about
  100 ms per second (gap plus lead dominate).
- Shrink BURST_GAP_MS and WINDOW_DEFAULT_MS once the burst is short and
  the prediction is stable. The adaptive window logic already exists
  (`src/FurbleGPS.cpp:456-471`).

Affected boards. S3 only for the lock itself (the guard at
`src/FurbleGPS.cpp:235`). The pruning and baud defaults help all boards
by cutting UART interrupt load.

Hardware verification. `power stats` before and after a 10 minute GPS-on
screen-off soak; compare NO_LIGHT_SLEEP held-time share for owner "gps".
Then an unplugged 30 minute battery-percent soak in the same state, via
the serial console. The ESP-IDF power management doc confirms light sleep
only engages while no NO_LIGHT_SLEEP lock is held
(https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html).

### 2. PERMANENT_LOCK is a silent full-power fallback with no recovery

Drain source. `enterPermanentLock()` (`src/FurbleGPS.cpp:601-607`)
acquires NO_LIGHT_SLEEP and parks the cycle state machine forever. It is
reached from four paths: interval measurement failure
(`src/FurbleGPS.cpp:574-590`), a missed wake deadline
(`src/FurbleGPS.cpp:369-372`), a rail-cycle wake without a pushed fix
(`src/FurbleGPS.cpp:488-492`), and resync with an unknown interval
(`src/FurbleGPS.cpp:388-390`). No timer, no retry, no log above the
normal lock INFO line, no UI indication. A user whose receiver hiccups
once (three bad checksum bursts suffice, `BAD_BURSTS_TO_RESYNC = 3`,
`src/FurbleGPS.cpp:44`) keeps NO_LIGHT_SLEEP held until GPS is toggled.

Estimated cost. Light sleep permanently blocked. In the screen-off
regimes this is the full 40 mA versus 3.6 mA class from plan 98, so up to
about 37 mA. This is a plausible root cause for "battery still poor" on
GPS-using devices that look correctly configured.

Concrete change. Make PERMANENT_LOCK a timed state: after a hold period
(60 s is enough to prove instability), re-enter MEASURING and try again.
Log the transition once at WARN. Show the degraded state on the GPS page
so the user can see the receiver never settled.

Affected boards. S3 (the only board with the lock). After plan 98 action
4 extends the lock to all boards, all of them.

Hardware verification. Force the path from the console: set gps rate to a
value the receiver does not honour, watch `power stats` show the gps
owner count stuck at 1, then confirm the reworked code drops the lock and
re-measures. Soak 30 minutes to confirm no oscillation.

### 3. Control task and every target task poll their queues at 20 Hz forever

Drain source. The control task ticks every 50 ms unconditionally
(`xQueueReceive(m_Queue, &cmd, pdMS_TO_TICKS(50))`,
`src/FurbleControl.cpp:280`), even in STATE_IDLE with zero targets,
because `reapZombieTargets()` runs on every tick
(`src/FurbleControl.cpp:271-276`). Each connected camera adds another 20
Hz poller (`Control::Target::getCommand`, `src/FurbleControl.cpp:80`).
With two cameras connected that is 60 scheduler wakes per second that
exist only to poll empty queues.

Estimated cost. Each wake costs light sleep exit and entry, about 0.5 to
1 ms of active time per the ESP-IDF sleep modes doc
(https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html).
At 20 to 60 Hz that is 1 to 6 percent awake duty, roughly 0.5 to 2.5 mA
in every otherwise sleep-capable state. It also fragments the sleep
windows the plan 10 idle interval (250 to 300 ms) is supposed to open.

Concrete change. Block on the queue for a state-dependent time: 50 ms
only while a teardown is draining or zombies exist, otherwise 500 ms or
portMAX_DELAY (the queue send wakes the task). Same change in
`Target::task()`: idle targets have no reason to wake between commands;
GPS and shutter commands arrive via the queue anyway.

Affected boards. All five.

Hardware verification. `power log` residency comparison over 10 minutes
connected screen-off with SLEEP_CONN on, before and after. The floor
should move measurably below the current 3.3 mA.

### 4. GPS task wakes at 10 Hz even in standby and rail-off

Drain source. `cycleWait()` starts from `uint32_t wait = 100`
(`src/FurbleGPS.cpp:280`) and only shortens it. In STANDBY or RAIL_OFF
with a 15 s duty interval the next deadline is seconds away, yet the task
still wakes every 100 ms to re-check. That is 10 wakes per second during
exactly the window the $PCAS12 standby exists to make cheap.

Estimated cost. About 1 percent awake duty, 0.4 mA, during standby
windows. Small, but it directly erodes the plan 98 action 7 GPS duty
rework, and the fix is one line.

Concrete change. Raise the idle cap to the real next deadline (bounded by
the duty interval), keep `MIN_CYCLE_WAIT_MS` as the floor. The deadline
arithmetic already exists in the same function.

Affected boards. All boards with GPS attached.

Hardware verification. Count `gps_service` wakes via the sim timer-fire
profiler first (the scenario exists), then a hardware `power log` soak in
standby duty mode.

### 5. Release builds log at INFO and the lock churn logs twice per second

Drain source. All five release sdkconfigs ship
`CONFIG_LOG_DEFAULT_LEVEL=3` (INFO, `sdkconfig.m5stick-s3:1797`).
`Power::acquire` and `Power::release` each emit an INFO line on every
transition (`src/FurblePower.cpp:81, 122`). With GPS on that is two log
lines per second from finding 1, forever, in production firmware. Every
line is formatted and pushed out UART0 at 115200 from the GPS task while
the lock dance runs. Camera code adds more INFO traffic per shutter and
per connection profile change (`lib/furble/Camera.cpp:422`).

Estimated cost. Sub-mA on average, but each log line extends the awake
window of the wake that produced it, and the churn lines make `power log`
diagnosis noisy. Zero-cost fix.

Concrete change. Demote the per-transition lock logs to ESP_LOGD. The
counters and `power stats` owner attribution already capture the same
information. Separately, decide whether release builds should ship at
WARN (`CONFIG_LOG_DEFAULT_LEVEL_WARN`); debug envs keep verbose via
`LOG_LOCAL_LEVEL` (`platformio.ini:55`).

Affected boards. All five.

Hardware verification. Build size and a boot log check; confirm `power
stats` still reports owners. No drain soak needed.

### 6. Screen-off wake floor: 5 ms UI loop plus 33 ms LVGL refresh timer

Drain source. The UI task loop delays 5 ms unconditionally
(`src/FurbleUI.cpp:5286`), and LVGL schedules its display refresh timer
at 33 ms (`CONFIG_LV_DEF_REFR_PERIOD=33`, `sdkconfig.m5stick-s3:2501`)
whether or not the panel is asleep. Only the icon timer is paused on
display off (`src/FurbleUI.cpp:906`). With
`CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP=3` (`sdkconfig.m5stick-s3:1718`)
a 5 ms wake period leaves at most 2 ms of eligible idle per cycle, so
automatic light sleep in screen-off states engages only in the gaps the
UI task happens to leave.

Estimated cost. Bounded by the measured 3.3 mA connected screen-off
floor, which already contains this overhead. The unmeasured part is how
much of that floor is wake overhead rather than radio. Expected recovery
is 1 to 2 mA screen-off.

Concrete change. Plan 98 action 9 already says "lengthen the UI task
delay while the display is off". Add the concrete mechanism: while
`m_DisplayOff` is set, pause the LVGL refresh timer
(`lv_display_get_refr_timer`) alongside the icon timer, and stretch the
loop delay to `lv_timer_handler`'s own next-run hint instead of a fixed 5
ms. Input wake paths already run through the indev read callbacks, which
still need a bounded latency; 50 ms screen-off is imperceptible.

Affected boards. All five (Core excluded from battery states per plan
98).

Hardware verification. The owed plan 12 wake-latency test (press within
100 ms of blanking) plus a `power log` residency soak screen-off.

### 7. GPS service timer runs at 1 Hz regardless of GPS state

Drain source. `GPS::startService()` creates a 1 s LVGL timer at UI
construction (`src/FurbleUI.cpp:561`, `src/FurbleGPS.cpp:720-732`,
`SERVICE_MS = 1000`). `update()` runs every second even with GPS disabled
and no companion fix, and while connected it calls
`Control::getInstance().updateGPS()` which queues to every target task
(`src/FurbleGPS.cpp:814-815`), waking them on top of finding 3.

Estimated cost. One extra 1 Hz wake chain. Negligible alone, but it is
the delivery path of the plan 98 geotag defeat (GPS writes keep the link
fast), so it belongs in the same batching rework (plan 98 missing plan 7,
BLE link efficiency).

Concrete change. Pause the timer while GPS is disabled and no external
fix is fresh. When batching geotag writes lands, this timer is where the
batch interval lives.

Affected boards. All five.

Hardware verification. Covered by the finding 3 soak; additionally
confirm geotag cadence on a connected Fujifilm body.

### 8. Companion reconnect advertising runs forever at 1 s intervals

Drain source. With the opt-in companion feature enabled and a bond
stored, the device advertises indefinitely at a 1000 ms interval
(`setAdvertisingInterval(1600)` in 0.625 ms units, `start(0)`,
`src/FurbleCompanion.cpp:274-298`) and the companion task wakes every
second (`src/FurbleCompanion.cpp:381`). This is deliberate design and
correctly stops during scans.

Estimated cost. Roughly 0.05 to 0.1 mA average radio for the three
advertising channels plus one 1 Hz task wake. Acceptable, but it is the
only radio consumer plan 98 does not mention at all.

Concrete change. Optional: decay the advertising interval (1 s for the
first 5 minutes, then 2.5 s) and let the task block on an event instead
of polling once per second. Low priority.

Affected boards. All five, only with COMPANION on (default off,
`src/FurbleSettings.cpp:319-321`).

Hardware verification. nRF sniffer or phone-side scan confirming the
decayed interval; reconnect latency check from the Android app.

## Explicitly checked, no action

- `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y` ships in the release S3
  sdkconfig (`sdkconfig.m5stick-s3:1141`). It only bites while a USB host
  is enumerated, which does not happen on battery. Correct as is.
- The battery timer (5 s) wakes the M5PM1 over I2C; the retry-once trap
  is already handled in the platform layer. Cost is in the plan 98 PM1
  watchdog note.
- Feedback, diagnostics, connect, interval, bulb and pairing timers all
  pause correctly when idle at this audit base.
- The BLE idle profile matches plan 10 as merged: idle 250 to 300 ms,
  latency 0, 10 s threshold, guard timer
  (`lib/furble/Camera.cpp:341-427`). The gaps (default off, geotag
  defeat, unmeasured sub-3.3 mA floor) are already plan 98 items 6 and 7.
- Scan remains active-mode always (`lib/furble/Scan.cpp:18`); passive
  scan is already plan 98's plan-08 note.

## Relation to plan 98

Already covered there, not repeated here: SLEEP_CONN and CONN_SAVER
defaults, display-off defaults and the APB lock scope, GPS duty interval
redesign, deep sleep, auto off, CI gate, calibration debt. New in this
document: findings 1 to 5, 7 and 8, and the two corrections. Finding 6
sharpens plan 98 action 9 with the blocking config values and the LVGL
refresh timer.

Suggested insertion into the plan 98 action list: finding 2 (permanent
lock recovery) and finding 1 (prune plus baud defaults) slot between
actions 4 and 5; finding 3 (queue poll rework) joins action 9; finding 5
(log demotion) is a freebie alongside any of them.

## References

- ESP-IDF power management (esp32s3): lock types, DFS, automatic light
  sleep requires no NO_LIGHT_SLEEP holder.
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html
- ESP-IDF sleep modes (esp32s3): light sleep entry and exit overhead,
  wakeup sources.
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html
- ESP-IDF FreeRTOS tickless idle: `CONFIG_FREERTOS_USE_TICKLESS_IDLE`,
  `CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP`.
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/freertos_idle.html
- AT6668 (CASIC) datasheet and protocol spec: $PCAS03 sentence output
  control, $PCAS12 timed standby, tracking current class; standby current
  in circuit remains the plan 98 calibration debt item.
- M5PM1 datasheet: I2C idle sleep and first-transaction wake, per the
  plan 26 notes.
- Bluetooth Core Specification: advertising interval units of 0.625 ms,
  three-channel advertising events.

## Verification of this document

Static audit only. Every file and line reference was read at fork master
`7fbfff5`. No firmware, sim or tooling changes. The estimates marked as
such require the bench soaks listed per finding before any of them are
treated as measured.
