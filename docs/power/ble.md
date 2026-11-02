# BLE power opportunities

Status: research lane, BLE. This file is an evidence base, not a release gate
and not a change list that is ready to merge.

Measurement status: no hardware current measurement was made for this lane, and
no simulator run was made for this file. The simulator was not built in this
worktree because `.pio/libdeps` and SDL2 are absent. Every number below is
copied from a cited source or is arithmetic on cited values. The measured label
appears only for measurements recorded elsewhere in this repository.

Tree read: `power/research` at `b2734c07`. The code is unchanged from
`7383561a`; the commits after it add research docs only. Sibling lane reports
may be edited in the same worktree; this file does not depend on them.

IDs are lane local (`BLE-nn`). The coordinator maps them to `P-nnn` rows in
`docs/power/opportunity-matrix.md`.

## Scope and method

Read for this lane:

- `plans/96-wifi-bt-ble-hardening.md`, `plans/98-power-optimization-audit.md`,
  `plans/98b-power-followup.md`, `plans/158-sim-scheduler-parity.md`
- `plans/07-ble-sleep.md`, `plans/08-scan-tuning.md`, `plans/09-reconnect-backoff.md`,
  `plans/10-adaptive-conn-params.md`, `plans/11-adaptive-tx-power.md`,
  `plans/65-bt-coexistence.md`, `plans/77-battery-saver-profile.md`
- `lib/furble/Scan.cpp`, `lib/furble/Scan.h`, `lib/furble/Camera.cpp`,
  `lib/furble/Camera.h`, `lib/furble/CameraList.cpp`, `lib/furble/Device.cpp`,
  and the matching `lib/furble/protocol/` parsers
- `src/FurbleControl.cpp`, `src/FurbleCompanion.cpp`, `src/FurbleUI.cpp`,
  `src/FurbleSettings.cpp`, `src/FurbleConsole.cpp`, `src/FurbleBtDebug.cpp`
- `components/esp-nimble-cpp` 2.5.0
  (`components/esp-nimble-cpp/src/NimBLEScan.cpp`,
  `components/esp-nimble-cpp/src/NimBLEClient.cpp`,
  `components/esp-nimble-cpp/src/NimBLEClient.h`,
  `components/esp-nimble-cpp/src/NimBLEScan.h`) and the
  ESP-IDF NimBLE headers and Kconfig shipped by `framework-espidf`
- all six release `sdkconfig.*` files and `platformio.ini`
- `tools/power-model/board-currents.yaml`, `tools/power-model/baseline/*.json`,
  `tools/power-model/compare.py`, `tools/power-model/README.md`
- `sim/power_profiler.cpp`, `sim/BleSim.cpp`, `sim/scenarios/manifest.json`
- `docs/power/README.md` and `docs/power/measurement.md` for the evidence
  vocabulary and the shared state IDs

Evidence labels follow `docs/power/README.md`: `measured`, `modeled`,
`inferred`, `unknown`. A `datasheet` or `published-measurement` attribution
names where an inferred number came from. A code reading that establishes a
behavioral fact with no energy number is `inferred`, with the saving marked as
not quantified.

## The short version

1. BLE has two very different power regimes in furble. A connected link that is
   allowed to light sleep sits at a measured 3.3 mA floor on the StickS3. A
   radio that is scanning continuously is on the order of the ESP32-S3
   datasheet BLE receive figure, 93 mA. The second regime is roughly 28 times
   the first.
2. The product spends most of its BLE energy in scan-bound phases, not in the
   connected interval:
   - the discovery page scans at 100 percent duty by default and has no default
     timeout;
   - the Fujifilm Secure and Nikon saved-camera reconnect scans at 100 percent
     duty for up to 60 s per attempt;
   - every `NimBLEClient::connect()` runs the controller's initiator scan at
     10 ms window every 10 ms, which is 100 percent duty, for up to the connect
     timeout.
3. The connection interval is still a real lever, but it is the second lever.
   The current saver lowers the event rate from about 27 per second to about
   3.6 per second. How much of the 3.3 mA floor that removes is unmeasured, and
   the model cannot attribute it.
4. Companion advertising is small at any instant and unbounded in time. The
   pairing window advertises at 100 ms for 2 minutes, and the reconnect
   advertisement runs at 1 s forever while the companion is enabled, bonded, and
   not connected.
5. The power model cannot see any of this today. The only function that charged
   a connected BLE floor, `profilerSetRadioConnected`, has had no caller since
   the production connection stack replaced the simulator camera
   (`9081fc94`). The simulator charges only shutter, focus and geotag command
   events. The checked-in connected baseline still contains a 3.2989 mA radio
   term, but the current code would not produce it, and the loss is 3.9 percent
   of the total, inside the comparator's 10 percent band.
6. Safe now, meaning no new hardware evidence is needed to know the change is
   safe: bound the discovery scan by default, stop it when the panel sleeps,
   halve the connection-initiation scan duty, shorten the companion pairing
   window, stop a peer parameter override from permanently disabling the
   connection saver, and default the reconnect backoff on. The reconnect scan is
   the biggest single case and it needs one measurement first: the camera
   advertising interval.

## How furble uses BLE

furble is always the central for camera links. It creates a client per camera
and never advertises for the camera role. The only advertising furble does is
the companion GATT server, which is off by default.

| Phase | Code | Radio behavior |
| --- | --- | --- |
| Idle, no link | `Scan` inactive, no client | No BLE radio. The controller is initialized and modem sleeps. |
| Discovery page | `UI::startScan` to `Scan::start` (`src/FurbleUI.cpp:6741-6778`) | Controller scan at the user preset. Default Full, 100 percent duty. No default timeout. |
| Connect | `Control::connectAll` to `Camera::connect` (`src/FurbleControl.cpp:236-281`, `lib/furble/Camera.cpp:203-377`) | For Fujifilm Secure and Nikon, a targeted scan first. Then `NimBLEClient::connect`, which uses the initiator scan at 100 percent duty. |
| GATT setup | vendor `_connect`, `Camera::gattSubscribe` | Service and characteristic discovery, then CCCD writes at the fast interval. |
| Connected fast | `Camera.h:469-472` | 30 to 50 ms requested interval, latency 1, 5.12 s supervision timeout. About 20 to 33 connection events per second. |
| Connected idle | `Camera.h:479-502`, `Camera.cpp:472-596` | 250 to 300 ms requested interval, latency 0, 7 s supervision timeout. About 3.3 to 4 events per second. Only with `CONN_SAVER` on. |
| Reconnect | `Control::connectAll` retry loop, `ReconnectBackoff::delayMs` | Flat 5 s wait by default, or 2.5 s then 10, 20, 40, 80, 120 s with backoff. Each attempt re-enters the connect phase above. |
| Companion pairing | `src/FurbleCompanion.cpp:302-344` | Connectable undirected advertising at 100 ms for up to 2 minutes. |
| Companion reconnect | `src/FurbleCompanion.cpp:347-369` | Connectable advertising at 1000 ms with a connect whitelist, `start(0)`, no timeout. |

## Units and what they mean

- Connection interval is in 1.25 ms units. Supervision timeout is in 10 ms
  units. `NimBLEClient::setConnectionParams` and `updateConnParams` pass the
  values through without conversion.
- Scan interval and window are passed to `NimBLEScan::setInterval` and
  `setWindow` in milliseconds; esp-nimble-cpp converts to 0.625 ms units.
- Advertising interval in `NimBLEAdvertising::setAdvertisingInterval` is in
  0.625 ms units.
- furble is the central on camera links, so peripheral latency lets the camera
  skip events but does not let furble skip its own receive windows. This is the
  correction plan 10 already records. Latency is not a furble power lever.

## Duty cycle arithmetic

These are the duty cycles the code actually requests. Duty is window divided by
interval.

| Path | Window / interval | Duty | Duration in the current code |
| --- | --- | --- | --- |
| Discovery, Full | 6553 / 6553 ms | 100 percent | Until the page is left, or the optional timeout |
| Discovery, Balanced | 30 / 120 ms | 25 percent | Same |
| Discovery, Low | 50 / 1000 ms | 5 percent | Same |
| Targeted reconnect | Full forced | 100 percent | Up to 60 s per attempt |
| Connection initiation | 10 / 10 ms | 100 percent | Up to the connect timeout, 30 s default or 5 s with infinite reconnect |
| Companion pairing | 100 ms advertising interval, 3 channels | about 1 to 1.5 percent | Up to 120 s |
| Companion reconnect | 1000 ms advertising interval, 3 channels | about 0.1 to 0.15 percent | Until connected, disabled, or a camera scan starts |
| Connected fast | 30 to 50 ms interval | event rate 20 to 33 per second | Whole session unless the saver runs |
| Connected idle | 250 to 300 ms interval | event rate 3.3 to 4 per second | After 10 s quiet with `CONN_SAVER` on |

Two consequences follow.

First, a discovery page left open is a continuous receive load. At the
ESP32-S3 datasheet BLE receive peak of 93 mA (whole chip, 100 percent receive
duty) and the modeled 41 mA display, the device is on the order of 134 mA with
the panel on. The receive figure is a whole-chip current, not an adder to the
MCU row, so do not add the 40 mA MCU figure again.

Second, the reconnect loop is the worst sustained case. With the camera off and
infinite reconnect on, each attempt is a 60 s targeted scan plus a 2.5 to 5 s
wait, so the radio is scanning at 100 percent duty for about 92 to 96 percent of
the time until the backoff grows. This is larger than any connected-idle tuning.

## Opportunity register

The summary table is for scanning. Each entry is detailed below.

| ID | Change | Evidence | Estimated saving | Risk | Effort |
| --- | --- | --- | --- | --- | --- |
| BLE-01 | Targeted reconnect scan at 50 percent duty | inferred | Large, unmeasured | Medium | Small |
| BLE-02 | Bound the targeted reconnect scan | inferred | Large when the camera is off | Medium | Small |
| BLE-03 | Passive targeted scan | inferred | Small to medium, needs capture | Medium | Small |
| BLE-04 | Discovery scan decay | inferred | Large on a long page visit | Medium | Medium |
| BLE-05 | Retune the Balanced preset | inferred | 2.7x less scan radio than Balanced | Medium | Small |
| BLE-06 | Stop the discovery scan when the panel sleeps | inferred | Large while the panel is off | Low | Small |
| BLE-07 | Default discovery scan timeout | inferred | Bounds the page to 60 s | Low | Small |
| BLE-08 | One targeted scan for all saved cameras | inferred | N times less scan for N cameras | Medium | Large |
| BLE-09 | Duplicate filtering | inferred | No radio saving | None | None |
| BLE-10 | Match first, then keep scanning | inferred | No radio saving | None | None |
| BLE-11 | `CONN_SAVER` default | inferred | Event rate down 6 to 8x; measured floor, saving unmeasured | Medium | Small |
| BLE-12 | Raise the fast interval floor | inferred | 1.3 to 3x fewer events | Medium | Small |
| BLE-13 | Idle interval above 300 ms | inferred | Another 1.7 to 3.3x | Medium | Medium |
| BLE-14 | Fast profile latency 1 | inferred | None for the central | None | None |
| BLE-15 | Supervision timeout | inferred | None as a duty lever | High if shortened | None |
| BLE-16 | 2M PHY | inferred | Small, unmeasured | Medium | Small |
| BLE-17 | Data length and MTU | inferred | Negligible, one 51 byte DJI handshake | None | None |
| BLE-18 | Peer override blocks the saver | inferred | Unlocks BLE-11 | Low | Small |
| BLE-19 | Initiation scan at 50 percent duty | inferred | Halves a 100 percent phase | Low | Small |
| BLE-20 | Companion pairing window 60 s | inferred | Halves that window | Low | Small |
| BLE-21 | Bound companion reconnect advertising | inferred | Small, continuous | Medium | Medium |
| BLE-22 | Companion preferred parameters | unknown | Unknown | Medium | Medium |
| BLE-23 | Idle disconnect | inferred | Large for long idles | Medium | Large |
| BLE-24 | Reconnect backoff default | inferred | Fewer attempts per hour | Low | Small |
| BLE-25 | TX power below 3 dBm | inferred | Very small | Medium | Small |
| BLE-26 | Skip GATT discovery on reconnect | inferred | Connect energy, unmeasured | High | Large |
| BLE-27 | CCCD subscription count | inferred | Negligible | None | None |
| BLE-28 | Geotag notification cadence | inferred | Negligible | None | None |
| BLE-29 | NimBLE buffers and tasks | inferred | None, RAM only | None | None |
| BLE-30 | 9 connections vs 6 controller activities | inferred | None, capability bug | None | Small |
| BLE-31 | Controller low power config | inferred | Already correct | None | None |
| BLE-32 | BLE radio absent from the power model | inferred | Enables all of the above | Low | Medium |

