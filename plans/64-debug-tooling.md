# 64: expanded debug tooling

Status: plan, no implementation yet. Line anchors below were read at `f455b0b`
on fork master. The plan ships as a series of independently mergeable PRs, one
per phase, each updating this document.

## Motivation

Debugging furble today means reflashing with ad hoc logging. Every power
question starts with sprinkling `ESP_LOGI` calls, rebuilding, and reading tea
leaves in the monitor output. Every soak run so far has used one-off host
scripts polling `status` over the console. The numbers exist, but nothing
collects them.

Onboarding a new camera today needs an external Android phone and an HCI snoop
log. That is how every vendor in `lib/furble` was reverse engineered, and it is
the slowest possible loop: capture on the phone, pull the log, decode it in
Wireshark, guess, reflash, repeat. The device in hand has a BLE radio, a GATT
client, and a USB console, but no way to point them at an unknown camera.

The console from plan 27 is the automation surface. Debug builds already carry
it, host scripts already parse its `key: value` output, and plan 05 gave live
data a home in the UI. What is missing is the data itself: where the power
goes, where the CPU goes, and what actually crosses the BLE link. This plan
adds all three, console first, with read-only diagnostics pages where a page
makes sense.

Three pillars:

1. Power usage stats: pm lock statistics, sleep and frequency residency,
   battery drain rate, and a CSV soak log.
2. Performance monitoring: per-task CPU, heap by capability, LVGL frame
   timing, queue depths.
3. BT debug and onboarding modes: verbose scans, a GATT explorer for unknown
   cameras, and a traffic journal on furble's own vendor connections.

Everything is debug-build only. Release binaries stay byte identical, proven
the same way plan 27 proved it.

## The debug sdkconfig channel

Pillars 1 and 2 need Kconfig symbols that must not reach release builds:

```
CONFIG_PM_PROFILING=y
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_LV_USE_SYSMON=y
CONFIG_LV_USE_PERF_MONITOR=y
```

`CONFIG_PM_PROFILING` is real but undocumented: `components/esp_pm/Kconfig:25`
in the pinned ESP-IDF 5.4.2, "Enable profiling counters for PM locks", depends
on `PM_ENABLE`, default n. `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`
(`components/freertos/Kconfig:289`) selects `FREERTOS_USE_TRACE_FACILITY` and
`FREERTOS_USE_STATS_FORMATTING_FUNCTIONS` automatically. The two LVGL symbols
hang off the Kconfig LVGL is configured through (`CONFIG_LV_CONF_SKIP=y`,
`sdkconfig.m5stick-s3:2467`, there is no `lv_conf.h`).

The problem: debug envs share the release sdkconfig. Each `-debug` env sets
`board_build.esp-idf.sdkconfig_path` to the committed per-board file
(`platformio.ini`), precisely so PlatformIO does not generate a divergent
config. There is no per-env defaults fragment, and `sdkconfig.defaults` is
project-wide, so today there is no debug-only Kconfig channel at all.

Chosen design: a generated overlay. A new `pre:` extra script
`debug_sdkconfig.py`, registered only in the `-debug` envs, follows the
`patches/apply.py` precedent already in `platformio.ini`:

- Read the board's committed release sdkconfig.
- Apply a committed fragment `sdkconfig.debug` (the four symbols above, one
  shared fragment for all boards).
- Replace matching `# CONFIG_X is not set` lines rather than appending, so the
  result is a well-formed sdkconfig with no duplicate keys.
- Write the merged file into the env build directory and point
  `board_build.esp-idf.sdkconfig_path` at it.

The committed release sdkconfigs never change, so the byte-identical release
proof is trivial and the regeneration quirk (derived symbols appended during
builds) lands in the build directory instead of the committed files. Fallback
if PlatformIO's reconfigure detection fights the generated path: commit five
`sdkconfig.<board>-debug` files instead. That works today with zero scripting
but duplicates a 3000-line file per board and must track every release
sdkconfig change. Prefer the overlay, fall back only with evidence.

Overhead is acceptable and stays in debug builds: PM profiling adds counter
updates to every lock operation and sleep transition, run time stats add a
timer read per context switch, and sysmon adds bookkeeping per refresh. Plan
27 already established that power numbers from console builds are not to be
trusted (the ESP32-board APB lock, `src/FurbleConsole.cpp:772-784`). This plan
widens that statement: any absolute measurement from a debug build needs the
release-build sanity check behind it. The drain numbers below are still useful
because they are differential and long-window.

## Pillar 1: power usage stats

### Furble::Power instrumentation

`Furble::Power` (`include/FurblePower.h`, `src/FurblePower.cpp`) tracks only a
live hold count per lock (`lock_t.count`). Add, per lock:

- `std::atomic<uint32_t> totalAcquires`: cumulative acquire count.
- `std::atomic<int64_t> heldSinceUs`: timestamp when `count` went 0 to 1, from
  `esp_timer_get_time()`.
- `std::atomic<int64_t> totalHeldUs`: accumulated on the 1 to 0 transition.
- A small per-owner table, 8 slots of `{const char *owner, uint32_t acquires}`
  keyed by pointer identity. Owners are static literals by contract
  (`include/FurblePower.h:66-68`), so pointer compare is enough. Slot 8 is an
  `other` overflow bucket.

Expose one snapshot accessor, `Power::getStats(LockType)`, returning a plain
struct. The additions are a few words per lock and two timer reads per
transition, cheap enough to keep in release builds, which also keeps the
module free of `#if`. Only the console reporting is gated.

### Console: `power stats`

```
power stats
```

Prints the furble lock table as `key: value` lines:

```
lock.no_light_sleep.held: 1
lock.no_light_sleep.acquires: 42
lock.no_light_sleep.held_ms: 123456
lock.no_light_sleep.owner.gps: 17
...
```

Then calls `Platform::dumpPMLocks()` (`src/FurblePlatform.cpp:160-165`), the
existing `esp_pm_dump_locks(stdout)` wrapper wired to the plan 05 button. With
`CONFIG_PM_PROFILING=y` that same call grows `Total_count`, `Time(us)` and
`Time(%)` per lock, plus the mode stats table: time and percentage per DFS
mode and light sleep entry and reject counts (`esp_pm_impl_dump_stats`,
`components/esp_pm/pm_impl.c`). That table is the frequency and sleep
residency summary per uptime. There is no public getter API for it, only the
dump, so the residency numbers are console-only and host scripts treat the
tabular section as opaque unless they choose to parse it.

### Console: `power log <interval>`

```
power log <seconds>      start periodic CSV emission
power log off            stop
```

Emits one CSV line per interval for unattended soak runs:

```
powerlog: <uptime_s>,<level_pct>,<mv>,<ma_raw>,<ma_ewma>,<drain_ma>,<runtime_min>
```

This replaces the ad hoc host polling scripts used so far and is the
calibration input for plan 63, the sim power model (in flight, not on fork
master yet; this plan is its on-device twin).

Data source: the PR02 EWMA. Today the filter lives in the UI task,
`UI::batteryUpdate` (`src/FurbleUI.cpp:2932-2992`), alpha 1/4 on level and
voltage, 1/12 on current, sampled every 5 s, units percent, mV, mA with
charging positive (`include/FurblePlatform.h:35-40`). Move the filter state
and update step into `Platform` as a `sampleBattery()` helper the existing UI
timer calls, so the console and the UI read one filter instead of running two.
This is a small refactor of release code and gets its own review attention.

Derived values:

- `drain_ma`: the EWMA current when the board has current sense (S3 M5PM1,
  AXP192 boards). On boards without it, fall back to percent per hour computed
  over the log window and mark the column unit in the header line.
- `runtime_min`: `Platform::getBatteryCapacity()` scaled by level over drain,
  the same arithmetic as the PR02 runtime row.

Emission runs in the console task loop. The task already wakes every 100 ms on
its read timeout (`src/FurbleConsole.cpp:787-793`), so a deadline check there
emits lines with no new task and no timer callback doing I/O.

### Diagnostics page

The plan 05 Power state page (`UI::addPowerStateMenu`,
`src/FurbleUI.cpp:3437`) gains one row per furble lock: held count, total
acquires, total held time, fed from `Power::getStats` by the existing shared
1 s diagnostics timer (`UI::diagnosticsUpdate`, `src/FurbleUI.cpp:3265`).
Rows follow the `addInfoRow` pattern and the changed-check rule. The IDF
residency table stays console-only, as the page's existing caveat label
already says.

## Pillar 2: performance monitoring