## Scan path

### BLE-01 Targeted reconnect scan at 50 percent duty

**Change.** Run the Fujifilm Secure and Nikon saved-camera rescan at a 50 percent
duty preset (30 ms window, 60 ms interval) instead of forcing Full. Keep a
follow-up option at 25 percent after the camera advertising interval is
measured.

**Current behavior.** `Scan::start(NimBLEScanCallbacks *, uint32_t duration, bool
wantDuplicates)` calls `applyMode(Mode::FULL)` unconditionally before every
targeted scan (`lib/furble/Scan.cpp:324`). `FujifilmSecure::_connect` and
`Nikon::_connect` use that overload with `SCAN_TIME_MS = 60000`
(`lib/furble/FujifilmSecure.cpp:157-174`, `lib/furble/Nikon.cpp:82-100`,
`lib/furble/FujifilmSecure.h:106`, `lib/furble/Nikon.h:49`). The discovery
preset is deliberately ignored on this path.

**Evidence.** Inferred. The behavior is read from the code. The energy claim is
datasheet arithmetic.

**Estimated saving.** Inferred. The targeted scan holds the receiver on for 100
percent of up to 60 s. At the ESP32-S3 datasheet receive peak of 93 mA, a 50
percent duty removes about half of the receive current during the scan, with an
upper bound near 46 mA average over the scan window. The 93 mA row is a
whole-chip peak, so treat that as a ceiling, not a prediction. A 25 percent
duty would raise the saving ceiling to about 70 mA.

**Risk.** Medium. A camera that advertises rarely is found later, or an attempt
fails and the backoff grows. The window must be at least as long as one
advertising event. The camera advertising interval is not documented and has not
been measured (plan 08 verification step 3 still owes it).

**Effort.** Small. Add a mode argument to the targeted overload or set the
targeted preset explicitly, then restore the discovery preset on the discovery
path. `applyMode` is already called at every start, so no new state machine is
needed.

**Required test.** Host test that the targeted overload requests 30/60 and the
discovery overload still requests the stored preset. A simulator scenario with
a 1 s advertiser proving a hit within the expected number of windows. On-device
A/B of reconnect time at Full and 50 percent on the X100VI, five trials each.

**Citation.** `lib/furble/Scan.cpp:99-105`, `:300-337`; `lib/furble/Scan.h:37-41`,
`:159-173`; `tools/power-model/board-currents.yaml` `esp32s3_radio.ble_rx`.

### BLE-02 Bound the targeted reconnect scan

**Change.** Reduce `SCAN_TIME_MS` from 60 s to 10 to 15 s per attempt. Rely on
the existing retry loop instead of one long scan.

**Current behavior.** Both saved-camera rescans wait up to 60 s. The wait loop
polls the queue in 1 s slices and exits when the scan ends, the camera is found,
or the connect is cancelled (`lib/furble/FujifilmSecure.cpp:169-174`,
`lib/furble/Nikon.cpp:92-100`).

**Evidence.** Inferred, code reading plus arithmetic.

**Estimated saving.** Inferred. A 10 s bound cuts scan-on time by 6x per failed
attempt. A camera advertising at 100 ms produces about 100 advertising events
inside 10 s, so the bound is generous for the normal case. A camera advertising
at 1 s produces 10 events, still enough to be seen if the window overlaps, but
the controller only listens at the window duty.

**Risk.** Medium. A camera with a long standby advertising interval, or a camera
that only advertises when its screen is touched, could be missed. The retry
covers it, at the cost of reconnect time.

**Effort.** Small, but the constant is shared by two vendor classes and the
comment in plan 08 explains why 60 s was chosen.

**Required test.** Host test that the scan duration passed to NimBLE matches the
new constant and that the wait loop exits when the scan ends. Simulator scenario
with a slow advertiser across two attempts. On-device reconnect success rate
over 20 attempts with the camera on and off.

**Citation.** `lib/furble/FujifilmSecure.cpp:157-174`,
`lib/furble/Nikon.cpp:82-100`, `lib/furble/FujifilmSecure.h:106`,
`lib/furble/Nikon.h:49`.

### BLE-03 Passive targeted scan

**Change.** Use passive scanning for the targeted reconnect path and keep active
scanning for the discovery page.

**Current behavior.** `Scan::getInstance()` calls `m_Scan->setActiveScan(true)`
once and nothing ever changes it (`lib/furble/Scan.cpp:84`). The targeted
overload changes only interval and window.

**Evidence.** Inferred. One external fact is relevant: the controller's active
scan backoff is enabled, because
`CONFIG_BT_CTRL_SCAN_BACKOFF_UPPERLIMITMAX` is not set, which is documented in
the ESP-IDF Kconfig as "disable active scan backoff" being off. The backoff
reduces scan requests when a response is missed; with one nearby camera that
answers every request, the request rate is still about one per scannable
advertisement.

**Estimated saving.** Inferred, not quantified. Removing active scanning removes
one scan request transmit and one scan response receive per accepted
advertisement, plus the receive window the controller keeps open for the
response. In a quiet room that is small. In a dense 2.4 GHz environment it is
larger. The naive "half the scan energy" claim is still not justified, because
the request and response are short compared with the scan window and the
receiver stays on for the window either way.

**Risk.** Medium. If the fields furble matches on live in the scan response
rather than the advertising indication, passive scanning never matches.
Fujifilm Secure parses manufacturer data and a service UUID
(`lib/furble/FujifilmSecure.cpp:99-135`,
`lib/furble/protocol/FujifilmProtocol.cpp`), Nikon matches manufacturer data and
a service UUID (`lib/furble/Nikon.cpp:58-76`), and the BLE stack is free to put
either in the scan response. A capture is required before changing this.

**Effort.** Small. Set passive in the targeted path and restore active in the
discovery path.

**Required test.** Capture one Fujifilm Secure and one Nikon advertisement with
`bt scan <seconds> duplicates` and confirm the match fields are in the
advertising PDU. Then a host test that the targeted path requests passive and
the discovery path requests active. On-device reconnect success at 1 m and 5 m.

**Citation.** `lib/furble/Scan.cpp:84`, `:323-327`;
`components/esp-nimble-cpp/src/NimBLEScan.cpp:409-411`;
ESP-IDF `components/bt/controller/esp32c3/Kconfig.in` `BT_CTRL_SCAN_BACKOFF_UPPERLIMITMAX`.

### BLE-04 Discovery scan decay

**Change.** Start the discovery page at Full for a bounded window (5 to 10 s),
then restart the scan at Balanced or Low for the rest of the page visit.

**Current behavior.** The discovery scan runs at the stored preset, default Full,
until the page is left or the optional timeout fires (`lib/furble/Scan.cpp:252-298`,
`src/FurbleUI.cpp:6741-6778`). The scan page has a "Scan finished" panel with a
Rescan button (`src/FurbleUI.cpp:6720-6740`), so a restart path already exists.

**Evidence.** Inferred.

**Estimated saving.** Inferred. A decay from 100 percent to 25 percent removes
75 percent of the radio-on time after the decay point, with a saving ceiling
near 70 mA during the decayed phase on the S3 receive figure. Most
user-visible discovery happens in the first seconds; a page left open is the
case that pays.

**Risk.** Medium. A camera switched on after the decay takes longer to appear.
The Rescan button restarts at Full, but the user has to notice the finished
state. Window and interval are applied at scan start, so the decay requires a
restart, which clears the controller duplicate cache and can re-report devices.

**Effort.** Medium. A timer and a restart path in the UI, plus preservation of
the found list across the restart. The current `startScan()` clears
`CameraList` (`src/FurbleUI.cpp:6741-6752`), so a plain restart would drop rows.

**Required test.** Simulator scenario that advances past the decay and asserts a
second scan start at the decayed preset, with the found rows still present. Host
test for the restart helper. On-device discovery latency for a camera turned on
before and after the decay.

**Citation.** `lib/furble/Scan.cpp:99-105`, `:252-298`; `src/FurbleUI.cpp:6720-6778`.

### BLE-05 Retune the Balanced preset

**Change.** Change Balanced from 30 ms window / 120 ms interval (25 percent) to
30 ms / 320 ms (about 9.4 percent), the scan profile the ESPHome bluetooth proxy
fleet uses, as recorded in plan 65.

**Current behavior.** `m_Duty` in `lib/furble/Scan.h:167-173` sets Balanced at
120/30 and Low at 1000/50.

**Evidence.** Inferred, using an external field default as the reference. Plan 65
records ESPHome's 320 ms interval and 30 ms window as the settings that "survive
coex at fleet scale".

**Estimated saving.** Inferred. 30/320 is about 2.7 times less radio-on than
30/120. If Balanced were the default, this would multiply the BLE-04 saving.

**Risk.** Medium. Discovery latency grows with the interval, and a camera whose
advertising interval is longer than the 30 ms window is missed more often. This
is why the camera advertising interval measurement comes first.

**Effort.** Small, one table row, plus the docs rows for the setting.

**Required test.** On-device discovery time at 1 m and 5 m, five trials per
distance, for 30/120 and 30/320. Simulator timing scenario. A soak that leaves
the scan page open for 10 minutes with a camera that advertises once per second.

**Citation.** `lib/furble/Scan.h:167-173`; `plans/65-bt-coexistence.md` lines
356-359; `plans/08-scan-tuning.md` lines 61-69.

### BLE-06 Stop the discovery scan when the panel sleeps

**Change.** Stop the discovery scan in `UI::sleepDisplay()` and reveal the
existing "Scan finished" panel on wake. A wake plus Rescan restarts the scan.

**Current behavior.** `UI::sleepDisplay()` sleeps the panel, releases the APB
frequency lock and pauses the icon timer, and does not touch the scan
(`src/FurbleUI.cpp:1293-1318`). With the Scan page open, the controller keeps
scanning at the stored preset. The default inactivity is Never
(`src/FurbleSettings.cpp:572-575`), so this triggers only when the user has set a
timeout, or with Battery Saver, which forces a 60 s screen-off.

**Evidence.** Inferred, code reading.

**Estimated saving.** Inferred. The full scan receive load for the whole time
the panel is off, with a ceiling near the 93 mA receive figure. On the S3 the
panel-off baseline is 0.25 to 0.51 mA modeled depending on accounting mode
(`screen-off-30s`), so the scan dominates that state by two orders of magnitude.

**Risk.** Low. The list persists. Note that `Scan::stop()` deliberately does not
synthesize `onScanEnd`, because NimBLE's cancel does not either
(`lib/furble/Scan.cpp:339-379`), so the UI has to reveal the finished panel
explicitly on wake rather than relying on the end callback. No link is up on the
Scan page.

**Effort.** Small. One call in `sleepDisplay`, one explicit panel reveal on wake,
and a page check. The scan page already has the finished state and a restart
callback.

**Required test.** Host or simulator test: open the Scan page, force display
sleep, assert `Scan::isActive()` is false and the finished panel is shown on
wake; press Rescan, assert the scan starts again. On-device check that a found
camera row survives the stop.