All commands gated behind `FURBLE_CONSOLE`. The Kconfig symbols come from the
debug channel above. Nothing in this pillar exists in release builds except
the queue depth getters, which are one-line `uxQueueMessagesWaiting` wrappers.

### Console: `perf tasks`

Two `uxTaskGetSystemState` snapshots 1 s apart, diffed. Per task: name,
priority, CPU percent over the sample window, stack high-water mark in bytes.
Diffing sidesteps the run time counter wrap: the default esp_timer clock is
1 MHz and 32-bit, wrapping at about 71 minutes
(`components/freertos/Kconfig:539`), so absolute totals are meaningless on
long uptimes and the command never prints them. The snapshot array is sized
for 24 tasks on the stack of the console task; bump the console task stack
from 6144 if measurement says so.

### Console: `perf heap`

`heap_caps_get_info` per capability: internal, DMA-capable, and SPIRAM on the
S3 (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, `sdkconfig.m5stick-s3:1405`).
Per capability: total free, largest free block, minimum free ever. One line
per fact, `heap.internal.free: 123456` style. This is `status` heap output
grown up; `status` keeps its two summary lines.

### Console: `perf lvgl`

LVGL 9.4 sysmon exposes the numbers programmatically, not just as the overlay:
`lv_display_private.h` gives each display `perf_sysmon_info.calculated` with
`fps`, `cpu`, `render_avg_time` and `flush_avg_time`, reachable via
`lvgl_private.h`. That is a private header of an exactly pinned dependency
(lvgl/lvgl 9.4.0, `src/idf_component.yml`), acceptable for debug-only code
and noted as version-fragile here.

LVGL is not thread safe, so the read happens on the UI task through the
existing request queue: a new `UI::Request::PERF` printing request, drained by
`UI::serviceRequests` under `m_Mutex`, using the `sendPrintingRequest` pattern
(`src/FurbleConsole.cpp:134-141`).

```
perf lvgl                 print fps, cpu, render_avg_ms, flush_avg_ms
perf lvgl overlay on|off  toggle the on-screen sysmon label
```

The overlay toggle maps to `lv_sysmon_show_performance` and
`lv_sysmon_hide_performance`, hidden by default at UI init so a debug build
looks like a release build until asked. Two dead ends verified up front:
`LV_USE_PERF_MONITOR_LOG_MODE` routes through `LV_LOG` and this project builds
with `CONFIG_LV_USE_LOG` unset, so log mode prints nothing; and
`LV_USE_MEM_MONITOR` requires the LVGL builtin allocator while this project
uses `CONFIG_LV_USE_CLIB_MALLOC=y`, so LVGL memory stats are out and
`perf heap` covers memory instead.

### Console: `perf queues`

Depth and capacity for every queue in the firmware, all cheaply readable with
`uxQueueMessagesWaiting`:

- Control command queue, length 32 (`include/FurbleControl.h:151`).
- Per-target queues, length 8 (`include/FurbleControl.h:58`), iterated over
  `Control::getTargets()`.
- UI request queue, length 8 (`include/FurbleUI.h:244`).
- GPS UART event queue, length 32 (`include/FurbleGPS.h:92-93`). This is the
  ESP-IDF driver event queue, so depth here means unserviced UART events, a
  direct leading indicator for the overflow events the GPS task logs.

All four handles are private today, so each owner gains a one-line public
`getQueueDepth()` const accessor. No other release-code change.

### Diagnostics page

One new read-only page, Diagnostics, Performance: heap free, minimum and
largest block for internal plus SPIRAM, and FPS when sysmon is compiled in.
Built on the plan 05 scaffold: `m_PerformanceStr` key in `UI::m_Menu`, an
`addPerformanceMenu` mirroring `addPowerStateMenu`
(`src/FurbleUI.cpp:3437-3467`), labels in `diagnostics_t`, page added to the
timer resume check (`src/FurbleUI.cpp:1272-1279`), changed-check guarded
updates. Task stats stay console-only; a per-task table has no place on an
80x160 screen.

## Pillar 3: BT debug and onboarding modes

### Honest scope

Passive sniffing of third-party connections is impossible here. NimBLE on this
radio is a host stack over an HCI-style controller interface; it sees only
traffic addressed to it, there is no promiscuous mode, and a connection
between a camera and a phone hops channels with parameters this device never
learns. Sniffing stays with dedicated hardware and Android HCI snoop logs.