**Citation.** `src/FurbleUI.cpp:1293-1318`, `:6720-6778`;
`lib/furble/Scan.cpp:339-379`; `src/FurbleSettings.cpp:572-575`.

### BLE-07 Default discovery scan timeout

**Change.** Change the `SCAN_TIMEOUT` default from 0 (Never) to 60 s.

**Current behavior.** Zero maps to `BLE_HS_FOREVER` in `NimBLEScan::start`
(`lib/furble/Scan.cpp:276-287`), so the page scans until it is left. The UI
roller already offers Never, 30, 60 and 120 s (`include/FurbleUI.h:655-656`),
and the scan end path already shows "Scan finished" with a Rescan button
(`src/FurbleUI.cpp:6720-6740`).

**Evidence.** Inferred, code reading.

**Estimated saving.** Inferred. It bounds a forgotten Scan page to 60 s of Full
duty instead of hours. This is the only scan bound that works regardless of the
inactivity and display settings, which makes it the highest confidence scan
change.

**Risk.** Low. The list remains and Rescan is one click. A user who expects a
continuously updating list must press Rescan. The default change needs a docs
update. Note that a changed default only seeds a fresh NVS: an existing device
keeps its stored 0 (Never) unless a migration sets it, so the reach is new
installs and updated devices that never stored the key.

**Effort.** Small, one default value. No UI change, because the roller already
has the 60 s entry.

**Required test.** Simulator scenario: open the scan page, advance 60 s of
virtual time, assert the finished panel and that Rescan starts a new scan.
Console test that `settings get scan_timeout` reports 60 on a fresh NVS.

**Citation.** `src/FurbleSettings.cpp:675-677`, `include/FurbleUI.h:655-656`,
`src/FurbleUI.cpp:9250-9270`, `lib/furble/Scan.cpp:276-287`.

### BLE-08 One targeted scan for all saved cameras

**Change.** Run one targeted scan that matches every saved camera, then connect
each camera in turn, instead of one targeted scan per camera.

**Current behavior.** `Control::connectAll()` loops cameras sequentially and
calls `camera->connect()` per camera (`src/FurbleControl.cpp:236-281`). Each
Fujifilm Secure or Nikon camera runs its own targeted scan. With N such saved
cameras and the cameras off, that is N times up to 60 s of Full-duty scanning.

**Evidence.** Inferred, code reading.

**Estimated saving.** Inferred. The scan time falls from about N times W to about
W plus the connects. For three saved Fujifilm Secure cameras that is roughly
three times less scan-on time in the worst case.

**Risk.** Medium. The scan callback is vendor owned; matching multiple identities
means a shared callback object or a fan-out list. Connect ordering and the
per-camera registration handshake still run one at a time. Mixed vendor lists
need a protocol-agnostic match.

**Effort.** Large. This is a design change in `Scan` and the vendor lifetime
model.

**Required test.** Host test with two virtual peers of different types, both
found in one scan, followed by two successful connects. Simulator multi-connect
scenario with the scan start count asserted to be one. On-device with the two
cameras available.

**Citation.** `src/FurbleControl.cpp:236-281`;
`lib/furble/FujifilmSecure.cpp:157-174`, `lib/furble/Nikon.cpp:82-100`.

### BLE-09 Duplicate filtering (no change)

**Change.** None recommended.