What the radio can do is be an excellent active endpoint. The practical
onboarding workflow is: find the camera in a verbose scan, connect to it with
no vendor assumptions, walk and poke its GATT tree, then, once a vendor class
exists, watch furble's own traffic against the real camera. The three commands
below cover exactly that, aligned with the plan 61 ranking: Lumix (upstream PR
282 needs validation traffic), Pentax K (one GATT walk from a K body owner
settles the open question), and DJI or GoPro (official specs, journal
validates the port).

### Console: `bt scan [seconds]`

Verbose unfiltered scan, default 10 seconds. Today `Scan::onResult`
(`lib/furble/Scan.cpp:44-51`) silently drops everything
`CameraList::match()` rejects, correct for the UI and useless for onboarding.
The console command uses the existing custom-callbacks `Scan::start` overload
(`lib/furble/Scan.cpp:78-85`, the same one the Fujifilm and Nikon rescan paths
use) with a console-owned `NimBLEScanCallbacks` that dumps every result:

```
adv.addr: aa:bb:cc:dd:ee:ff (random)
adv.rssi: -62
adv.name: X100VI-1234
adv.raw: 0201060907...
adv.rsp: 1109...
adv.mfr: 04d8 6f0...        decoded company id + payload
adv.svc: 0000180a-...       one line per advertised service UUID
```

Raw bytes come from `NimBLEAdvertisedDevice::getPayload()`, hex via
`NimBLEUtils::dataToHexString`. No filter mode is the point: unknown cameras
by definition do not match. Duplicates are suppressed by address with a
`bt scan all` escape hatch for watching advertisement data change, which is
how the Fujifilm pairing token and the Sony pairing-window flags were found.
Refused while a scan or connection attempt is already active; the command
reports the Control state instead.

### Console: `bt explore <addr>`

Connect without vendor logic, then map everything.

```
bt explore <addr> [pair none|bond|passkey]
bt explore read             read every readable characteristic, hexdump
bt explore stop
```

Implementation: a new debug-only module, `src/FurbleBtDebug.cpp` plus
`include/FurbleBtDebug.h`, gated on `FURBLE_CONSOLE`. It drives a raw
`NimBLEClient` directly and deliberately does not subclass `Camera`: no
`Camera::Type`, nothing in NVS, nothing in `CameraList`, no way to leak a fake
camera into release state. Runs in its own on-demand task (8192 stack,
priority 2) since a full discovery walk plus reads is too much for the console
task stack. Refused unless Control is idle, so the explorer never contends
with a real connection for the radio or the Control mutex.

Session flow, all output as `key: value` lines:

1. Connect, print MTU and connection parameters.
2. If a pairing mode is set, `secureConnection()` after discovery below.
3. Full GATT walk with `getServices(true)`,
   `getCharacteristics(true)`, `getDescriptors(true)` (esp-nimble-cpp 2.5.0,
   all verified present and currently unused by furble):
   `svc:`, `chr:` with properties decoded (`R W w N I`) and handle, `desc:`
   including CCCDs.
4. Subscribe to every characteristic with notify or indicate, streaming:
   `notify: <t_ms> <chr uuid> <hexdump>` until disconnect or
   `bt explore stop`.
5. `bt explore read` walks the readable set once and hexdumps each value.

Pairing passthrough is the part the device screen must not be required for.
The explorer installs its own `NimBLEClientCallbacks` (furble overrides only 2
of the 9 available, `lib/furble/Camera.cpp:14-25`, so the security hooks are
free) and forwards everything to the console:

- `onPassKeyDisplay`: print `pair.display: <6 digits>`, for cameras that ask
  the remote to show a code.
- `onConfirmPasskey`: print `pair.confirm: <6 digits>` and wait. The user
  answers `bt pair yes` or `bt pair no`, which calls
  `NimBLEDevice::injectConfirmPasskey`. The Ricoh callbacks
  (`lib/furble/Ricoh.cpp:458-478`) and the companion pairing flow
  (`src/FurbleCompanion.cpp`) are the two existing inject examples.
- `onPassKeyEntry`: print `pair.entry: required`, answered with
  `bt pair key <digits>`.
- `onAuthenticationComplete`: print encrypted, authenticated and bonded flags
  from `NimBLEConnInfo`.

`pair none` skips `secureConnection()` entirely, `pair bond` uses the default
just-works path with the confirm passthrough above, `pair passkey` selects
`SECURE_KEYBOARD_DISPLAY` IO capability. A bond created while exploring is
deleted on `bt explore stop` unless `keep` is given, so experiments do not
fill the bond store (`CONFIG_BT_NIMBLE_MAX_BONDS=15`).

### Console: `bt journal on|off`

The traffic journal for furble's own vendor connections. This is the tool for
debugging existing vendors and validating new ports: watch the Fujifilm GEOTAG
request and response flow live, or diff a Lumix port's writes against the
reference captures from plan 61's sources.

```
bt journal on|off           toggle capture and live streaming
bt journal dump [n]         print the last n entries from the ring
bt journal clear
```

Each record: timestamp ms, direction (`tx` write, `txr` write with response,
`rx` read result, `nfy` notify, `ind` indicate), service UUID, characteristic
UUID, length, payload hexdump, truncation flag.

```
bt: 123456 tx 0x1234 0x5678 8 01000000aabbccdd
```

### Where the hooks go

There is no chokepoint today. `Camera` has zero GATT helpers
(`lib/furble/Camera.h:142-171`), and the vendors call NimBLE directly in three
styles: `NimBLERemoteCharacteristic::writeValue/readValue/subscribe`,
`NimBLEClient::setValue/getValue`, and `NimBLERemoteService::setValue`, about
55 call sites across the vendor files. Notifications are per-vendor lambdas
installed at each subscribe site; there is no central dispatch. Partial
private helpers exist per vendor (`Fujifilm::subscribe`,
`lib/furble/Fujifilm.cpp:37-54`; `CanonEOS::writePrefix`,
`lib/furble/CanonEOS.cpp:34`; `Ricoh::writeByte`, `writeOperation`,
`subscribeCharacteristic`, `lib/furble/Ricoh.cpp:314-350`;
`NikonBase::subscribePair`, `lib/furble/NikonBase.cpp:35-59`) but nothing
shared.

Chosen chokepoint: protected wrappers on the `Camera` base class, and a
mechanical rewrite of the call sites onto them.

```
protected:
  bool gattWrite(NimBLERemoteCharacteristic *chr, const uint8_t *data,
                 size_t length, bool response);
  bool gattWrite(const NimBLEUUID &service, const NimBLEUUID &chr,
                 const uint8_t *data, size_t length, bool response);
  bool gattRead(NimBLERemoteCharacteristic *chr, NimBLEAttValue &value);
  bool gattSubscribe(NimBLERemoteCharacteristic *chr, notify_cb callback,
                     bool indicate = false);
```

The UUID overload absorbs the `client->setValue` style. `gattSubscribe` wraps
the vendor callback in a journaling lambda before installing it, which is the
only way to see notifies without a central dispatcher. The journal call inside
each wrapper is `#if defined(FURBLE_CONSOLE)`; in release builds the wrappers
are thin forwarders and the vendors behave identically.

The alternative, one true chokepoint inside the dependency
(`NimBLERemoteValueAttribute::writeValue/readValue` in the esp-nimble-cpp
managed component, which every style funnels through, plus the client notify
RX path), was considered and rejected. The `patches/apply.py` precedent covers
ESP-IDF, not managed components, and a second patch mechanism against a
version-pinned C++ library is more fragile than 55 mechanical edits. The
wrappers also pay forward: plan 36 Tier B needs exactly this seam to mock the
NimBLE surface, so the refactor is shared infrastructure, not journal-only
cost. Scoping falls out for free: hooks in `Camera` see only camera
connections and never the companion peripheral traffic.

The wrapper refactor is the one part of this plan that rewrites release code
paths across all vendors. It ships as its own PR with no behavior change, no
journal, and gets the full untested-vendor treatment: code review against
each vendor file, FauxNY, Fujifilm on hardware, declared untested for the
rest.

### Memory budget

The journal ring and its entries are allocated once at `bt journal on`, never
in a hot path:

- S3: 64 KB ring in PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`, payload
  cap 64 bytes per entry, 40-byte fixed header (timestamp, direction, flags,
  length, two 128-bit UUIDs), roughly 600 entries. PSRAM is the right place:
  the journal is bulk storage, not DMA, and
  `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` would route it there anyway.
- ESP32 boards: 8 KB ring in internal heap, payload cap 32 bytes, roughly 110
  entries. Debug builds on these boards already carry the console; 8 KB is
  affordable, 64 KB is not.
- `bt explore`: one line buffer of 3x MTU, about 1.6 KB, in the explorer task.
  Scan dumps format from the stack; advertisement payloads are at most 31+31
  bytes.

Truncated payloads set the truncation flag and print `...`. For full payloads
beyond the cap, the journal is the wrong tool and an HCI snoop is honest
advice.

Writer discipline: journal hooks run on the NimBLE host task. The hook does a
bounded memcpy into the ring under a short critical section and returns.
Printing happens in the console task loop, the same 100 ms drain that emits
`power log` lines. The BLE host never blocks on the UART.

### Golden captures for plan 36

Plan 36 Tier D wants a corpus of real Fujifilm X100VI traffic and notes that
captures need either a sniffer with keys or logging at the NimBLE operation
boundary. `bt journal` is exactly that boundary logger. Running
`bt journal on` through a connect, shutter, and GEOTAG cycle against the
X100VI produces the raw material for `tests/corpus/x100vi/`, normalized by
the plan 36 tooling. State this in the journal PR body so the harness work
does not rebuild it.

## Settings

None. Every command is compile-time gated behind `FURBLE_CONSOLE`, so there is
nothing to toggle at runtime and nothing to store, the same argument plan 27
made. No new NVS keys, no new menu switches, no companion characteristics. If
review lands on wanting a runtime toggle for any of it, the fallback is the
standard pattern: settings table entry, console `settings` support, companion
exposure. Prefer none.

## Phases

Order 1, 2, 3. Each phase is one PR. Phases 3a to 3c are internally ordered
but independent of 1 and 2, so pillar 3 can proceed in parallel once the
wrapper question is settled.

1. Debug sdkconfig channel, `Furble::Power` instrumentation, `power stats`,
   `power log`, EWMA move to `Platform`, Power state page lock rows.
2. `perf tasks`, `perf heap`, `perf lvgl`, `perf queues`, queue depth
   getters, Performance diagnostics page.
3. BT debug:
   - 3a. `bt scan`.
   - 3b. `bt explore` with pairing passthrough.
   - 3c. `Camera` GATT wrappers as a standalone no-behavior-change PR, then
     `bt journal` on top.

## Risks

- The overlay script is new build machinery. A wrong merge produces a debug
  build that silently differs from release in more than the intended symbols.
  Mitigation: the script diffs its output against the input and fails the
  build if anything but the fragment's symbols and their direct derivations
  changed. Fallback to committed debug sdkconfigs is always available.
- `CONFIG_PM_PROFILING` is undocumented and its counters touch the sleep
  path. Watch for timing shifts in GPS UART and LEDC behavior on the S3, the
  known DFS victims. Any anomaly gets checked against a build without the
  symbol before blaming anything else.
- The wrapper refactor touches every vendor. It is mechanical, but Canon,
  Nikon, Sony and Ricoh cannot be hardware-tested here. Mitigations: separate
  no-behavior-change PR, byte-level code review per vendor, FauxNY, and the
  plan 36 harness eventually pinning the wrappers down for good.
- `lvgl_private.h` is private API. A future LVGL bump can break `perf lvgl`.
  Acceptable: the dependency is exactly pinned, the code is debug-only, and
  `lv_sysmon_performance_dump()` is the public fallback.
- Run time counters wrap at about 71 minutes. `perf tasks` only ever diffs
  1 s windows, and the help text says absolute totals are unavailable.
- `bt explore` against an unknown device can hang in discovery or flood the
  console with notifications. The explorer task has a discovery timeout, the
  stream is rate-limited by the ring drain, and `bt explore stop` plus
  `disconnect` always work because the console task never blocks on BLE.
- Bond store pollution from exploring. Default-delete the explorer bond on
  stop, as specified.
- Scope creep is the biggest one. The console command list triples in this
  plan. Anything not listed here goes to a follow-up plan, not into these
  PRs.

## Verification

All on the attached M5StickS3 debug build unless stated. Release proof per
phase: build all five release envs, confirm byte-identical binaries apart from
the version string, exactly as plan 27 did.

Phase 1:

1. `power stats` shows the furble lock table and, from `CONFIG_PM_PROFILING`,
   per-lock hold times and the mode residency table. Toggle GPS and confirm
   the `no_light_sleep` numbers move accordingly.
2. Lock rows on the Power state page match `power stats` output.
3. `power log 30` unattended for 4 hours on battery. Confirm no missed
   intervals, CSV parses, drain figure within the envelope of previous manual
   soak measurements, and the file feeds the plan 63 calibration ingest.
4. GPS fix acquisition and backlight behavior unchanged with profiling on,
   per the DFS risk above.

Phase 2:

1. `perf tasks` percentages are plausible: idle tasks dominate at rest, UI
   task rises with the display on, sum per core near 100.
2. `perf heap` SPIRAM numbers present on the S3 and absent on a StickC Plus
   debug build.
3. `perf lvgl` FPS matches the overlay when both are on; overlay toggles
   cleanly and is invisible by default.
4. `perf queues` shows depth 0 at idle and nonzero GPS event depth while
   `gps raw on` floods the console.

Phase 3:

1. `bt scan` finds the X100VI and dumps its manufacturer data; compare
   against the documented Fujifilm advertisement format. Confirm unknown
   household BLE devices appear too, since no-filter is the feature.
2. `bt explore` against the X100VI: full service walk matches the known
   Fujifilm GATT layout, subscribe-all streams the pairing notifications,
   pairing passthrough completes bonding entirely from the console with the
   device untouched.
3. Wrapper PR: full Fujifilm regression via console script, connect, twenty
   `shutter hold` cycles, GEOTAG flow, disconnect, reconnect. FauxNY pass.
   Other vendors declared untested in the PR body.
4. `bt journal on` through a Fujifilm connect and GEOTAG cycle: every write
   and notify appears with correct UUIDs and payloads, matching the wiki
   protocol documentation. Ring wrap behaves at sustained notify load.
   Archive one clean capture as the plan 36 Tier D seed.
5. Journal off: measure shutter latency with and without the journal enabled
   and confirm no observable difference with journal off.

## Relationship to other plans

- Plan 05: created the Diagnostics scaffold and the pm lock dump button this
  plan upgrades. The new pages follow its patterns exactly.
- Plan 27: created the console, the `key: value` contract, the debug-env
  gating and the byte-identical release proof. This plan is its largest
  consumer so far.
- Plan 36: the `Camera` wrappers are the Tier B mock seam, and `bt journal`
  produces the Tier D golden captures.
- Plan 61: pillar 3 is the tooling its ranked onboarding candidates need,
  Lumix validation, the Pentax K GATT walk, DJI and GoPro port checks.
- Plan 63 (sim power model, in flight): `power log` CSV is its calibration
  input; the residency stats cross-check its state model on real hardware.

## References

- ESP-IDF v5.4, Power Management, `esp_pm_dump_locks`, pm locks, DFS:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/power_management.html
- ESP-IDF v5.4, esp_pm Kconfig source, `PM_PROFILING` (undocumented in the
  reference):
  https://github.com/espressif/esp-idf/blob/v5.4/components/esp_pm/Kconfig
- ESP-IDF v5.4, FreeRTOS (IDF) additions, `vTaskGetRunTimeStats`,
  `uxTaskGetSystemState`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/freertos_idf.html
- FreeRTOS kernel, run time statistics:
  https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/08-Run-time-statistics
- ESP-IDF v5.4, Heap Memory Allocation, `heap_caps_get_info`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/mem_alloc.html
- ESP-IDF v5.4, Heap Memory Debugging:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/heap_debug.html
- ESP-IDF v5.4, Console:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/console.html
- LVGL 9.4, System monitor:
  https://lvgl.io/docs/open/9.4/details/auxiliary-modules/sysmon
- esp-nimble-cpp, the pinned BLE library:
  https://github.com/h2zero/esp-nimble-cpp
- NimBLE-Arduino API docs, identical client API surface, `NimBLEClient`,
  `NimBLERemoteService`, `NimBLERemoteCharacteristic`:
  https://h2zero.github.io/NimBLE-Arduino/class_nim_b_l_e_client.html
  https://h2zero.github.io/NimBLE-Arduino/class_nim_b_l_e_remote_service.html
  https://h2zero.github.io/NimBLE-Arduino/class_nim_b_l_e_remote_characteristic.html
- Apache Mynewt NimBLE host, GATT client and GAP:
  https://mynewt.apache.org/latest/network/ble_hs/ble_gattc.html
  https://mynewt.apache.org/latest/network/ble_hs/ble_gap.html