**Current behavior.** Both product scan paths enable the controller duplicate
filter. The discovery overload calls `setScanCallbacks(proxy)` with the default
`wantDuplicates = false`, which sets `filter_duplicates = 1`
(`lib/furble/Scan.cpp:285`,
`components/esp-nimble-cpp/src/NimBLEScan.cpp:471-473`). The targeted overload
also defaults to no duplicates (`lib/furble/Scan.h:81`). The controller cache is
100 entries, keyed by data and device address, and is cleared when scanning is
disabled (`CONFIG_BT_CTRL_BLE_SCAN_DUPL=y`,
`CONFIG_BT_CTRL_SCAN_DUPL_CACHE_SIZE=100`,
`CONFIG_BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD=0`, documented as "not cleared
until scanning is disabled").

**Evidence.** Inferred, code reading plus Kconfig help.

**Estimated saving.** None on the radio. Filtering reduces host wakeups and the
bounded handoff queue pressure, not receiver duty. The queue is capped at 32
(`lib/furble/Scan.h:44-45`), so filtering also protects against drops. Keep it.

**Risk.** None. Disabling filtering would increase host wakeups and queue drops
for no radio benefit.

**Effort.** None.

**Required test.** Existing scan generation and advertisement dispatch tests
already cover the handoff queue. No change needed.

**Citation.** `lib/furble/Scan.cpp:117-169`, `:285`, `:325`;
`lib/furble/Scan.h:44-45`, `:81`;
`components/esp-nimble-cpp/src/NimBLEScan.cpp:471-473`;
`sdkconfig.m5stick-s3` `BT_CTRL_BLE_SCAN_DUPL*`.

### BLE-10 Match first, then keep scanning (no change)

**Change.** None recommended.

**Current behavior.** The UI task drains pending advertisements and calls
`CameraList::match`, which de-duplicates by address and stops matching a camera
that is already in the list (`lib/furble/Scan.cpp:408-441`,
`lib/furble/CameraList.cpp:454-498`). The scan keeps running so the user can add
more cameras. Non-camera advertisers cost one queue slot and one match attempt,
not a callback.

**Evidence.** Inferred, code reading.

**Estimated saving.** None on the radio. Stopping the scan after the first match
would reduce radio time, but it would break multi-camera selection, which is a
product feature. The correct power lever for the idle page is BLE-04 or BLE-06,
not a match-first stop.

**Risk.** None as written. A match-first stop would be a functional regression.

**Effort.** None.

**Required test.** No change. Existing scan duplicate and distinct-row scenarios
cover the list behavior.

**Citation.** `lib/furble/Scan.cpp:408-441`; `lib/furble/CameraList.cpp:454-498`.

## Connection path

### BLE-11 `CONN_SAVER` default

**Change.** Turn the connection saver on by default, or keep it off and make the
Battery Saver profile the documented path that turns it on.

**Current behavior.** `CONN_SAVER` defaults to false (`src/FurbleSettings.cpp:651-676`,
the bool default group). With it off, `Camera::connect` uses the fast profile for
the whole session and `setConnProfile(FAST)` is a no-op (`lib/furble/Camera.cpp:500-502`).
With it on, the target task calls `maybeSetIdle()` on its 50 ms tick and the link
moves to the idle profile 10 s after the last shutter or focus activity
(`lib/furble/Camera.cpp:472-482`, `src/FurbleControl.cpp:159-168`).

**Evidence.** Inferred overall, built from a measured floor, modeled screen-off
states, and the code event rate. The connected floor is measured at 3.3 mA on
the S3 (`plans/00-hardware-experiments.md`,
`tools/power-model/board-currents.yaml` `ble_connected_idle_floor`, light sleep
from the main crystal, connection interval not stated). The screen-off states
are modeled: 43.6 mA with `SLEEP_CONN` off and 3.61 mA with it on (plan 98
what-if). The interval change itself is not visible to the current model, which
has no per-interval term and, today, no connected floor charge at all (BLE-32).

**Estimated saving.** Measured floor, modeled screen-off states, inferred event
share. The event rate falls from about 27 per second at a 37.5 ms average to
about 3.6 per second at 275 ms, a 7.6x reduction. The measured 3.3 mA includes a
fixed main crystal cost that the event rate does not remove, so the achievable
saving is some fraction of the event share. Plan 98 estimates the true floor
with the saver on at 0.5 to 2 mA, which is a hypothesis, not a measurement.

**Risk.** Medium. The first shutter after idle goes out at the idle interval; the
Fujifilm shutter is two write-with-response operations, so the worst case was
characterized at about 1.25 s at 300 ms. Peer renegotiation and per-vendor
behavior under modem sleep are only verified on Fujifilm.

**Effort.** Small for a default flip or for using the existing Battery Saver
override. The mechanism is already merged and hardware verified.

**Required test.** The shared measurement rows P06 to P09 and the per-family
K rows. A 60 minute soak per vendor with no disconnects. Shutter latency
distribution after a 30 s idle gap. `debug camera <n>` to confirm the live
interval, not just the requested one.

**Citation.** `lib/furble/Camera.h:469-502`, `lib/furble/Camera.cpp:472-596`;
`plans/10-adaptive-conn-params.md`; `plans/98-power-optimization-audit.md`;
`plans/00-hardware-experiments.md`.

### BLE-12 Raise the fast interval floor

**Change.** Raise the fast profile minimum from 24 units (30 ms) to 40 to 80
units (50 to 100 ms).

**Current behavior.** `m_FastMinInterval = BLE_GAP_INITIAL_CONN_ITVL_MIN` (24
units, 30 ms), `m_FastMaxInterval = BLE_GAP_INITIAL_CONN_ITVL_MAX` (40 units,
50 ms), latency 1, timeout 512 (5.12 s) (`lib/furble/Camera.h:469-472`, ESP-IDF
NimBLE `ble_gap.h:113-122`). The controller picks a value in the range. A
hardware log recorded 24-40 units.

**Evidence.** Inferred. Behavior from the code and the NimBLE macros; the energy
split is not measured.

**Estimated saving.** Inferred. Raising the floor to 100 ms cuts the event rate
from about 27 per second to about 10 per second, a 2.7x reduction, in the
default state where the saver is off. The saving in milliamps is bounded by the
event share of the 3.3 mA floor, which is unknown. At the modeled 43.6 mA
screen-off state the radio component is 3.5 mA, so even a total elimination of
the radio term is under 4 mA there. Do not expect the full 2.7x in current.

**Risk.** Medium. Connect and GATT discovery take longer at a longer interval.
Shutter latency grows by about one interval per write-with-response. Some
cameras may reject or behave worse. The coex guidance in plan 65 also wants a
floor above 50 ms while WiFi is up, so a 51.25 to 60 ms floor would satisfy both
goals, but that is a separate A/B.

**Effort.** Small (constants plus docs), but the soak is the work.

**Required test.** 60 minute soak per vendor. Shutter latency median and worst
case. `debug camera` interval readback. The shared P06 and K-family rows.

**Citation.** `lib/furble/Camera.h:469-472`, `:504-510`;
`components/bt/host/nimble/nimble/nimble/host/include/host/ble_gap.h:113-122`;
`plans/10-adaptive-conn-params.md` hardware verification (the 24-40 unit log);
`plans/65-bt-coexistence.md` recommendation 2.

### BLE-13 Idle interval above 300 ms

**Change.** Add a third, slower profile for screen-off or blind-remote use, for
example 500 to 1000 ms.

**Current behavior.** The idle profile is 200 to 240 units (250 to 300 ms),
latency 0, timeout 700 (7 s), requested 10 s after the last activity
(`lib/furble/Camera.h:479-502`).

**Evidence.** Inferred.

**Estimated saving.** Inferred. Moving from 275 ms to 1000 ms cuts the event rate
by another 3.6x, but the same fixed-floor caveat as BLE-11 and BLE-12 applies.
The first-press cost rises to one to two seconds per write, so this is a
user-visible trade.

**Risk.** Medium. First shutter after idle can take 2 to 4 s with two
write-with-response operations at a 1 s interval, using plan 10's two-interval
per write arithmetic. Geotag notifications are delayed by up to one interval.
Supervision margin at 7 s and 1 s is seven events, which is thinner than at
300 ms but still well above the four consecutive losses recorded in the coex
field data.

**Effort.** Medium. A new profile or a screen-state-selected profile, plus the
UI/console reporting and the idle timer.

**Required test.** First-press latency distribution at 500 ms and 1000 ms.
Geotag freshness against `MAX_AGE_MS`. 60 minute soak. The shared P07 to P09
rows.

**Citation.** `lib/furble/Camera.h:472-502`; `plans/10-adaptive-conn-params.md`
idle-profile latency arithmetic; `plans/65-bt-coexistence.md` event-loss data.

### BLE-14 Fast profile latency 1 (no change for power)

**Change.** None recommended for power. Setting latency to 0 is a latency
improvement, not a power saving.

**Current behavior.** The fast profile requests latency 1. The idle profile
requests 0.

**Evidence.** Inferred, from the BLE role definitions and plan 10's correction.

**Estimated saving.** None for furble. The central must be present at every
connection event; latency lets the peripheral skip. A non-zero latency only
adds worst-case write delay on furble's side.

**Risk.** None as written. Changing latencies is a behavior change for the
camera, not a furble energy lever.

**Effort.** None for power.

**Required test.** If the latency is changed for responsiveness, the existing
conn-saver host tests and a shutter latency A/B cover it.

**Citation.** `lib/furble/Camera.h:469-483`;
`plans/10-adaptive-conn-params.md` "peripheral latency lets the peripheral
skip".

### BLE-15 Supervision timeout (no change as a power lever)

**Change.** None recommended. Do not lengthen the timeout to save power, and do
not shorten it without a link soak.

**Current behavior.** Fast 512 units (5.12 s), idle 700 units (7 s). Peer
requests above 700 are rejected once Secure registration is complete
(`lib/furble/Camera.cpp:863-920`).

**Evidence.** Inferred, code reading.

**Estimated saving.** None as a duty lever. The timeout does not change the
connection event rate. A longer timeout only keeps a dead link reported as
connected for longer, which keeps the controller transmitting anchor packets to
a peer that is not there. A shorter timeout ends that state sooner, but the
saving is a handful of events and the risk is spurious drops.

**Risk.** High if changed for power. This value is the dead-link detector that
fixed the false-connected bug family.

**Effort.** None.

**Required test.** No change. The existing control disconnect and peer-cap tests
cover the behavior.

**Citation.** `lib/furble/Camera.h:484-502`; `lib/furble/Camera.cpp:889-911`;
`plans/10-adaptive-conn-params.md` peer timeout cap section.

### BLE-16 2M PHY

**Change.** Request the 2M PHY after connect on the S3, and only for cameras
that accept it.

**Current behavior.** No code calls `NimBLEClient::updatePhy` or `setPhy`
(`grep` over `src/`, `include/`, `lib/furble/` returns nothing). The link stays
on the 1M PHY. The S3 config enables the 2M feature
(`CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_2M_PHY=y`). The ESP32 boards have no BLE 5
features.

**Evidence.** Inferred, code reading plus the datasheet radio figures.

**Estimated saving.** Inferred and small. 2M halves the data PDU airtime, but an
empty connection event is dominated by the two anchor PDUs, the 150 us
inter-frame spacing and the event overhead, not by payload. Most furble writes
are a few bytes to 23 bytes, with one 51 byte DJI handshake frame per connect.
The event energy reduction is therefore a fraction of an already small share of
the total. The camera must also support 2M, which is unknown for every supported
model.

**Risk.** Medium. A PHY update procedure at connect, per-vendor support unknown,
and at long range 2M can retry more and cost more than it saves.

**Effort.** Small code change, large validation matrix.

**Required test.** `NimBLEClient::getPhy` readback on the X100VI and at least one
other vendor. Event-level current capture at 1M and 2M with the shutter quiet.
Shutter latency and range check. The shared K-family rows.

**Citation.** `components/esp-nimble-cpp/src/NimBLEClient.h:102-103`;
`sdkconfig.m5stick-s3` `BT_NIMBLE_LL_CFG_FEAT_LE_2M_PHY`;
`tools/power-model/board-currents.yaml` `esp32s3_radio`.

### BLE-17 Data length and ATT MTU (no change)

**Change.** None recommended.

**Current behavior.** The preferred ATT MTU is 256
(`CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256`). No code calls `setDataLen`. The
largest single write in the tree is the DJI Osmo connection request frame at 51
bytes, 12 header + 2 command + 33 payload + 4 tail
(`lib/furble/DJIOsmo.cpp:244-256`, `DJIOsmo.h:46-50`). The Fujifilm geotag is 23
bytes (`lib/furble/protocol/FujifilmProtocol.h:19`,
`FujifilmProtocol.cpp:156-173`).

**Evidence.** Inferred, code reading.

**Estimated saving.** Negligible. Data length extension changes how many
link-layer PDUs carry a write, not how many connection events occur or how often
the link wakes. The only writes above the 27 byte default link-layer payload are
the 23 byte geotag (30 bytes with the L2CAP header), sent at most once per 10 s,
and the 51 byte DJI handshake frame, sent once per connect. Whether the
controller negotiates a larger data length automatically for these peers was not
established here; either way the event rate is unchanged. A larger ATT MTU costs
RAM and is already 256.

**Risk.** None as written. Adding a data length procedure for its own sake adds a
compatibility surface for a one-time handshake.

**Effort.** None.

**Required test.** No change. A vendor protocol conformance test already covers
the payload sizes.

**Citation.** `lib/furble/DJIOsmo.cpp:244-256`, `lib/furble/DJIOsmo.h:46-50`;
`lib/furble/protocol/FujifilmProtocol.h:19`,
`FujifilmProtocol.cpp:156-173`; `sdkconfig.m5stick-s3`
`BT_NIMBLE_ATT_PREFERRED_MTU`.

### BLE-18 Peer override blocks the saver

**Change.** Set `m_PeerOverride` only when the accepted peer parameters are
faster than the idle profile. If the peer asks for values at or slower than the
idle range, accept them without blocking `maybeSetIdle()`.

**Current behavior.** `Camera::onConnParamsUpdateRequest` accepts any peer request
whose supervision timeout is within the cap and then always sets
`m_PeerOverride = true` (`lib/furble/Camera.cpp:884-919`). `maybeSetIdle()`
returns early while `m_PeerOverride` is set (`lib/furble/Camera.cpp:472-479`).
Only a shutter or focus press clears the override, or a confirmed Fujifilm
Secure fast profile (`Camera.cpp:414-424`). A camera that requests parameters at
connect and never presses a shutter can therefore keep the link at the fast
interval for the whole session, so the connection saver never engages.

**Evidence.** Inferred, code reading.

**Estimated saving.** Inferred and contingent. It does not save energy on its
own; it makes BLE-11 effective on peers that renegotiate at connect. If the
override is the reason the saver underperforms on some vendors, the saving is
the whole BLE-11 saving for those links.

**Risk.** Low to medium. The change must classify and accept, never counter
request, or it becomes a renegotiation loop. A peer that asks for a fast
interval within the idle range should still block the idle request, because
fighting it would be worse.

**Effort.** Small. One classification in the callback and a host test.

**Required test.** Host test with `mockPeerRequestConnParams`: a 250 to 300 ms
peer request must not block the idle request after the 10 s threshold; a 30 to
50 ms request must still block it. A no-request control case keeps the existing
`camera_regression_test` behavior.

**Citation.** `lib/furble/Camera.cpp:414-424`, `:472-482`, `:863-920`;
`lib/furble/Camera.h:578-592`;
`tests/host/camera_regression_test.cpp` conn saver transition.

### BLE-19 Connection-initiation scan window

**Change.** Pass explicit initiator scan parameters, for example 16 units window
and 32 units interval (50 percent), instead of the library default 16/16.

**Current behavior.** `Camera::connect` calls
`m_Client->setConnectionParams(min, max, latency, timeout)` with the four
argument overload (`lib/furble/Camera.cpp:274`). esp-nimble-cpp defaults the last
two arguments to 16 and 16, which is 10 ms window every 10 ms, 100 percent duty
(`components/esp-nimble-cpp/src/NimBLEClient.cpp:82-89`, `:542-558`).
`NimBLEClient::connect` passes the same struct to `ble_gap_connect`, and the
NimBLE `ble_gap_conn_params` struct documents those fields as the initiator scan
interval and window. The phase lasts until the peer is found, or up to the
connect timeout: 30 s by default, 5 s with infinite reconnect.

**Evidence.** Inferred, code reading of the library and the app call.

**Estimated saving.** Inferred. Halves the initiator receive duty. The absolute
saving depends on how long the initiator runs. For a camera that advertises, it
is a few advertising intervals. For a camera that advertises but refuses, or a
stale address, it can be the full connect timeout, and then this is a large
saving.

**Risk.** Low. Expected connection setup roughly doubles from one advertising
event to two, tens to hundreds of milliseconds. The connect timeout still
bounds the phase.

**Effort.** Small. One call change plus a mock extension, because the host
`NimBLEClient::setConnectionParams` currently takes four arguments and the mock
does not record scan parameters.

**Required test.** Host test that the client records 16/32 and that a virtual
peer connect still succeeds. Simulator connect scenario. On-device connect time
for the X100VI at 1 m, five trials, current code versus 50 percent.

**Citation.** `lib/furble/Camera.cpp:274`;
`components/esp-nimble-cpp/src/NimBLEClient.cpp:82-89`, `:182-198`, `:542-558`;
`components/bt/host/nimble/nimble/nimble/host/include/host/ble_gap.h:326-343`,
`:3205-3208`.

## Advertising, companion, and reconnect

### BLE-20 Companion pairing window

**Change.** Reduce `PAIRING_WINDOW_MS` from 2 minutes to 60 s. Plan 96 item A5b
already recommends 30 s, because SMP times out near 30 s and the dialog otherwise
outlives the procedure.

**Current behavior.** `CompanionGatt::startPairingWindow` advertises connectable
undirected at `setAdvertisingInterval(160)`, 100 ms, with scan response enabled
and the whitelist cleared, for the remaining window
(`src/FurbleCompanion.cpp:302-344`). `PAIRING_WINDOW_MS` is `2 * 60 * 1000`
(`include/FurbleCompanionService.h:58`).

**Evidence.** Inferred, code reading plus arithmetic.

**Estimated saving.** Inferred. The window transmits at about 10 advertising
events per second, 3 channels per event, roughly 1 to 1.5 ms per event, so about
1 to 1.5 percent duty. At the 93 mA receive/transmit ceiling that is on the
order of 1 mA average for the window. Halving the window halves the charge.
These numbers are order-of-magnitude only.

**Risk.** Low. The user must complete pairing within the window; the window is
user-triggered and can be re-armed. A 60 s value leaves margin over the SMP
timeout.

**Effort.** Small, one constant plus the companion docs.

**Required test.** Existing companion pairing tests updated for the new bound.
On-device pairing from Android and iOS within the window, with a deliberate
late attempt rejected.

**Citation.** `src/FurbleCompanion.cpp:302-344`;
`include/FurbleCompanionService.h:58`; `plans/96-wifi-bt-ble-hardening.md` A5b.

### BLE-21 Bound companion reconnect advertising

**Change.** Bound the reconnect advertising to a finite window (for example 10
minutes after enable or disconnect), or raise the interval from 1 s to 2 to 5 s.
Re-arm on a user action or a companion app attempt.

**Current behavior.** `startReconnectAdvertising` advertises at
`setAdvertisingInterval(1600)`, 1000 ms, with a connect whitelist, and calls
`m_Advertising->start(0)`, which is no timeout
(`src/FurbleCompanion.cpp:347-369`). It runs while the companion is enabled,
bonded, not connected, and the pairing window is closed, and is stopped only
when a companion connects, the companion is disabled, or a camera scan starts.

**Evidence.** Inferred, code reading plus arithmetic.

**Estimated saving.** Inferred. At 1 s interval and 3 channels the duty is about
0.1 to 0.15 percent, on the order of 0.1 to 0.15 mA average at the 93 mA
ceiling. That is small at any instant and continuous for days. On a 250 mAh cell
it is about 1 to 1.5 percent of the battery per day, given the same unmeasured
per-event charge.

**Risk.** Medium. A bounded window or a slower interval means the companion app
cannot find the device until the user re-arms. Neither Android nor iOS
reconnect guidance has been tested here.

**Effort.** Medium. A deadline and a re-arm path, or a slower interval plus a
companion reconnect latency test.

**Required test.** Companion reconnect latency with 1 s, 2 s and 5 s
advertising, on both phone platforms. Current A/B on the bench. The shared S35
and N09 rows.

**Citation.** `src/FurbleCompanion.cpp:347-369`, `:511-519`.

### BLE-22 Companion preferred connection parameters

**Change.** Advertise preferred connection parameters in the GAP service, or
send a peripheral connection parameter update request, so an idle companion link
uses a slower interval.

**Current behavior.** The GAP preferred parameters are all zero
(`CONFIG_BT_NIMBLE_SVC_GAP_PPCP_MIN_CONN_INTERVAL=0`,
`..._MAX_CONN_INTERVAL=0`, `..._SLAVE_LATENCY=0`,
`..._SUPERVISION_TMO=0`). Furble never requests a parameter update as a
peripheral. The phone is the central and chooses the interval.

**Evidence.** Inferred for the current behavior, from code reading; unknown for
the saving, because the phone's chosen interval was not read.

**Estimated saving.** Unknown. It depends on the phone's chosen interval, which
was not read. If the phone uses 30 ms for an idle companion link, a 300 ms
preferred interval would cut the event rate 10x, but the phone may ignore the
preference and the companion status freshness would suffer.

**Risk.** Medium. iOS and Android differ in how they honor preferences, and the
companion app expects prompt status.

**Effort.** Medium. Setting PPCP is small; validating both platforms is the
work.

**Required test.** Read the live interval on Android and iOS with the current
build, then with the preference. Companion status latency and reconnect
behavior. The shared S35 and N09 rows.

**Citation.** `sdkconfig.m5stick-s3` GAP PPCP block;
`src/FurbleCompanion.cpp` server setup.

### BLE-23 Idle disconnect

**Change.** Disconnect after N minutes without user activity while the display is
off, and reconnect on the next press or page entry.

**Current behavior.** The link stays up until the user disconnects, the camera
drops, or auto-off powers the device down. `AUTO_OFF` defaults to Never.

**Evidence.** Inferred, arithmetic on modeled states.

**Estimated saving.** Inferred. Connected idle with `SLEEP_CONN` on is 3.61 mA
modeled; disconnected screen-off is 0.25 to 0.51 mA modeled. The difference is
about 3.1 mA, which recovers a 0.1 to 0.2 mAh reconnect in roughly 2 to 4
minutes. With `SLEEP_CONN` off the connected state is 43.6 mA modeled and the
break-even is under 30 s. These are model numbers, not measurements, and the
reconnect charge is not measured.

**Risk.** Medium. The first press after the disconnect pays a targeted scan plus
connect plus GATT discovery, seconds of latency and a visible wake indicator.
A camera that dislikes reconnects, or an address rotation, makes it worse.

**Effort.** Large. A new policy, state, UI indicator, and the reconnect path on
input.

**Required test.** Bench idle-drain A/B for 30 minute windows, and a first-press
latency distribution. Simulator state test for the disconnect and reconnect on
input. The shared P08, P09 and K-family rows.

**Citation.** `src/FurbleControl.cpp` target lifetime;
`lib/furble/Camera.cpp:981-996`; `plans/98-power-optimization-audit.md` modeled
values; `tools/power-model/baseline/*.json`.

### BLE-24 Reconnect backoff default

**Change.** Default `RECON_BACKOFF` on, or force it through Battery Saver, and
document it.

**Current behavior.** `RECON_BACKOFF` defaults to false. With it off, each failed
attempt waits a flat 5 s (`ReconnectBackoff::delayMs`). With it on, the first
retry is 2.5 s and later retries follow 10, 20, 40, 80, 120, 120 s
(`include/FurbleReconnectBackoff.h`, `src/FurbleControl.cpp:318-360`).

**Evidence.** Inferred, code reading.

**Estimated saving.** Inferred. Backoff alone does not shorten a scan; it reduces
how many 60 s scans happen per hour. With the current 60 s scan, the attempts
per hour fall from about 55 with a flat 5 s wait to about 35 in the first ten
minutes, and to about 20 once the 120 s cap is reached. The saving multiplies
with BLE-01 and BLE-02, and is small compared with them.

**Risk.** Low. Slower recovery is the documented trade in plan 09. The
interactive cancel path is already bounded by the 100 ms slice.

**Effort.** Small, a default or the Battery Saver override already exists.

**Required test.** Existing `reconnect-backoff` host test. On-device recovery
time after the camera returns. A drain A/B for 60 minutes with the camera off.

**Citation.** `include/FurbleReconnectBackoff.h`;
`src/FurbleControl.cpp:318-360`; `plans/09-reconnect-backoff.md`.

### BLE-25 TX power below 3 dBm (no change for power)

**Change.** None recommended for power. Plan 11 owns the transmit-power work.

**Current behavior.** The TX power setting offers P3, P6 and P9, default P3
(3 dBm). The adaptive loop floors at P3. `Device::powerLevelToDbm` maps only
those three.

**Evidence.** Inferred, code reading plus the datasheet radio table.

**Estimated saving.** Very small for a central. Transmit happens only for the
writes furble sends, a few per press, while receive dominates the connected
event. The datasheet lists no S3 row below 0 dBm, so a negative level would be an
interpolation with no measurement behind it. Range margin, not idle current, is
what changes.

**Risk.** Medium. Lower TX reduces range and can increase retries, which can cost
more than it saves.

**Effort.** Small.

**Required test.** If pursued, a range and retry count A/B, not a current test
alone.

**Citation.** `lib/furble/Device.cpp:9-20`, `src/FurbleSettings.cpp:424-432`;
`tools/power-model/board-currents.yaml` `esp32s3_radio.ble_tx_*`;
`plans/11-adaptive-tx-power.md`.

## Connect and subscription cost

### BLE-26 Skip GATT discovery on reconnect

**Change.** Cache the camera GATT database across reconnects, or use NimBLE GATT
caching, so a reconnect does not repeat full service and characteristic
discovery.

**Current behavior.** The first `getService(uuid)` for a client calls
`retrieveServices`, which discovers the full attribute database and stores all
services and characteristics
(`components/esp-nimble-cpp/src/NimBLEClient.cpp:710-750`). Every
`Camera::connect()` creates a new client, so every connect rediscovers. The
Fujifilm Secure connect then subscribes to 12 CCCDs
(`lib/furble/FujifilmSecure.cpp:345-405`). `CONFIG_BT_NIMBLE_GATT_CACHING` is not
set.

**Evidence.** Inferred, code reading. The energy of a connect has not been
measured.

**Estimated saving.** Inferred, potentially the largest part of a connect. A
camera with a large attribute database takes many ATT round trips to discover at
30 to 50 ms intervals. Removing the discovery would shorten every reconnect. The
shared measurement plan expects a handshake of 2 to 40 s and 0.5 to 8 J by
family (K-FB-01 to K-DJ-01), which is a hypothesis band, not a measurement.

**Risk.** High. Attribute handles can change after a camera firmware update. The
GATT service-changed characteristic is not implemented by these cameras as far
as this tree knows. esp-nimble-cpp 2.5 does not expose a cache API, so this
needs library work.

**Effort.** Large.

**Required test.** Per-family handshake capture with `bt journal` and a sniffer
to separate discovery from subscription and registration. A firmware-update
test proving the cache is invalidated or verified.

**Citation.** `components/esp-nimble-cpp/src/NimBLEClient.cpp:710-750`;
`sdkconfig.m5stick-s3` `BT_NIMBLE_GATT_CACHING` unset;
`lib/furble/FujifilmSecure.cpp:345-405`.

### BLE-27 CCCD subscription count (no change)

**Change.** None recommended.

**Current behavior.** Fujifilm Secure subscribes to 12 characteristics, 2 of them
acknowledged; Fujifilm Basic to 5; the other vendors fewer. The CCCD write is one
ATT transaction per subscription.

**Evidence.** Inferred, code reading plus arithmetic.

**Estimated saving.** Negligible. Twelve writes at a 30 to 50 ms interval is
about 12 to 24 connection events, a few milliseconds of radio airtime. The
connect cost is dominated by discovery and the vendor handshake, not the
subscriptions.

**Risk.** None. Trimming optional subscriptions could break a feature (battery
status, geotag sync) for a few milliamps of connect energy.

**Effort.** None.

**Required test.** No change. The vendor conformance tests cover the required
subscriptions.

**Citation.** `lib/furble/FujifilmSecure.cpp:345-405`,
`lib/furble/FujifilmBasic.cpp:120-155`, `lib/furble/Camera.cpp:793-838`.

### BLE-28 Geotag notification cadence (no change)

**Change.** None recommended.

**Current behavior.** Fujifilm Secure subscribes to the geotag notification and
writes the sync interval as 10 s (`lib/furble/Fujifilm.h:75`,
`lib/furble/FujifilmSecure.cpp:410-420`). Each camera request produces a 23 byte
write (`lib/furble/Fujifilm.cpp:200-231`). That is 0.2 transactions per second,
trivial next to the connection events.

One documentation conflict to resolve before designing a fix: plan 10's 2026-08-17
hardware note says active geotagging keeps the link permanently fast because
"every geotag write counts as activity". The current code does not call
`noteConnActivity` on the GPS path; `CMD_GPS_UPDATE` only calls
`updateGeoData` (`src/FurbleControl.cpp:153-156`). The likely mechanism is a
peer parameter override or the target task staying busy, not an activity
timestamp. Re-verify before changing the geotag path for power.

**Evidence.** Inferred, code reading. The behavior claim in plan 10 is a prior
hardware observation whose mechanism is not present in the current code.

**Estimated saving.** Negligible for the notification itself.

**Risk.** None as written.

**Effort.** None for power. The documentation conflict is a small correction.

**Required test.** A `debug camera` trace with GPS on and off, 10 minutes each,
recording the live profile and interval.

**Citation.** `lib/furble/Fujifilm.h:75`, `lib/furble/Fujifilm.cpp:200-231`,
`lib/furble/FujifilmSecure.cpp:410-420`, `src/FurbleControl.cpp:153-168`.

## Configuration and model

### BLE-29 NimBLE buffers and tasks (no change for power)

**Change.** None recommended for power. These are RAM and scheduling settings.

**Current behavior.** On the S3:

| Setting | Value | Power effect |
| --- | --- | --- |
| `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE` | 16384 | RAM only; the host task blocks on its event queue |
| `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` | 9 | RAM and controller slots, see BLE-30 |
| `CONFIG_BT_NIMBLE_MAX_CCCDS` | 16 | RAM per connection |
| `CONFIG_BT_NIMBLE_MAX_BONDS` | 15 | NVS |
| `CONFIG_BT_NIMBLE_WHITELIST_SIZE` | 12 | RAM |
| `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` | 256 | RAM |
| `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT/SIZE` | 12 x 256 | 3 KB internal RAM, always allocated |
| `CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT/SIZE` | 24 x 320 | 7.5 KB internal RAM, always allocated |
| `CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT` | 24 | Controller RAM |
| `CONFIG_BT_NIMBLE_GATT_MAX_PROCS` | 4 | RAM |
| `CONFIG_BT_NIMBLE_HS_STOP_TIMEOUT_MS` | 2000 | Teardown bound, not duty |
| `CONFIG_BT_NIMBLE_RPA_TIMEOUT` | 900 | Address rotation, not duty |
| `CONFIG_BT_NIMBLE_PROX_SERVICE` and the other bundled services | on | Flash and RAM, no runtime radio |

**Evidence.** Inferred, from the sdkconfig and the component Kconfig.

**Estimated saving.** None. A blocked task and an allocated buffer do not draw
current. The only configuration in this block that affects radio energy is the
modem sleep and low power clock block, which is already correct on every board
(BLE-31).

**Risk.** None as written. Reducing buffers to save RAM could increase dropped
ACL data and retransmissions, which would cost radio energy.

**Effort.** None.

**Required test.** No change. Heap and client-pool tests already cover the
allocation side.

**Citation.** `sdkconfig.m5stick-s3` NimBLE Options and Controller Options;
`sdkconfig.m5stick-c` BTDM block;
`components/esp-nimble-cpp/src/NimBLEClient.cpp:82-89`.

### BLE-30 9 connections vs 6 controller activities (not power)

**Change.** None for power. Fix separately as a capability bug.

**Current behavior.** The S3 config allows 9 host connections
(`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=9`) but only 6 controller activities
(`CONFIG_BT_CTRL_BLE_MAX_ACT=6`). The ESP-IDF Kconfig help says an activity
covers "both of connections, scan, sync and adv". Scanning or advertising
consumes one. A scan plus companion advertising leaves four activities for
connections, not nine.

**Evidence.** Inferred, from the sdkconfig and the Kconfig help.

**Estimated saving.** None. This is a correctness and expectation gap, not an
energy lever. It is listed here because the lane must cover the NimBLE
configuration, and because a multi-connect power measurement would otherwise be
run against a configuration the controller cannot hold.

**Risk.** None to change. Leaving it means the UI may promise more targets than
the controller can sustain.

**Effort.** Small (align the two values or document the real ceiling).

**Required test.** A multi-connect scenario that connects six cameras plus a
scan and asserts the documented outcome.

**Citation.** `sdkconfig.m5stick-s3` lines for `BT_NIMBLE_MAX_CONNECTIONS` and
`BT_CTRL_BLE_MAX_ACT`; ESP-IDF
`components/bt/controller/esp32c3/Kconfig.in` `BT_CTRL_BLE_MAX_ACT`.

### BLE-31 Controller low power config (already correct)

**Change.** None recommended.

**Current behavior.** The S3 runs controller modem sleep mode 1 with the main
crystal as the low power clock and MAC/baseband power down
(`CONFIG_BT_CTRL_MODEM_SLEEP=y`, `..._MODE_1=y`, `..._LPCLK_SEL_MAIN_XTAL=y`,
`CONFIG_ESP_PHY_MAC_BB_PD=y`). The four ESP32 boards run BTDM modem sleep in ORIG
mode with the main crystal. The S3 connection event length method is ORIGINAL,
which ends an event when there is no more data. LE ping is enabled on both
controller families. Advertising report flow control is enabled.

**Evidence.** Inferred, from the sdkconfig and the Kconfig help; the connected
floor is recorded at 3.3 mA on the S3, under conditions that plan 98 says are
not trace-equivalent.

**Estimated saving.** None left in this block. The 32 kHz low power clock mode,
which would allow the main crystal to power down, is impossible on the StickS3
because the board has no 32.768 kHz crystal, proven by the BLE init log
(`plans/00-hardware-experiments.md`). The ESP32 boards have the same absence.

**Risk.** Changing any of these without a soak risks drops. Do not disable LE
ping or the event length method for a theoretical gain.

**Effort.** None.

**Required test.** No change. The existing hardware soaks cover the current
configuration.

**Citation.** `sdkconfig.m5stick-s3` MODEM SLEEP and Controller Options blocks;
`sdkconfig.m5stick-c` BTDM block;
`components/bt/controller/esp32c3/Kconfig.in`;
`plans/07-ble-sleep.md`; `plans/00-hardware-experiments.md`.

### BLE-32 The BLE radio is absent from the power model

**Change.** Add a scan state, an advertising state, a restored connected floor,
and a per-event connection term to the simulator model, and add a scan scenario
to the power-gate suite.

**Current behavior.** The model charges radio energy in exactly one place:
`radio_ma = (radio_connected_ms * connected_idle + radio_event_count * radio_tx * 2.0) / duration`
(`sim/power_profiler.cpp:1242-1244`; the `2.0` charges 2 ms at the transmit
current per event). `radio_connected` is set only by
`profilerSetRadioConnected` (`sim/power_profiler.cpp:1730-1736`), and a grep over
`sim/`, `src/`, `lib/`, `include/` and `tests/` finds no caller. The only radio
events are shutter, focus and GPS command events from the virtual peer
(`sim/BleSim.cpp:392-407`). The virtual radio advertises every 100 ms and is not
charged (`sim/BleSim.cpp:26`, `:154-189`). There is no scan state in the model
and no scan scenario in the power-gate suite
(`sim/scenarios/manifest.json`, suite `power-gate`).

The checked-in `tools/power-model/baseline/connected-idle-30s.json` still
reports `radio: 3.2989` and a total of 84.527813. `menu-idle-30s.json` reports
81.228912, which is the same total minus that radio term. The delta is 3.9
percent, inside `compare.py`'s default two-sided 10 percent band, so the gate
cannot see the term disappear.

**Evidence.** Inferred, from the code and the checked-in baseline arithmetic. No
simulator run was made for this file.

**Estimated saving.** Not a device saving. This is the evidence gap that makes
every BLE row above unrankable in milliamps. Fixing it is a prerequisite for
using the model to choose among BLE changes.

**Risk.** Low. The model change is additive and the scenario is new. The risk is
in re-baselining: the existing connected baseline must be regenerated with the
floor restored, in the same PR, with the reason stated.

**Effort.** Medium. A scan state in `sim/power_profiler.cpp` with window,
interval and a receive current; a connected floor wired to the production link
state; an advertising term for the companion; a scan scenario; a regenerated
baseline.

**Required test.** `tests/test_power_compare.py` for the comparator. A new
power-gate scenario that scans for 30 s at Full and asserts the report contains a
non-zero scan component. A negative test: a scenario with the BLE link up but the
radio flag unset must fail validation, not silently report zero.

**Citation.** `sim/power_profiler.cpp:70-84`, `:1242-1244`, `:1730-1736`;
`sim/BleSim.cpp:26`, `:154-189`, `:392-407`;
`tools/power-model/baseline/connected-idle-30s.json`;
`tools/power-model/compare.py:98-105`;
`.github/workflows/power-gate.yml`.

## Covered with no recommended change

These topics were in scope and are recorded so a later reader does not reopen
them without cause.

| Topic | Finding | Why no change |
| --- | --- | --- |
| Active scan backoff | Already enabled; `BT_CTRL_SCAN_BACKOFF_UPPERLIMITMAX` is not set, which the Kconfig defines as "disable active scan backoff" being off. | Disabling it would increase scan request collisions and transmit energy. |
| Duplicate filter | On in both product paths, controller cache 100, data plus address, not refreshed until scanning stops. | Saves host wakeups, not radio. Keep. |
| Match-first | `CameraList::match` de-duplicates by address; the scan continues for other cameras. | A stop-on-first-match would break multi-camera selection for no radio benefit beyond BLE-04/06. |
| Fast latency 1 | Central does not skip events; latency only delays writes. | Not a furble power lever. |
| Supervision timeout | 5.12 s fast, 7 s idle, peer cap enforced. | Not a duty parameter; changing it risks the false-connected class. |
| Data length and MTU | Largest write is a 51 byte DJI handshake frame; the Fujifilm geotag is 23 bytes. | DLE changes PDUs per write, not the connection event rate. |
| CCCD subscriptions | One ATT write each; 12 on Fujifilm Secure. | Negligible connect energy versus discovery. |
| Geotag cadence | 10 s camera request, 23 byte write, 0.2 transactions per second. | Negligible; the plan 10 mechanism claim needs a doc correction, not a code change. |
| Coded PHY | `CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_CODED_PHY=y` but `CONFIG_BT_NIMBLE_EXT_ADV` is off, so the scanner uses the legacy `ble_gap_disc` path and never scans coded. | No runtime cost, no change. |
| Extended scanning | `EXT_SCAN=y` but `EXT_ADV=n`, so `MYNEWT_VAL(BLE_EXT_ADV)` is 0. | The legacy path is used; no coded or extended scan windows are scheduled. |
| LE ping | Enabled; the controller runs the authenticated payload timer. | One ping per authenticated payload timeout is negligible; disabling risks compatibility. |
| Event length method | ORIGINAL ends events when there is no data. | Already the low-power choice. |
| Bundled GATT services | PROX, ANS, CTS, HTP, IPSS, TPS, IAS, LLS, SPS, HR, BAS, DIS are compiled in. | Flash and RAM only; no runtime radio. |
| Advertising report flow control | Enabled, 100 reports, discard threshold 20. | Controller buffering, not radio duty. |
| Host based privacy | On the four ESP32 boards, off on the S3 (controller based). | Address handling, not duty; the S3 resolves without a host wakeup. |
| Connection count 9 vs activities 6 | S3 mismatch. | Capability bug, listed as BLE-30; not an energy lever. |
| TX power below 3 dBm | Not available below P3. | Receive dominates; range risk. |
| RPA rotation | 900 s timeout, controller based on S3. | Does not change link duty. It affects reconnect-by-address, which is a correctness matter for plan 96 A7. |

## Ranked safe now

Ranked by expected saving times confidence, divided by risk. Each item is
implementable without a new measurement campaign. Where a bench smoke test is
listed it is a functional or latency check, not a power measurement, and the
required test remains a merge gate. A changed setting default only seeds a
fresh NVS; existing devices keep the stored value unless a migration or the
Battery Saver override is used.

1. **BLE-07, default discovery scan timeout 60 s.** Works regardless of the
   user's display and inactivity settings, and the UI already has the finished
   state and Rescan. One default value, no radio behavior change while the user
   is present. Doc surfaces: `docs/settings-and-controls.md`,
   `docs/wiki/Settings-Reference.md`, the README settings overview.
2. **BLE-06, stop the scan when the panel sleeps.** Large saving when it
   triggers, no link to break, and the list survives. Needs the inactivity
   timeout or Battery Saver to trigger, so it complements BLE-07 rather than
   replacing it. No user-facing setting, so only a wording change if the finished
   state is described.
3. **BLE-19, initiation scan at 50 percent.** Every connect pays the 100 percent
   initiator today. The expected cost is a small increase in setup time, bounded
   by the existing connect timeout. No user-facing setting. Required test and a
   bench connect-time smoke test.
4. **BLE-20, companion pairing window 60 s.** User-triggered, bounded, and the
   plan already recommends shortening it. Doc surface: `docs/companion.md`.
5. **BLE-18, peer override must not block the saver.** Only affects the opt-in
   `CONN_SAVER` path, so the default install is unchanged. It makes BLE-11
   effective on peers that renegotiate at connect. Host test only; no hardware
   risk when the saver is off. Doc surface: `docs/settings-and-controls.md` for
   the saver description.
6. **BLE-24, default reconnect backoff on.** Small saving, low technical risk,
   and the curve is already pinned by a host test. The remaining question is the
   policy trade (slower recovery) rather than safety. It multiplies with BLE-01
   and BLE-02 but is not a substitute for them. Doc surface:
   `docs/settings-and-controls.md` for the reconnect options.

Prerequisite, not a device change: **BLE-32** restores the BLE radio term in the
model. Land it before ranking any of the rows above in milliamps, otherwise the
gate cannot see a scan or connected-floor regression.

Deliberately not in this list: BLE-01, BLE-02, BLE-04, BLE-05 and BLE-11. Their
savings are large but they need the camera advertising interval or a connected
floor measurement first.

## Needs measurement

Ordered by expected value per bench hour. Each row names the measurement from
`docs/power/measurement.md`; the BLE sub-states extend S06, S07 and S35.

1. **Camera advertising interval, per vendor.** No power change is safe without
   it. Use `bt scan 60 duplicates` with the camera in standby and one in
   reconnect state, and log the inter-arrival times. This gates BLE-01, BLE-02,
   BLE-03, BLE-04 and BLE-05.
2. **Targeted reconnect scan current, camera off, 10 minutes.** S06f. Accept if
   the scan phase is separable from the wait phase and the 100 percent duty
   plateau matches the receive arithmetic. Gates BLE-01 and BLE-02.
3. **Connected idle versus interval, P06, P07, P08, P09.** Three runs each. Fit
   current against 1/interval to get the per-event charge and the fixed floor.
   This decides whether BLE-11, BLE-12 and BLE-13 are worth any default change.
4. **Connection initiation current, camera advertising and camera unresponsive.**
   S07 and a new S07b. Separate the initiator phase from the camera handshake.
   Gates BLE-19.
5. **Passive versus active targeted scan.** S06g. Capture one vendor with the
   sniffer first, then measure current at Full active and Full passive. Gates
   BLE-03.
6. **Companion advertising, pairing and reconnect.** S35a, S35b, S35c. Three
   runs. Gates BLE-20, BLE-21 and BLE-22.
7. **Per-family handshake and discovery.** K-FB-01 to K-DJ-01. Use `bt journal`
   and the sniffer to split discovery, subscription, registration, and
   identification. Gates BLE-26 and validates the connect-cost model.
8. **Idle disconnect versus stay connected.** S10, S11, S23 plus a reconnect
   event. Gates BLE-23.
9. **2M PHY event current.** S08 at 1M and 2M with the shutter quiet. Gates
   BLE-16.
10. **Negative TX levels.** Not worth a campaign of its own; fold into the
    existing plan 11 range test if it is ever run.

## Rejected

| ID or topic | Reason |
| --- | --- |
| BLE-09 duplicate filter change | Filtering reduces host wakeups, not radio duty. Keep it. |
| BLE-10 match-first stop | Breaks multi-camera selection. Use BLE-04 or BLE-06 for the idle page instead. |
| BLE-14 latency 1 as a power lever | The central still receives at every event. Latency only delays writes. |
| BLE-15 supervision timeout as a power lever | Not a duty parameter. Shortening it risks spurious drops and reintroduces the false-connected family. |
| BLE-17 data length or larger MTU | The writes above the 27 byte default are the 51 byte DJI handshake and the geotag's 30 byte L2CAP PDU. DLE changes PDUs per write, not the event rate; the ATT MTU is already 256. |
| BLE-25 negative TX levels for power | Receive dominates, the datasheet has no sub-0 dBm row, and lower TX risks retries and range. |
| BLE-27 trimming subscriptions | A few milliseconds of connect airtime against a feature risk. |
| BLE-28 changing the geotag cadence | 0.2 transactions per second. The real issue is the unexplained plan 10 claim, which is a documentation fix. |
| BLE-29 buffer and task tuning for power | RAM and scheduling only. |
| BLE-30 connection-count mismatch for power | Capability bug, not an energy lever; fix separately. |
| BLE-31 controller low power config | Already correct on all boards. The 32 kHz mode is impossible without the crystal. |
| Active scan backoff disable | Would increase scan request collisions and transmit energy. |
| Coded PHY removal | Already never scanned, because extended advertising is compiled out. |
| LE ping disable | Compatibility risk for a negligible ping rate. |
| Event length method change | ORIGINAL is already the early-termination choice. |
| Bundled GATT service removal for power | Flash and RAM only. |
| Advertising report flow control change | Not a radio duty lever. |
| Scan window equal to scan interval under coex | Plan 65 workaround for hub mode. For BLE power it is the opposite of what we want, and hub mode is a separate lane. |

## BLE measurement plan

The shared method, boundaries, instruments and promotion rules are in
`docs/power/measurement.md`. This section only adds what is BLE specific. Use
boundary B1 (battery positive) for every claim. Never mix a B4 USB measurement
into a battery number.

### Instruments

- I1 (source and measure, at least 100 kSa/s) for the scan plateaus, the
  connected floors, and the per-event fits.
- I2 (logger, at least 10 kSa/s) for the 30 minute soaks.
- I4 (oscilloscope, at least 1 MSa/s) for connection event shape, scan window
  edges, and advertising bursts. BLE events are 1 to 3 ms, so 100 kSa/s is the
  minimum for a shape and 1 MSa/s for the edges.
- I8 (BLE sniffer) for the negotiated interval, PHY, payload length, retries,
  and the advertising interval. Without I8, an interval claim is a readback from
  the device, not an air observation.
- I9 (state observer) for screen state and camera indicator. Phone radios off
  during the run.

### BLE sub-states

These extend the shared S06, S07 and S35 rows. Add them to the shared register
when this lane is consolidated.

| ID | State | Entry condition | Verification | Window |
| --- | --- | --- | --- | --- |
| S06a | Scan Full, no advertiser | Baseline block, `scan_mode 0`, `scan_timeout 0`, scan page, camera off and out of range | `scan` console active; no rows | 60 s |
| S06b | Scan Full, one camera | Same with the camera advertising at 1 m | One row; duplicate filter active | 60 s |
| S06c | Scan Balanced | `scan_mode 1` | `scan` active; 25 percent preset | 60 s |
| S06d | Scan Low | `scan_mode 2` | `scan` active; 5 percent preset | 60 s |
| S06e | Scan page, panel off | S06b, `inactivity 1`, wait for panel off | Blank panel; scan still active on the current code | 300 s |
| S06f | Targeted reconnect, camera off | Saved Fujifilm Secure, camera off, infinite reconnect on, backoff off | Log shows the 60 s scan then the 5 s wait, repeating | 600 s |
| S06g | Targeted scan, passive and active | Debug build with a scan-phy/passive switch, or a temporary build | Sniffer confirms no scan requests in passive | 60 s |
| S07a | Connect initiation, camera advertising | Saved camera, camera on, from idle | Sniffer shows the initiator then the connection | 1 event, 20 repeats |
| S07b | Connect initiation, camera unresponsive | Camera advertises but refuses, or a stale address | Initiator runs to the connect timeout | 1 event, 10 repeats |
| S35a | Companion pairing window | `companion on`, pairing window open, phone away | Advertising at 100 ms; window closes | 120 s |
| S35b | Companion reconnect advertising | Bonded companion, phone away, companion on | Advertising at 1000 ms, no timeout | 600 s |
| S35c | Companion connected idle | `companion on`, phone connected, no camera | Companion status connected; interval read from the phone side if possible | 600 s |

### Procedure

1. Apply the baseline settings block from `docs/power/measurement.md` section
   7.1 with the debug build, verify with `settings list`, then flash the release
   build without erasing NVS. The console distorts the measurement.
2. Set the battery to a recorded voltage, no USB, and record the cell voltage
   before and after each run.
3. Fix the display at brightness 128, the camera at 1 m, and the phone radios
   off. Record the room, the WiFi state, and the number of foreign advertisers
   from a `bt scan` snapshot.
4. For each state, capture 60 s at I1. For connection event shapes, capture 5 s
   at 1 MSa/s. For soaks, use I2 for 30 minutes.
5. Run each state three times, on two different days for the reconnect and
   companion states. Report the median and the range, not a single value.
6. For the per-event fit, read the live interval with `debug camera <n>` and
   correlate it with the event period in the I4 trace. If they disagree, the
   readback is not the air truth and the run is invalid.
7. For the scan duty, check that the current plateau edges line up with the
   30/120 or 50/1000 ms pattern. A plateau that does not match the requested
   window is a controller or build mismatch, and the run is invalid.
8. For the passive scan, take one sniffer capture first. If the match fields are
   only in the scan response, stop: the change is not viable, regardless of the
   current.

### What each measurement decides

| Measurement | Decision it unlocks |
| --- | --- |
| Camera advertising interval | The lowest safe scan duty and the shortest safe scan duration |
| Full scan current plateau | The receive ceiling for the S3 model term |
| Connected current versus interval | The per-event charge and the fixed floor, which decide BLE-11, BLE-12 and BLE-13 |
| Initiator current with and without a peer | BLE-19 |
| Passive versus active targeted scan | BLE-03 |
| Companion advertising current | BLE-20, BLE-21, BLE-22 |
| Handshake breakdown | BLE-26 and the connect-cost model |
| Reconnect energy and latency | BLE-23 |

### Acceptance and falsification

- Scan duty: a 25 percent preset must draw about a quarter of the Full plateau
  during the window, within the uncertainty of the meter and the display. If it
  does not, the controller is not honoring the window, or another term dominates,
  and the duty model is wrong.
- Connected interval: if the current does not change measurably between 30 ms
  and 300 ms intervals, the event share is below the noise floor and BLE-11,
  BLE-12 and BLE-13 are not worth a default change.
- Advertising: if the measured advertising charge is under the noise floor, do
  not spend effort on BLE-20 or BLE-21.
- Passive scan: if the match fields are in the scan response, BLE-03 is dead.
- Any run where the sniffer and the device readback disagree on the interval is
  invalid.

### Model and CI changes to land with the measurements

1. Restore the connected floor: wire `profilerSetRadioConnected` to the
   production link state, or derive it from the active target count. Add a
   validation that a connected scenario with a zero radio floor fails closed.
2. Add a scan state to `sim/power_profiler.cpp`: window, interval, duty and a
   receive current. Charge it only while a scan is active.
3. Add an advertising term for the companion: interval, channels, duration.
4. Add a per-event connection term derived from the measured current versus
   interval slope, not from a constant.
5. Add a scan scenario to the `power-gate` suite and regenerate the connected
   baseline in the same PR, with the reason in the PR body.
6. Tighten the gate or add component-level guards so that a 3.9 percent radio
   regression cannot hide inside the 10 percent band. The comparator already
   supports a threshold argument; the component check is new.
7. Keep the simulator's virtual advertiser interval (100 ms) marked as a model
   assumption, not a camera fact (`sim/BleSim.cpp:26`).

## Per-board notes

| Board | Radio | Connected sleep | BLE notes |
| --- | --- | --- | --- |
| M5StickS3 | ESP32-S3, 2M PHY capable, BLE 5 | 3.3 mA measured, light sleep from the main crystal | Best BLE power target. No 32 kHz crystal, so the 0.23 mA mode is impossible. Controller based privacy. 6 controller activities, 9 host connections. |
| M5StickC, StickC Plus | ESP32, BLE 4.2, no 2M | 14.1 mA published modem sleep, no light sleep without a 32 kHz crystal | AXP192 floor about 2 mA. BLE-11/12/13 still reduce event count, but the floor is high. The 93 mA receive figure does not apply; the ESP32 receive figure is 95 to 100 mA. |
| M5Stack Core | ESP32, BLE 4.2 | Same as the Sticks | IP5306 hard powers off after 32 s below 45 mA. Any BLE low-power posture is not survivable as shipped; do not plan a BLE sleep state for this board. |
| M5Stack Core2 | ESP32, BLE 4.2 | Same as the Sticks | AXP192 floor and touch controller left running. BLE changes apply but the board floor dominates. |
| ESP32-S3 headless, Waveshare S3 ETH | ESP32-S3 | Mains | Not a battery target. Use as a control only, and mark it a developer result. |

## Plan claims checked against the code

This repository requires doc claims to be verified against the code. These were
the material BLE claims in the plans read for this lane.

| Claim | Source | Code in this tree | Verdict |
| --- | --- | --- | --- |
| The power gate is not wired into CI | plan 98 | `.github/workflows/power-gate.yml` exists and runs the power-gate suite | Outdated |
| The model charges a constant connected floor | plan 98 | `profilerSetRadioConnected` has no caller since `9081fc94`; the only radio charge is the five command events | Outdated in the other direction; the floor is now zero |
| The model has no scan, advertising or reconnect state | plan 98 | Confirmed: no scan state, no advertising charge, virtual advertiser at 100 ms | Still true |
| Idle connection profile is 250 to 300 ms, latency 0 | plan 10 | `m_IdleMinInterval = 200`, `m_IdleMaxInterval = 240`, `m_IdleLatency = 0` | True |
| Idle supervision timeout is 16000 ms | plan 10 | `m_IdleTimeout = 700`, which is 7 s | Outdated; the cap was retuned |
| Fast profile is 30 to 50 ms, latency 1, 5120 ms | plan 10 | `BLE_GAP_INITIAL_CONN_ITVL_MIN/MAX`, latency 1, timeout 512 | True |
| Pairing window is 2 minutes | plan 96 A5b | `PAIRING_WINDOW_MS = 2 * 60 * 1000` | True |
| Scan is always active mode | plan 98 | `setActiveScan(true)` once, never changed | True |
| Default scan mode is Full | plan 08, plan 98 | `SCAN_MODE` default 0, `Mode::FULL` | True |
| Scan timeout default is no timeout | plan 08 | `SCAN_TIMEOUT` default 0, mapped to `BLE_HS_FOREVER` | True |
| Reconnect scans are forced to Full | plan 08 | `applyMode(Mode::FULL)` in the targeted overload | True |
| S3 modem sleep mode 1 on the main crystal | plan 07 | `BT_CTRL_MODEM_SLEEP_MODE_1`, `LPCLK_SEL_MAIN_XTAL`, `ESP_PHY_MAC_BB_PD` | True |
| The connected floor is 3.3 mA on the S3 | plan 00 | `board-currents.yaml` measured-local entry, plans/00 record | True as recorded, interval not stated |

## Risks to this analysis

- The camera advertising interval is the single largest unknown. Every scan duty
  recommendation is conditional on it.
- The 93 mA figure is the datasheet BLE receive peak for the whole chip at 100
  percent receive duty. It is not a measured scan current, and the real
  controller may not hold the receiver on for a full window.
- The 3.3 mA floor was measured at an unstated connection interval. It cannot be
  used to rank interval changes.
- The checked-in connected baseline predates the production connection stack.
  Quoting its 3.2989 mA radio term as a current model output would be wrong.
- The plans disagree on the geotag and connection-saver interaction. The code
  path does not match the published explanation. Re-verify before optimizing it.
- Only Fujifilm hardware is available. Nikon shares the targeted scan path, and
  the other vendors are code review only.
- Multi-connect power was not measured. The S3 controller activity limit means
  the advertised 9-connection ceiling is not reachable with a scan or
  advertising active.

## Sources

Code and configuration:

- `lib/furble/Scan.cpp`, `lib/furble/Scan.h`
- `lib/furble/Camera.cpp`, `lib/furble/Camera.h`, `lib/furble/Device.cpp`
- `lib/furble/CameraList.cpp`, `lib/furble/protocol/FujifilmProtocol.{h,cpp}`,
  `lib/furble/protocol/AdvertisementProtocol.cpp`
- `lib/furble/Fujifilm.cpp`, `lib/furble/FujifilmSecure.cpp`,
  `lib/furble/FujifilmBasic.cpp`, `lib/furble/Nikon.cpp`
- `src/FurbleControl.cpp`, `src/FurbleCompanion.cpp`, `src/FurbleUI.cpp`,
  `src/FurbleSettings.cpp`, `src/FurbleConsole.cpp`, `src/FurbleBtDebug.cpp`
- `include/FurbleCompanionService.h`, `include/FurbleReconnectBackoff.h`,
  `include/FurbleUI.h`, `include/FurbleSettings.h`
- `components/esp-nimble-cpp/src/NimBLEScan.{h,cpp}`,
  `components/esp-nimble-cpp/src/NimBLEClient.{h,cpp}` (version 2.5.0)
- `sdkconfig.m5stick-s3`, `sdkconfig.m5stick-c`,
  `sdkconfig.m5stick-c-plus`, `sdkconfig.m5stack-core`,
  `sdkconfig.m5stack-core2`, `sdkconfig.esp32-s3-headless`
- `platformio.ini`

ESP-IDF and NimBLE. Paths below are inside the ESP-IDF 5.5.3 framework package,
not this repository:

- `components/bt/host/nimble/nimble/nimble/host/include/host/ble_gap.h`,
  `BLE_GAP_INITIAL_CONN_ITVL_MIN/MAX` and `BLE_GAP_INITIAL_SUPERVISION_TIMEOUT`
- `components/bt/controller/esp32c3/Kconfig.in` (sourced by the S3),
  `BT_CTRL_MODEM_SLEEP`, `BT_CTRL_LPCLK_SEL_MAIN_XTAL`,
  `BT_CTRL_BLE_MAX_ACT`, `BT_CTRL_SCAN_BACKOFF_UPPERLIMITMAX`,
  `BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD`, `BT_CTRL_CE_LENGTH_TYPE`,
  `BT_CTRL_LE_PING_EN`
- `components/bt/controller/esp32/Kconfig.in` for the ESP32 BTDM options
- `components/bt/host/nimble/port/include/esp_nimble_cfg.h` for
  `MYNEWT_VAL_BLE_EXT_ADV`

Model and tooling:

- `tools/power-model/board-currents.yaml` (`esp32s3_radio.ble_rx`,
  `ble_tx_0dbm`, `ble_connected_idle_floor`, `esp32_radio.*`)
- `tools/power-model/README.md`, `tools/power-model/compare.py`
- `tools/power-model/baseline/connected-idle-30s.json`,
  `menu-idle-30s.json`, `screen-off-30s.json`
- `sim/power_profiler.cpp`, `sim/BleSim.cpp`, `sim/scenarios/manifest.json`
- `.github/workflows/power-gate.yml`

Plans:

- `plans/00-hardware-experiments.md` (3.3 mA measured floor, 32 kHz crystal
  absent)
- `plans/07-ble-sleep.md`, `plans/08-scan-tuning.md`,
  `plans/09-reconnect-backoff.md`, `plans/10-adaptive-conn-params.md`,
  `plans/11-adaptive-tx-power.md`, `plans/65-bt-coexistence.md`,
  `plans/77-battery-saver-profile.md`,
  `plans/96-wifi-bt-ble-hardening.md`, `plans/98-power-optimization-audit.md`,
  `plans/98b-power-followup.md`, `plans/158-sim-scheduler-parity.md`

Datasheets and published measurements, as recorded in
`tools/power-model/README.md` and `board-currents.yaml`:

- ESP32-S3 Series Datasheet v2.2, Table 5-8: BLE receive 93 mA (continuous
  receive, whole chip), BLE transmit 176 mA at 0 dBm and 193 mA at 9 dBm, whole
  chip at 100 percent transmit duty.
  https://documentation.espressif.com/esp32-s3_datasheet_en.pdf
- ESP32 Series Datasheet v5.3, Table 5-4: BLE receive 95 to 100 mA, BLE
  transmit 130 mA at 0 dBm at 50 percent duty.
  https://documentation.espressif.com/esp32_datasheet_en.pdf
- Espressif NimBLE `power_save` example README: ESP32 modem sleep 14.1 mA,
  ESP32-S3 modem sleep 17.9 mA, ESP32-S3 light sleep from the main crystal
  3.3 mA, interval not stated.
  https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/power_save
- ESP-IDF BLE low power guide, 500 ppm sleep clock accuracy and the 136 kHz RC
  limitation.
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-ble.html

## Consolidation rows

Ready to paste into `docs/power/opportunity-matrix.md`. The coordinator owns the
final IDs and priority. Only rows with a proposed change are listed; the
no-change entries stay in this lane report. The priority column is this lane's
expected-value rank, not merge order; the safe-now list above is the merge
order, and `prereq` marks the modeling prerequisite.

| ID | Lane | Current behavior | Proposed change | Evidence | Estimated saving | Risk | Effort | Required test | Priority |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| BLE-01 | BLE | Targeted reconnect scan forces Full, 100 percent duty, up to 60 s | 30/60 ms targeted preset | inferred | Up to half the scan receive current | Medium | Small | Host preset test; sim slow advertiser; on-device A/B | 3 |
| BLE-02 | BLE | 60 s targeted scan per attempt | 10 to 15 s bound | inferred | 6x less scan-on per failed attempt | Medium | Small | Host duration test; reconnect success rate | 3 |
| BLE-03 | BLE | Active scan on every path | Passive on targeted only | inferred | Small to medium, unmeasured | Medium | Small | Sniffer capture of match fields; host preset test | 4 |
| BLE-04 | BLE | Discovery runs at the preset until the page closes | Full for 5 to 10 s, then Balanced | inferred | 75 percent less scan radio after decay | Medium | Medium | Sim restart and row-preservation test | 4 |
| BLE-05 | BLE | Balanced is 30/120, 25 percent | 30/320, 9.4 percent | inferred | 2.7x less scan radio than Balanced | Medium | Small | Discovery time at 1 m and 5 m | 4 |
| BLE-06 | BLE | Panel sleep leaves the scan running | Stop the scan on panel sleep | inferred | Full scan load while the panel is off | Low | Small | Sim panel-off scan assertion | 1 |
| BLE-07 | BLE | Scan timeout defaults to Never | Default 60 s | inferred | Bounds the page to 60 s | Low | Small | Sim 60 s finished state; console default test | 1 |
| BLE-08 | BLE | One targeted scan per saved camera | One scan for all, then connect | inferred | N times less scan for N cameras | Medium | Large | Two-peer host test; sim scan count | 5 |
| BLE-11 | BLE | `CONN_SAVER` off by default | Default on or via Battery Saver | inferred | 6 to 8x fewer events; 3.3 mA floor is measured, saving unmeasured | Medium | Small | P06 to P09; vendor soaks; shutter latency | 2 |
| BLE-12 | BLE | Fast 30 to 50 ms | 50 to 100 ms floor | inferred | 1.3 to 3x fewer events | Medium | Small | Soak, latency, `debug camera` | 3 |
| BLE-13 | BLE | Idle 250 to 300 ms | 500 to 1000 ms screen-off profile | inferred | Another 1.7 to 3.3x fewer events | Medium | Medium | First-press latency; geotag freshness; soak | 4 |
| BLE-16 | BLE | No PHY update, link stays 1M | 2M on S3 for capable cameras | inferred | Small, unmeasured | Medium | Small | `getPhy` readback; event current A/B | 5 |
| BLE-18 | BLE | Any peer parameter request blocks the idle saver | Classify the accepted parameters | inferred | Unlocks BLE-11 on those peers | Low | Small | Host peer-request test | 2 |
| BLE-19 | BLE | Initiator scan 10/10 ms, 100 percent | 10/20 ms, 50 percent | inferred | Halves the initiator phase | Low | Small | Host scan-param test; connect time | 2 |
| BLE-20 | BLE | Pairing window 2 minutes at 100 ms | 60 s | inferred | Halves the window charge | Low | Small | Companion pairing tests; on-device pairing | 2 |
| BLE-21 | BLE | Reconnect advertising forever at 1 s | Bound or slow it | inferred | 0.1 to 0.15 mA continuous | Medium | Medium | Companion reconnect latency; bench A/B | 4 |
| BLE-22 | BLE | No preferred companion parameters | PPCP or peripheral update request | unknown | Unknown | Medium | Medium | Android and iOS interval readback | 5 |
| BLE-23 | BLE | Link stays up until user or camera acts | Idle disconnect and reconnect on input | inferred | 3.1 mA idle difference, model-based | Medium | Large | Idle-drain A/B; first-press latency | 5 |
| BLE-24 | BLE | Backoff off, flat 5 s retry | Default on or via Battery Saver | inferred | Fewer scan attempts per hour | Low | Small | Existing backoff test; recovery time | 3 |
| BLE-26 | BLE | Full GATT discovery every connect | Cache the attribute database | inferred | Connect energy, unmeasured | High | Large | Per-family handshake trace; firmware-update test | 5 |
| BLE-30 | BLE | 9 host connections, 6 controller activities | Align or document | inferred | None | None | Small | Six-camera plus scan scenario | - |
| BLE-32 | BLE | No BLE radio term in the model | Add scan, floor, per-event and advertising terms | inferred | Enables ranking of every row | Low | Medium | New power-gate scan scenario; negative test | prereq |

## Change log

- 2026-11-01: initial BLE lane report. No measurement run. Code read at
  `b2734c07`, power/research; code unchanged from `7383561a`.
