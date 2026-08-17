# 33 - WiFi hub: displayless build, provisioning, MQTT

Upstream issues: [#248 WIFI feature](https://github.com/gkoh/furble/issues/248)
and [#249 Board-Only support](https://github.com/gkoh/furble/issues/249).

Four separately mergeable PRs. 33a, 33b and 33c are proposed work. 33d is
explicitly deferred and documents why.

All line anchors below were read at commit `2b79ce8` on `master`.

## Goal

Let furble act as a network attached remote so a camera can be triggered from
Home Assistant, from a studio dashboard, or from any MQTT client. Let a bare
ESP32-S3 board with no screen run furble as a fixed installation. Keep the BLE
camera link exactly as reliable as it is today whenever WiFi is off, and prove
how much it degrades when WiFi is on.

## Motivation

#248 asks for WiFi so furble can be a hub. #249 asks for a board only build so
an ESP32-S3 devkit with no display can be that hub in a studio rack. Both come
from the same user with the same setup: a Home Assistant dashboard that already
drives a teleprompter, an e-ink slate and a timecode display, missing only the
camera trigger.

Today the only control surfaces are three buttons and a 1.14 inch screen. That
is the right interface for a remote in a coat pocket. It is the wrong interface
for a device bolted to a wall. The device already runs a full TCP/IP capable
SoC with a WiFi radio that has never been switched on.

The maintainer's own staging in #249 puts the console first and the network
last, for good reasons. The console is the configuration surface for everything
here. There is no room on the screen for an SSID entry widget, and nobody wants
to type a WPA2 passphrase with two buttons.

## Upstream coordination

There is no draft issue for this document. #248 and #249 already exist and they
are the issues.

Map to the maintainer's staging as stated in
[#249 comment 3843918449](https://github.com/gkoh/furble/issues/249#issuecomment-3843918449):

| gkoh stage | Covered by |
|---|---|
| 1. enable an interactive console | `plans/27-usb-console.md` |
| 1b. via console toggle console only or GUI only mode | PR33a, runtime half |
| 2. flesh out the console commands | `plans/27-usb-console.md`, command set section |
| 3. enable a displayless build | PR33a, build half |
| 4. console support for configuring network, MQTT, etc | PR33b and PR33c |

What each PR does to the issues:

- PR33a advances #249. It does not close it. #249 also wants the device driven
  over the network, which is PR33c.
- PR33b advances #248. It delivers the NTP bullet from the #248 optional list.
- PR33c closes #248 as far as the maintainer's stated first target goes, since
  he said in [#248 comment 3843927598](https://github.com/gkoh/furble/issues/248#issuecomment-3843927598)
  that MQTT is a good first target and he will likely start there. Whether to
  close or leave open for WebUI and REST is his call, which is why PR33d exists
  as a written deferral rather than as silence.
- PR33c also closes #249, because a displayless board that can be driven over
  MQTT is exactly the requested feature.

Before writing any code, comment this staging on #249. The maintainer has
already stated his order and said progress will be slow on his end. The useful
contribution is work that lands inside his order, not beside it. Say which of
the four stages we would pick up, confirm that `plans/27` is stages 1 and 2,
and ask whether he wants PR33a as one PR or split into build gate and runtime
toggle. Do not open a competing issue.

---

# PR33a: displayless build profile and console only mode

## Goal

One compile-time profile that builds furble with no display and no LVGL, and
one runtime setting that turns the display off on boards that have one, so the
same binary can run headless.

## Scope

In scope:

- A `FURBLE_NO_DISPLAY` build gate that compiles out `FurbleUI.cpp`,
  `FurbleUIIntervalometer.cpp`, `FurbleCalibrate.cpp`, `FurbleSpinValue.cpp`
  and the `lvgl` and `icons` components.
- A headless main loop that replaces `vUITask` and owns the same
  `Platform::update()` cadence.
- A new PlatformIO environment: the M5StickS3 built without its display stack. A truly generic S3 devkit stays out of scope for 33a; the env defines FURBLE_M5STICKS3 and drives the M5PM1, which a bare devkit lacks (upstream issue 249's bare board ask needs a later PMIC-optional pass).
- A runtime `DISPLAY_MODE` setting with values `GUI` and `CONSOLE`, settable
  from the console, so a StickS3 with a screen can be told to stop using it.

Out of scope:

- Any change to the five existing release environments. They keep building the
  GUI.
- Any new UI element. `DISPLAY_MODE` is set from the console only, per PR27's
  rule that a developer facing feature does not earn a menu entry.
- Buttons. A headless board has no reliable button map. Everything is console.

## Files to change

- `platformio.ini:7-21`, the shared `[env]` block, and a new
  `[env:esp32-s3-headless]` using `board = esp32-s3-devkitc-1` with
  `-DFURBLE_NO_DISPLAY`.
- `src/CMakeLists.txt:1-14`. `furble_sources` lists eight sources plus
  `main.cpp`, and `idf_component_register` has `REQUIRES icons`. Both need to
  become conditional on the gate. `REQUIRES icons` is unconditional today and
  will fail to link a build with no icons component.
- `src/idf_component.yml`. It pulls `lvgl__lvgl` and
  `h2zero__esp-nimble-cpp` from the registry. LVGL must become optional or the
  headless build downloads and compiles a graphics library it never calls.
- `src/main.cpp:13-19`, `vUITask`. It constructs `UI(interval)` and calls
  `ui.task()`, which never returns. The headless path replaces this whole
  function.
- `src/main.cpp:21-40`, `app_main`. `Platform::init()` at line 27,
  `Settings::init()` at 28, `Device::init(...)` at 29, the control task at 32
  and `vUITask(NULL)` at 39. Lines 27 to 32 are display independent and stay.
  Line 39 branches.
- `src/FurblePlatform.cpp:9-45`, `Platform::getInstance`. It calls `M5.begin()`
  with `cfg.clear_display = true` at line 17. On a bare devkit `M5.getBoard()`
  returns `board_unknown` and `M5.Display` has no panel. Confirm M5Unified
  tolerates this rather than assuming it. The `m_PMICHack` switch at lines
  24-32 falls through to `default:` for an unknown board, which is correct.
- `src/FurbleUI.cpp:81-91`, the `UI` constructor. It reads
  `M5.Display.width()`, `M5.Display.height()` and calls `lv_init()` and
  `M5.Display.setBrightness()`. This is the hard dependency that forces the
  whole file out of a headless build rather than a stub.
- `src/FurbleUI.cpp:2123-2134`, `UI::task`. The headless loop must reproduce
  the `Platform::getInstance().update()` call at line 2125 and the 5 ms delay
  at line 2133. `update()` is what services the M5 button and PMIC state.
  Dropping it breaks power button handling on boards that have one.
- `src/FurbleUI.cpp:2094-2111`, `UI::processInactivity`, and
  `src/FurbleUI.cpp:2114-2121`, `UI::handleLockScreen`. Both are display only
  and have no headless equivalent.
- `include/FurbleSettings.h:16-29`, `type_t`. Add `DISPLAY_MODE`.
- `src/FurbleSettings.cpp:11-24`, `m_Setting`. Add the name, key and namespace.

## New settings

| Setting | Type | Default | Effect |
|---|---|---|---|
| `DISPLAY_MODE` | enum `GUI`, `CONSOLE` | `GUI` | `CONSOLE` blanks the panel and stops LVGL rendering |

Default `GUI` means every existing board behaves exactly as it does today on a
fresh NVS and on an upgrade. That is the rule from the roadmap index and it
holds here.

`DISPLAY_MODE` does not exist in a `FURBLE_NO_DISPLAY` build. There is nothing
to toggle. Do not add a stub that silently ignores writes. Have the console
print `not supported in this build`.

## Menu placement

None in the headless build, by definition.

In a GUI build, `DISPLAY_MODE` is deliberately console only. A menu entry that
turns off the menu is a trap: the user cannot get back without the console. If
upstream wants it in the UI later it needs a button chord to recover, which is
a separate design problem.

## Implementation notes

### Two halves, possibly two PRs

The build gate and the runtime toggle are independent. The gate is a build
system change with no runtime behavior. The toggle is a runtime change with no
build system change. If review prefers, split them. Land the gate first,
because the headless main loop is the thing every later PR here depends on.

### The headless main loop

`UI::task` at `src/FurbleUI.cpp:2123-2134` is three things in a loop:
`Platform::getInstance().update()`, the LVGL handler under `m_Mutex`, and a
5 ms delay. Only the first and the third survive without a display.

The headless loop is therefore about ten lines: call `Platform::update()`, drain
the console request queue that PR27 introduces at the same point in the GUI
loop, delay 5 ms. Put it in `main.cpp` next to `vUITask`, not in a new file. It
is too small to justify one.

The 5 ms delay is worth keeping identical rather than lengthening. It is what
makes `Platform::update()` responsive enough for the PMIC click counter at
`src/FurblePlatform.cpp:54-60`.

### The intervalometer is not a UI feature but it lives in the UI

`src/FurbleUIIntervalometer.cpp` is a separate translation unit but
`include/FurbleUI.h` owns the intervalometer state, and `Settings::INTERVAL`
loads into the `UI` constructor at `src/main.cpp:15`. A headless build that
cannot run an intervalometer is not much of a studio hub, and the MQTT topic
design in PR33c has an intervalometer topic.

Do not solve this in PR33a. Scope PR33a to "headless boots, connects, fires the
shutter from the console". Lifting the intervalometer state machine out of `UI`
is its own refactor and it should be argued on its own merits, not smuggled in.
Note it in the PR body as known missing, and make PR33c depend on it.

### Runtime toggle mechanics

`CONSOLE` mode should do the same thing `plans/12-display-off.md` calls true
display off, not merely set brightness to zero. `src/FurbleUI.cpp:2100`, inside
`UI::processInactivity`, only calls `M5.Display.setBrightness(m_MinimumBrightness)`,
which leaves the panel powered.

If PR12 has landed, reuse its display off path. If it has not, the honest
minimum for PR33a is `M5.Display.sleep()` plus skipping `lv_task_handler()`,
and say in the PR body that the panel power saving is PR12's job.

Skipping `lv_task_handler()` while LVGL objects still exist is safe. LVGL does
no work if it is not ticked. Do not tear down the object tree. Re-entering
`GUI` mode has to be instant and it has to land on a working menu.

### Board detection on a bare devkit

`esp32-s3-devkitc-1` is already the board for `m5stick-s3`
(`platformio.ini:39-41`), so the toolchain and flash settings are proven. What is
not proven is `M5.begin()` on hardware with no display, no PMIC and no IMU. The
existing config at `src/FurblePlatform.cpp:16-22` already disables the IMU, the
speaker and the mic, so the remaining risk is the display and `pmic_button`.

Test this on the StickS3 running the headless image. A bare devkit is out of scope for 33a (PMIC dependency, see Scope).
If `M5.begin()` misbehaves, the fallback is to skip M5Unified entirely under
`FURBLE_NO_DISPLAY` and call `esp_timer_get_time()` directly for
`Platform::tick()` at `src/FurblePlatform.cpp:51-53`. That is a bigger change
and should be a second PR if it is needed.

### Flash saved

Measure it, do not estimate it. The S3 release binary today is 1,034,256 bytes
(`.pio/build/m5stick-s3/firmware.bin`), of which `.flash.rodata` is 221,916
bytes, a large part of that being the `icons` component and LVGL font data.
Report `pio run -e m5stick-s3 -t size` against
`pio run -e esp32-s3-headless -t size` in the PR body. The number matters for
`plans/34-ota-partitions.md`, which has to fit two app slots in flash.

## Risks

- `REQUIRES icons` at `src/CMakeLists.txt:13` is unconditional. Getting the
  CMake conditionals wrong breaks all five release builds, not just the new one.
  The build matrix check is mandatory.
- LVGL comes from the component registry via `src/idf_component.yml`. Making a
  managed component conditional is more awkward than making a source file
  conditional. It may be simpler to accept downloading LVGL and rely on the
  linker to drop it. Measure whether it actually drops before accepting that.
- `M5.begin()` on unknown hardware is the single unproven assumption. Everything
  else here is mechanical.
- A user who sets `DISPLAY_MODE=CONSOLE` on a StickS3 and unplugs the cable has
  a brick until they plug back in. Document it, and consider a power button long
  press escape hatch. Do not ship the setting without at least documenting it.
- Two main loops that must stay in step. If someone adds work to `UI::task`
  later and forgets the headless loop, the headless build silently loses it.
  Put a comment at both sites pointing at the other.

## Verification

Attached StickS3, plus a generic ESP32-S3 devkit if one is available.

Build matrix. All five release environments byte identical to master apart
from the version string:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Then:

1. `pio run -e esp32-s3-headless`. Confirm it builds and that `size` shows LVGL
   and icons gone.
2. Flash the headless build to the StickS3. Confirm it boots to a console prompt
   with a blank screen and no crash.
3. From the console, `cameras list`, `connect 0`, `shutter hold 200`. Confirm
   the Fujifilm camera fires with no display involved at all.
4. Leave it connected for 30 minutes. Confirm the link holds with no UI task
   feeding anything.
5. Flash the normal `m5stick-s3` build. `settings set display_mode console`.
   Confirm the screen goes dark and that `shutter hold 200` still fires.
6. `settings set display_mode gui`. Confirm the menu comes back on the page it
   was on, with working buttons.
7. Power cycle in `CONSOLE` mode. Confirm it boots dark and still responds.
8. Measure idle current in `CONSOLE` mode against `GUI` mode with the screen
   dimmed. Report both. This is what tells us whether PR12 is needed here.

Camera coverage: Fujifilm on hardware, FauxNY for the rest. No vendor specific
code is touched, since the console sends the same `Control::cmd_t` values the UI
sends.

## Implementation status

### PR33a, stage 33a

Rebase notes:

- `DISPLAY_MODE` is assigned wire_id 36, continuing after the feedback
  reservations 33 to 35 recorded in plans/23-feedback-outputs.md from PR 30.
  The table row keeps the `FURBLE_NO_DISPLAY` guard. The
  `feat/21-dead-reckoning` branch provisionally used 36 for `GPS_EXTRAP`;
  this PR is ahead in the queue, so `DISPLAY_MODE` keeps 36 and
  dead-reckoning renumbers its provisional ids at its rebase.
- `src/FurbleCompanion.cpp` settingType and settingValue cover `DISPLAY_MODE`
  as SETTING_U8 under the same guard.
- `src/FurbleGPS.cpp` was rewritten on master by PR 27 (burst-windowed
  locking). The displayless guards were re-applied on top: the esp_timer
  include and the three-state icon source logic from master are kept, wrapped
  in `FURBLE_NO_DISPLAY` where they touch LVGL.
- `src/CMakeLists.txt` keeps master's `FurbleCompanion.cpp` in the common
  source list; `FurbleCalibrate.cpp` moves to the display-only list per this
  branch.
- `UI::serviceRequests()` became an instance method. It was declared static
  while calling the non-static `setDisplayMode()`, which fails to compile in
  console (debug) builds. Its only caller is `UI::task()`.

Implemented in `feat/33-wifi-hub`:

- Added the GUI-only `DISPLAY_MODE` console setting, defaulting to `GUI`.
  `CONSOLE` sleeps the panel and skips LVGL handling, while `GUI` wakes it
  and restores the saved brightness.
- Added `esp32-s3-headless` with `FURBLE_NO_DISPLAY`, a headless 5 ms loop,
  and USB console request handling. The profile compiles out the UI sources
  and excludes the `icons` component.
- The headless GPS geotag path is now driven without LVGL. `app_main` calls
  `GPS::init()` under `FURBLE_NO_DISPLAY` to start the UART task and apply the
  stored enable state, and the headless loop in `vUITask` ticks `GPS::update()`
  on the same one second cadence the display build uses via its LVGL timer
  (`GPS::SERVICE_MS`), timed from `esp_timer_get_time()`. `GPS::update()` is
  free of LVGL except for the icon block, which is already guarded out of the
  headless build, so geotag fixes push to the camera with no display present.
- The intervalometer remains unavailable in the displayless profile because its
  state machine still lives in the UI. The planned refactor is required before
  33c.
- Hardware verification on the StickS3 via the console is pending.

Known headless build blockers (pre-existing, outside the sdkconfig and GPS
tick fixes above; the `esp32-s3-headless` env does not yet compile from a clean
tree until they are resolved):

- `src/FurbleCompanionService.cpp` calls `UI::getBatteryLevel()`,
  `getBatteryVoltage()`, `getBatteryCurrent()`, `isBatteryCharging()`,
  `getBatteryVBUSVoltage()`, `getIntervalometerState()` and
  `getIntervalometerRemaining()` unconditionally, but the headless `UI` stub in
  `include/FurbleUI.h` declares none of them, and `FurbleUI.cpp` (their only
  definition) is excluded from the headless source list. The headless status
  record needs a display-independent data source (Platform) for these values.
- `src/FurbleConsole.cpp` references `UI::Request::AUDIT` and `UI::Request::PERF`
  unconditionally, but the headless `UI::Request` enum omits both. These are
  LVGL-only operations; the console commands need a headless gate or a no-op
  handler.
- `src/FurbleGPS.cpp` selects `UART_SCLK_REF_TICK` in the non-`FURBLE_M5STICKS3`
  branch, which does not exist on the ESP32-S3 the headless env targets. The
  headless image runs on the StickS3, so the env likely needs
  `-DFURBLE_M5STICKS3` (which also restores the `UART_SCLK_XTAL` clock the DFS
  trap requires), or the `#else` branch needs an S3-valid clock source.
- `src/main.cpp` console helpers used `auto *` on `CameraList::get()`, which now
  returns `std::shared_ptr<Camera>` after the PR 106 UAF fix. Fixed here to
  `auto` alongside the GPS tick, so `main.cpp` itself compiles.

Deviations:

- The conditional LVGL dependency rules in the `idf_component.yml` files were
  reverted during integration. The `CONFIG_FURBLE_NO_DISPLAY` rule made the
  component manager drop LVGL for the five release envs, because the symbol is
  undefined at dependency resolution time, which broke every GUI build. LVGL
  now resolves unconditionally again. Source gating and `EXCLUDE_COMPONENTS`
  keep the UI and icons out of the headless image. Full LVGL dependency
  exclusion for the headless profile is follow-up work.
- The new `Furble` Kconfig menu appends a derived
  `# CONFIG_FURBLE_NO_DISPLAY is not set` block on regeneration. It was added
  consistently to all five committed sdkconfig files.
- `sdkconfig.esp32-s3-headless` is now a fully expanded config, not a one-line
  stub. `[env:esp32-s3-headless]` sets no explicit `sdkconfig_path`, so
  PlatformIO reads `sdkconfig.esp32-s3-headless` by default and regenerates any
  unset symbol from the `esp32-s3-devkitc-1` board defaults. A stub therefore
  disabled the BLE controller, NimBLE and SPIRAM, leaving the image
  non-functional. The file is now seeded from the working `sdkconfig.m5stick-s3`
  with only `# CONFIG_FURBLE_NO_DISPLAY is not set` flipped to
  `CONFIG_FURBLE_NO_DISPLAY=y`; every other line, including
  `CONFIG_BT_NIMBLE_ENABLED=y`, `CONFIG_BT_CONTROLLER_ENABLED=y`,
  `CONFIG_SPIRAM=y`, `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` and
  `CONFIG_FREERTOS_UNICORE=y`, is identical to the StickS3 config.

The `m5stick-s3` build passes with `FURBLE_VERSION=dev FURBLE_TEST=0` after
the revert.

---

# PR33b: WiFi station provisioning over the console, and NTP

## Goal

Store one WiFi network in NVS, bring the station up only when explicitly asked,
and sync the clock from NTP when it is up. Nothing joins a network by default.

## Scope

In scope:

- `esp_wifi` in station mode, one saved network.
- Console commands to set SSID and passphrase, connect, disconnect and show
  status.
- Credentials in NVS through the existing `Settings` interface.
- SNTP time sync gated behind its own setting.
- A documented power cost.

Out of scope:

- SoftAP, WPS, SmartConfig, Improv, BLE provisioning. One network, typed once,
  over a cable that is already plugged in. #249's use case is a fixed studio
  install.
- Multiple saved networks. Add it when someone asks.
- Enterprise WPA. No.
- Anything that opens a listening socket. That is PR33c and PR33d.

## Files to change

- `src/CMakeLists.txt:12-14`. `PRIV_REQUIRES` gains `esp_wifi esp_netif
  esp_event nvs_flash lwip`.
- `src/main.cpp:27-29`. `esp_netif_init()` and
  `esp_event_loop_create_default()` have to run once, after
  `Settings::init()` and before anything touches the network.
  `nvs_flash_init()` is already done inside `Preferences`
  (`src/FurbleSettings.cpp:39-45`), so do not call it twice.
- New `src/FurbleWiFi.cpp` and `include/FurbleWiFi.h`. Add to `furble_sources`
  at `src/CMakeLists.txt:1-10`.
- `include/FurbleSettings.h:16-29`, `type_t`, and `src/FurbleSettings.cpp:11-24`,
  `m_Setting`. Five new entries.
- `include/FurbleSettings.h:96-148`, the load and save specialisations. There is
  no `std::string` specialisation today. SSID and passphrase need one, and
  `Preferences` supports it.
- The PR27 console command table. New `wifi` and `ntp` command groups.
- `src/FurbleGPS.cpp:174-187`, the geodata time path. NTP time feeds the same
  `timesync_t` the GPS path builds. See `plans/90-scheduled-shooting.md`.

## New settings

| Setting | Type | Default | Effect |
|---|---|---|---|
| `WIFI` | bool | `false` | master enable. false means the radio never starts |
| `WIFI_SSID` | string | `""` | station SSID |
| `WIFI_PSK` | string | `""` | WPA2 passphrase |
| `NTP` | bool | `false` | sync the clock when WiFi is up |
| `NTP_SERVER` | string | `"pool.ntp.org"` | SNTP server |

Every default keeps the radio off. A user who upgrades and never touches the
console sees no change in behavior, no change in battery life and no change in
BLE reliability. That is the point.

`WIFI_PSK` in plaintext NVS is the same trust level as the BLE bonding keys
already stored there. Say so in the PR body. Do not add NVS encryption in this
PR. It is a separate decision that affects the migration in
`plans/34-ota-partitions.md`.

## Menu placement

None. Console only, per #249 stage 4.

If upstream later wants a status readout on the screen, the right place is the
Diagnostics submenu from `plans/05-diagnostics-scaffold.md`, read only. Never
put credential entry on a 135 by 240 panel.

## Implementation notes

### Power discipline

This is the part that must not be negotiable.

Measured and published figures, ESP32-S3:

| State | Current | Source |
|---|---|---|
| BLE connected, modem sleep | 17.9 mA | ESP-IDF chip series comparison, quoted in `plans/07-ble-sleep.md:130-134` |
| BLE connected, light sleep on main XTAL | 3.3 mA | same |
| WiFi station, modem sleep, DTIM1 | 40.1 mA | ESP-IDF low power mode, WiFi scenario, shielded box |
| WiFi station, modem sleep, DTIM3 | 38.7 mA | same |
| WiFi station, modem sleep, DTIM10 | 38.2 mA | same |
| WiFi station, DTIM1, 160 MHz, DFS on | peak 113.5 mA, min 15.0 mA | same |

The StickS3 has a 250 mAh battery (`plans/02-battery-display.md:126`). At
17.9 mA that is roughly 14 hours. At 40.1 mA it is roughly 6 hours. The DTIM
setting barely matters. What matters is that the radio is on at all.

Worse, WiFi and automatic light sleep do not combine for free. The 3.3 mA
number that `plans/07-ble-sleep.md` is chasing depends on the CPU actually
sleeping. A station that has to wake for every DTIM beacon does not get there.
Enabling WiFi therefore costs more than the difference in the table. It
forfeits the largest saving in the whole roadmap.

Rules that follow:

- The radio starts only on `wifi connect` or on boot when `WIFI` is true. There
  is no implicit start, no "try it and see", no background scanning.
- `wifi disconnect` calls `esp_wifi_stop()` and `esp_wifi_deinit()`, not just
  disconnect. Leaving the driver initialised keeps the PHY calibrated and the
  buffers allocated.
- Call `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` explicitly. Do not rely on the
  default.
- The console `wifi status` output must include the current and the estimated
  runtime, using the same numbers `plans/02-battery-display.md` produces. A user
  who turns on WiFi should be able to see what it cost, on the device, without
  a datasheet.
- On a battery build, print a warning when `WIFI` is set true. On a headless
  build the device is mains powered by definition and the warning is noise, so
  suppress it under `FURBLE_NO_DISPLAY`.

### Never scan while connected to a camera

This is the coexistence mitigation that costs nothing and buys the most. See
the coexistence section below.

Store the BSSID and the channel alongside the SSID at first successful connect.
On later connects, populate `wifi_config_t.sta.bssid`, `bssid_set = true` and
`channel`, so `esp_wifi_connect()` goes straight to the right channel instead
of sweeping all fourteen. A full active scan is the single worst thing that can
happen to a BLE connection on a shared radio.

If a direct connect fails twice, fall back to a scan, but refuse to scan while
`Control::getState()` is `STATE_ACTIVE`. Print `refusing to scan while a camera
is connected` and let the user disconnect first. That is a real limitation and
it should be visible rather than hidden.

### Reconnect policy

Reuse the shape from `plans/09-reconnect-backoff.md`. Exponential backoff from
1 s to a 60 s cap, with jitter, reset on success. Do not use a fixed retry
timer. A studio device whose access point reboots should not hammer the channel
while a shoot is running.

Cap the total attempts at zero, meaning never give up, but hold the 60 s cap
forever. A wall mounted device has nobody to press a button.

### NTP

`esp_netif_sntp_init()` with `ESP_NETIF_SNTP_DEFAULT_CONFIG(server)`, started
on the got-IP event and stopped when the station stops. Use
`SNTP_SYNC_MODE_IMMED`, the default. Smooth mode uses `adjtime()` and is for
devices that must never step the clock. furble has no such constraint and
stepping to the correct time immediately is what a camera clock sync wants.

`CONFIG_LWIP_SNTP_UPDATE_DELAY` defaults to one hour. Leave it. Nothing here
needs better than that.

Timezone: store UTC internally and do not add a TZ setting in this PR. The
camera geodata path at `src/FurbleGPS.cpp:174-187` already builds a
`timesync_t`, and GPS time is UTC. Feeding NTP time through the same path with
the same convention means zero new conversion code and zero new bugs. A
per-camera local time preference is a separate question that
`plans/90-scheduled-shooting.md` already raises.

What this unlocks: `plans/90` documents that furble only sends time to cameras
as part of a geodata update and only when a GPS fix is present. NTP is a second
time source with no satellite, no UART and no sleep lock. It does not solve
`plans/90` on its own, since the vendor protocols still couple time to
location, but it removes the receiver from the dependency chain for a mains
powered device that has WiFi anyway.

### Console commands

```
wifi status              enabled, state, ssid, bssid, channel, rssi, ip, current
wifi set ssid <ssid>
wifi set psk <psk>
wifi enable | disable    writes the WIFI setting, applies immediately
wifi connect | disconnect
wifi forget              clears ssid, psk, bssid, channel
ntp status               enabled, server, last sync, offset
ntp set server <host>
ntp enable | disable
ntp sync                 force one sync now
```

`wifi set psk` echoes nothing back. `wifi status` prints `psk: set` or
`psk: unset`, never the value. PR27 already establishes that the console never
dumps raw NVS.

## Risks

- Battery life roughly halves on the StickS3 whenever WiFi is up. This is not a
  bug and it cannot be engineered away. It can only be defaulted off and
  documented, which is what this PR does.
- Flash and RAM cost. See the budget section below. This is the risk that
  interacts with `plans/34-ota-partitions.md` and it needs a measured number
  before 34 can size its app slots.
- `std::string` load and save specialisations are new in `Settings`. Get the
  length handling wrong and a 63 character passphrase corrupts NVS. Test at the
  boundary: 0, 1, 8, 63 and 64 characters.
- A wrong passphrase produces a reconnect loop that keeps the radio on. The
  backoff cap bounds it but the radio never turns off. Consider disabling
  `WIFI` automatically after N consecutive auth failures, and print why.
- `esp_netif_init()` and `esp_event_loop_create_default()` are process global.
  Calling them twice returns an error that is easy to ignore and hard to debug.
  Call them once, from `app_main`, guarded.

## Verification

Attached StickS3.

1. Fresh NVS boot. Confirm `wifi status` shows `enabled: false` and that the
   radio never comes up. Confirm idle current matches a master build.
2. `wifi set ssid`, `wifi set psk`, `wifi enable`, `wifi connect`. Confirm an IP
   inside 15 seconds and that `wifi status` shows bssid, channel and rssi.
3. `wifi status` after a reboot. Confirm it reconnects using the stored bssid
   and channel, and time the connect. It should be faster than the first one.
4. `ntp enable`, `ntp sync`. Confirm the system clock lands within a second of
   the host clock.
5. Connect the Fujifilm camera with GPS off and NTP synced. Confirm what the
   camera clock does. Document the result honestly, including if the answer is
   "nothing, because the vendor protocol needs a fix".
6. Wrong passphrase. Confirm backoff, confirm the console says why, confirm no
   crash and no watchdog.
7. Access point powered off for five minutes then back on. Confirm reconnect.
8. `wifi connect` while a camera is connected. Confirm the scan refusal fires
   and the camera link is not disturbed.
9. Measure idle current in four states: WiFi off, WiFi associated idle, WiFi
   associated with a camera connected, WiFi off with a camera connected. Report
   all four.
10. Run the shutter latency test from the coexistence section below.

---

# PR33c: MQTT client

## Goal

Publish furble state and accept furble commands over MQTT, with optional Home
Assistant autodiscovery, so a dashboard can trigger the camera.

## Scope

In scope:

- `esp-mqtt` client, one broker, username and password auth, plain TCP and TLS.
- A topic tree for shutter, focus, intervalometer, status and GPS.
- Last will and testament for availability.
- Optional Home Assistant discovery publication.
- Console configuration for all of it.

Out of scope:

- Running a broker. furble is a client.
- Client certificate auth. Username and password, or nothing. Add certificates
  when someone asks and when there is flash to spare.
- Bridging camera images or any bulk data. This is a control channel.

## Files to change

- `src/CMakeLists.txt:12-14`. `PRIV_REQUIRES` gains `mqtt` and `json`.
- New `src/FurbleMQTT.cpp` and `include/FurbleMQTT.h`.
- `include/FurbleSettings.h:16-29` and `src/FurbleSettings.cpp:11-24`. Six new
  entries.
- `include/FurbleControl.h:13-22`, `cmd_t`. `CMD_SHUTTER_PRESS`,
  `CMD_SHUTTER_RELEASE`, `CMD_FOCUS_PRESS`, `CMD_FOCUS_RELEASE`, `CMD_CONNECT`
  and `CMD_DISCONNECT` are what the command topics map onto.
- `src/FurbleControl.cpp:183-185`, `Control::sendCommand`. One `xQueueSend` with
  a zero timeout, safe from the MQTT event task. Same entry point PR27 uses.
- `src/FurbleControl.cpp:159-178`. Commands are only forwarded in
  `STATE_ACTIVE`. The MQTT layer must publish an error rather than accept
  silently when no camera is connected.
- `include/FurbleControl.h:99` `getTargets()` and `:124` `getState()`. These
  back the status topics.
- The UI request queue PR27 adds at `src/FurbleUI.cpp:2123-2134`. Anything that
  touches LVGL, which includes connect and disconnect via
  `UI::doConnect` at `src/FurbleUI.cpp:1229-1249` and `UI::doDisconnect` at
  `1251-1263`, goes through it. In a headless build it goes through the PR33a
  loop instead.
- `lib/furble/Device.h:37`, `Device::getStringID()`, which returns the
  `furble-xxxx` identifier (`lib/furble/Device.cpp:55`). It is the natural MQTT
  client ID and the natural topic segment.

## New settings

| Setting | Type | Default | Effect |
|---|---|---|---|
| `MQTT` | bool | `false` | master enable |
| `MQTT_URI` | string | `""` | `mqtt://host:1883` or `mqtts://host:8883` |
| `MQTT_USER` | string | `""` | broker username |
| `MQTT_PASS` | string | `""` | broker password |
| `MQTT_BASE` | string | `"furble"` | topic root |
| `MQTT_HA` | bool | `false` | publish Home Assistant discovery |

Off by default. `MQTT` true with `WIFI` false does nothing and says so.

## Menu placement

None. Console only.

## Implementation notes

### Topic design

Let `ID` be the existing `furble-xxxx` identifier and `BASE` be `MQTT_BASE`.

Commands, furble subscribes:

| Topic | Payload | QoS |
|---|---|---|
| `BASE/ID/cmd/shutter` | `press`, `release`, `hold <ms>` | 1 |
| `BASE/ID/cmd/focus` | `press`, `release` | 1 |
| `BASE/ID/cmd/interval` | `start`, `stop` | 1 |
| `BASE/ID/cmd/connect` | camera index, or `all` | 1 |
| `BASE/ID/cmd/disconnect` | empty | 1 |

State, furble publishes:

| Topic | Payload | QoS | Retain |
|---|---|---|---|
| `BASE/ID/status` | `online`, `offline` | 1 | yes |
| `BASE/ID/state/cameras` | JSON array of name, connected, rssi | 1 | yes |
| `BASE/ID/state/battery` | JSON percent, mV, mA, charging | 0 | yes |
| `BASE/ID/state/interval` | JSON running, shot, total, next in ms | 1 | no |
| `BASE/ID/state/gps` | JSON fix, sats, lat, lon, alt, age | 0 | no |
| `BASE/ID/state/shutter` | `idle`, `held` | 1 | yes |

### QoS choices, and the reason for each

QoS 1 on every command. A dropped shutter is a missed frame, which is the whole
point of the device. QoS 1 is at least once, so a redelivery is possible.

That redelivery is the reason `hold <ms>` exists and is the documented way to
trigger. A duplicated `hold 200` fires one extra frame, which is annoying. A
duplicated `press` with a lost `release` leaves the shutter held, which can fill
a card or cook a sensor. Publish `press` and `release` as available primitives
because scripts want them, but tell users to prefer `hold`. This mirrors the
same advice in `plans/27-usb-console.md`.

Do not use QoS 2. esp-mqtt supports all three levels, but QoS 2 costs two extra
round trips per message on a shared radio while a camera link is running, and
it does not solve the duplicate-shutter problem any better than making the
command idempotent does.

QoS 0 for battery and GPS. They are sampled state. A lost sample is replaced by
the next one within seconds. Paying for delivery guarantees on data that is
stale on arrival is waste, and every retransmit is radio time stolen from BLE.

Retain on `status`, `cameras`, `battery` and `shutter`. A dashboard that starts
after furble did should see current state without waiting for the next sample.
Do not retain `gps` or `interval`. Retained position data outlives the device
being switched off and is then simply wrong, and a retained `running` on a
timelapse that has finished is worse than no data.

Last will: topic `BASE/ID/status`, payload `offline`, QoS 1, retain true, set in
`esp_mqtt_client_config_t.session.last_will`. Publish `online` to the same topic
on `MQTT_EVENT_CONNECTED`. This is the only availability signal that survives a
battery pull.

### Reconnect policy

esp-mqtt reconnects on its own. Do not write a reconnect loop. Set
`network.reconnect_timeout_ms`, leave `disable_auto_reconnect` false, and handle
`MQTT_EVENT_DISCONNECTED` by logging only.

Set `session.keepalive` to 60 seconds. Shorter keepalives mean more radio
wakeups for no benefit on a control channel where the LWT covers the failure
case.

Set `session.disable_clean_session` to false, so the broker does not hold a
session for a device that may be off for days. furble republishes everything it
needs on connect. There is no useful server side state.

On `MQTT_EVENT_CONNECTED`: publish `online`, publish all retained state, then
subscribe to the command topics. In that order. Subscribing first means the
first command can arrive before the device has told anyone it is there.

The MQTT event handler runs on the esp-mqtt task. `Control::sendCommand` is safe
from it. LVGL is not. Route connect and disconnect through the PR27 UI request
queue.

### Home Assistant discovery

Home Assistant's MQTT integration reads a discovery prefix, default
`homeassistant`. Two topic forms exist. The per component form is
`<prefix>/<component>/[<node_id>/]<object_id>/config`. The newer device form is
`<prefix>/device/<object_id>/config`, and its payload must contain a `device`
mapping, abbreviated `dev`, and an `origin` mapping, abbreviated `o`.

Use the device form. One retained publish creates every entity, one `device`
block ties them together in the UI, and one delete removes the lot. The per
component form needs six publishes and six deletes and repeats the device block
in each.

Topic: `homeassistant/device/furble_<ID>/config`, published retained on every
MQTT connect after `online`.

Entities to declare:

| Component | Purpose | Topic used |
|---|---|---|
| `button` | shutter | `BASE/ID/cmd/shutter`, payload `hold 200` |
| `button` | focus | `BASE/ID/cmd/focus` |
| `switch` | intervalometer | `BASE/ID/cmd/interval`, state `BASE/ID/state/interval` |
| `sensor` | battery percent | `BASE/ID/state/battery` with a value template |
| `sensor` | connected cameras | `BASE/ID/state/cameras` |
| `binary_sensor` | camera connected | `BASE/ID/state/cameras` |
| `device_tracker` | GPS position | `BASE/ID/state/gps` |

Every entity carries `unique_id` derived from the `furble-xxxx` identifier plus
the entity name, and `availability_topic` set to `BASE/ID/status` with
`payload_available: online`. Without `unique_id` Home Assistant will not let the
user rename or reassign the entity, which is the first thing they will try.

Subscribe to `homeassistant/status`. Home Assistant publishes `online` there on
start. Republish the discovery payload when that arrives. Without this, a Home
Assistant restart after a broker restart loses the entities until furble
reconnects on its own schedule.

Retained discovery has a known downside, stated in the Home Assistant docs: the
payload stays at the broker after the device is gone. Provide
`mqtt discovery clear` on the console, which publishes an empty retained payload
to the discovery topic. That is the documented removal mechanism and users will
need it.

Gate all of this behind `MQTT_HA`, default false. A user with a plain broker and
no Home Assistant should not see `homeassistant/...` topics appear.

### Payload format

JSON via the `json` component, which is cJSON and is already in the IDF tree.
Keep payloads under 256 bytes. Flat objects, no nesting beyond one level, no
arrays longer than the camera count.

Do not invent a binary format. The audience for this feature is a dashboard,
and the debugging tool is `mosquitto_sub`.

### Publish cadence

Battery every 60 seconds and on change of charging state. GPS every 10 seconds
while a fix is held, never while there is no fix. Cameras on every connect,
disconnect and rssi change beyond 5 dBm. Interval on every shot.

Do not publish on a fixed fast timer. Every publish is radio time contended
with BLE, and see the coexistence section for why that matters.

## Risks

- MQTT keeps the radio up continuously. The 40.1 mA figure from PR33b is the
  steady state cost of this feature, not a peak.
- A broker that is unreachable produces a permanent reconnect loop with the
  radio on. Bound it: after 10 consecutive failed connects, stop the client, log
  it, and require `mqtt connect` to retry. Do not stop WiFi, the user may still
  want the console over the network later.
- Duplicate shutter delivery under QoS 1. Mitigated by preferring `hold`, not
  eliminated.
- `MQTT_PASS` in plaintext NVS, same argument as `WIFI_PSK`.
- TLS pulls mbedTLS into the image. See the budget section. If the number does
  not fit, ship plain MQTT first and TLS as a follow up, and say so.
- The intervalometer topic depends on lifting intervalometer state out of `UI`,
  which PR33a explicitly does not do. Either PR33c does that refactor or it
  ships without the intervalometer topic. Decide before starting, do not
  discover it halfway.

## Verification

Attached StickS3, a Mosquitto broker on the LAN, and a Home Assistant instance.

1. `mqtt` disabled. Confirm nothing is published and no socket opens.
2. Configure and enable. Confirm `online` appears retained on
   `furble/ID/status` within 10 seconds of connect.
3. Pull the battery. Confirm `offline` appears via the LWT within the keepalive
   window.
4. `mosquitto_pub` to `furble/ID/cmd/shutter` with `hold 200`. Confirm the
   Fujifilm fires.
5. Publish 50 `hold 200` commands at 2 second intervals. Confirm 50 frames, no
   duplicates, no stuck shutter.
6. Publish a shutter command with no camera connected. Confirm an error is
   published and nothing hangs.
7. Enable `MQTT_HA`. Confirm the device and all seven entities appear in Home
   Assistant with no YAML.
8. Press the shutter button in the Home Assistant UI. Confirm the camera fires.
9. Restart Home Assistant. Confirm the entities come back without touching
   furble.
10. `mqtt discovery clear`. Confirm the device disappears from Home Assistant.
11. Kill the broker for 10 minutes. Confirm reconnect, confirm the bounded retry
    limit works, confirm the BLE camera link survived the whole outage.
12. Run the shutter latency test from the coexistence section with MQTT
    connected.

---

# PR33d: REST API and WebUI, deferred

Not proposed. #248 asks for all three of WebUI, MQTT and REST. This documents
why only MQTT is being built.

**The maintainer picked MQTT.** He said so in
[#248 comment 3843927598](https://github.com/gkoh/furble/issues/248#issuecomment-3843927598):
MQTT is a good first target and he will likely start there. Building a WebUI
first would be building against his stated order.

**A WebUI is a second UI to maintain.** furble's UI is one 2136 line file,
`src/FurbleUI.cpp`. Every feature in this roadmap that adds a setting adds a
switch to it. A WebUI means every one of those features gains a second widget,
in a second language, with a second layout, that has to agree with the first.
The intervalometer alone is a three roller spinner plus a unit selector
(`include/FurbleSpinValue.h`, `src/FurbleUIIntervalometer.cpp`). Reimplementing
that in HTML is not a small job and keeping it in agreement forever is a larger
one. This is the UI element explosion argument and it is decisive.

**A WebUI costs flash that is already spoken for.** An HTTP server, mDNS and an
embedded asset bundle land in the same image that has to fit an OTA slot. See
`plans/34-ota-partitions.md`, which sizes app slots at 1700K against a current
binary of roughly 1.01 MB. WiFi, lwIP, MQTT and TLS already claim most of the
difference.

**REST has no free authentication story.** MQTT gets a broker, and the broker
already holds credentials, access control and TLS. A REST endpoint on the device
has to solve all three itself, on a device with no clock at boot and no way to
rotate a secret. The bad version is an unauthenticated HTTP endpoint that fires
the shutter, on a studio network, for anyone.

**MQTT covers the stated use case.** #249's use case is a Home Assistant
dashboard. Home Assistant speaks MQTT natively with autodiscovery, so PR33c
delivers it with zero integration code. A REST API would need a custom Home
Assistant integration, which is exactly what #248 says MQTT avoids.

Revisit when all of these hold: PR33c has shipped and the topic set has been
stable through at least one release, someone asks for REST for a use case MQTT
genuinely cannot serve, and the OTA app slot has more than 400 KB of measured
headroom.

---

# Home Assistant integration design

Added after the displayless build (PR33a) merged on fork master. This section
extends PR33c. It changes no decision made above. It answers one question:
what is the right way to make furble a first-class Home Assistant citizen, and
what exactly does Home Assistant see.

## Three candidate paths

### MQTT with HA discovery, extending PR33c

The path already designed above. furble stays furble, esp-mqtt publishes
state and discovery payloads, Home Assistant's stock MQTT integration creates
the entities with zero custom code on the HA side.

Cost accounting: esp-mqtt and cJSON are tens of kilobytes of flash on top of
the WiFi stack that PR33b already pays for. The discovery layer itself is only
JSON strings, low single-digit kilobytes. Against the plan 34 slot budget of
1700K with 686 KB of measured headroom before WiFi, this is the smallest
possible HA footprint. BLE coexistence is governed by the publish-on-change
cadence rules already written, and nothing new listens on a socket.

### ESPHome external component

Package furble's camera control as an ESPHome external component, and let
ESPHome provide WiFi, the native HA API, OTA and configuration.

Rejected. ESPHome is not a library, it is a competing firmware framework with
its own build system, its own main loop, its own OTA and its own BLE stack
usage. furble as an external component means porting the vendor protocol
library, the reconnect state machine and the pairing flows into ESPHome's
runtime, then maintaining that port beside the real firmware forever. The five
release environments, the LVGL UI, the companion GATT service and the plan 34
OTA scheme all live outside ESPHome and would remain, so the project would
ship two firmwares. ESPHome's native API also holds a persistent TCP
connection from Home Assistant with its own keepalive, which is a standing
radio cost the MQTT cadence rules exist to avoid. The one thing ESPHome would
buy, effortless HA entities, is exactly what MQTT discovery already provides
for a few kilobytes of JSON.

### Native REST plus a custom HA integration

Rejected for the reasons PR33d already states: no authentication story on the
device, flash spent on an HTTP server, and a custom Home Assistant integration
to write and maintain, which is the work #248 explicitly hoped MQTT would
avoid. Nothing in the HA layer changes that judgment. No REST endpoints are
part of this design.

**Recommendation: MQTT with HA discovery, as PR33c, with the entity model
below.** The displayless build merged first, so the mains-powered wall device
that #249 asked for exists; MQTT discovery is the shortest path from that
device to a dashboard button.

## Entity model: hub device plus camera devices

PR33c's discovery table treats furble as one flat device. Home Assistant can
do better: each saved camera should be its own HA device, linked to the hub,
so automations and dashboards target "the A7 III" rather than "furble camera
slot 2". MQTT discovery supports this directly through `via_device` in the
`device` block.

Camera identity uses the stable per-camera `camera_id` designed in
[51-app-feature-parity.md](51-app-feature-parity.md) section 2.1. The ids are
allocated once, never reused, and survive reordering, which is exactly what an
HA `unique_id` needs. Without them, deleting one camera would silently rebind
every dashboard reference to a different body.

### The hub device

`furble_<ID>`, where `ID` is the existing `furble-xxxx` identifier. Entities:

| Component | Entity | Topic |
|---|---|---|
| `button` | shutter, all connected cameras | `BASE/ID/cmd/shutter`, payload `hold 200` |
| `button` | focus | `BASE/ID/cmd/focus` |
| `switch` | intervalometer | `BASE/ID/cmd/interval`, state `BASE/ID/state/interval` |
| `sensor` | battery percent, `device_class: battery` | `BASE/ID/state/battery` |
| `sensor` | battery voltage, diagnostic category | `BASE/ID/state/battery` |
| `device_tracker` | GPS position, `source_type: gps` | see below |

The `device_tracker` question from the PR33c table gets a firm yes, with
constraints. The tracker uses `json_attributes_topic` pointing at
`BASE/ID/state/gps` for latitude, longitude and gps_accuracy. Publish only
while a fix is held and never retained, per the PR33c cadence rules; a
retained position for a powered-off device is a lie on a map. When no fix is
held the tracker simply goes stale, which HA renders honestly.

### One device per camera

Discovery topic `homeassistant/device/furble_<ID>_cam<camera_id>/config`,
retained, published beside the hub payload. Each carries
`via_device: furble_<ID>`. Entities per camera:

| Component | Entity | Topic |
|---|---|---|
| `binary_sensor` | connected, `device_class: connectivity` | `BASE/ID/camera/<camera_id>/state` |
| `sensor` | link RSSI, dBm, diagnostic category | same state topic, value template |
| `button` | connect | `BASE/ID/cmd/connect`, payload `<camera_id>` |

New state topic, retained, published on the same change-driven cadence as
`state/cameras`:

```
BASE/ID/camera/<camera_id>/state
  JSON: name, type, connected, state, progress, rssi
```

The flat `BASE/ID/state/cameras` array from PR33c stays for compatibility and
for dashboards that want one blob. The per-camera topics are additive.

Per-camera shutter buttons are deliberately absent from version 1.
`Control::sendCommand` fans out to all active targets
(`src/FurbleControl.cpp:183-185`); there is no per-target trigger in the
control layer yet. Plan 51 reserves per-camera addressing on its wire, and
when `Control` grows it, the camera device gains a shutter button in the same
release. Declaring the button before the firmware can honor it would fire
every camera and surprise exactly the multi-camera users it targets.

Availability: every camera entity lists two availability topics with
`availability_mode: all`, the hub LWT `BASE/ID/status` and its own connected
state. A camera that is saved but disconnected shows unavailable rather than
off, which matches what the on-device Cameras page shows as `lost`.

### Location push, the reverse direction

Home Assistant already knows where things are. A new command topic closes the
#248 QOL request for devices lacking GPS without any OSM lookup on the
device:

```
BASE/ID/cmd/location
  JSON: latitude, longitude, altitude, accuracy, timestamp
```

The payload feeds `GPS::setExternalFix`, the same entry point the companion
BLE location write uses, and the same 30 s staleness arbitration applies. An
HA automation can forward a person entity or zone coordinates to a studio
furble on mains power. This is strictly optional, off unless something
publishes to it, and costs one subscription.

## Mapping to upstream issue #248

| #248 item | Where it lands |
|---|---|
| MQTT control, HA without a custom integration | PR33c plus this entity model |
| WebUI | deferred, PR33d, unchanged |
| REST API | deferred, PR33d, unchanged |
| NTP sync | PR33b |
| Location parser for devices lacking GPS | `cmd/location` topic above, HA side pushes, no OSM client on device |
| OTA updates | `plans/34-ota-partitions.md` |

The maintainer named MQTT as the first target in
[#248 comment 3843927598](https://github.com/gkoh/furble/issues/248#issuecomment-3843927598).
This section stays inside that choice and only deepens the Home Assistant
half of it.

## Delivery

The hub-level entity model ships inside PR33c as already scoped. The
per-camera device layer and the location command topic are a small follow-up
PR after PR33c, because they depend on the stable camera ids from plan 51 and
should not hold the first MQTT release hostage to that migration. Verification
extends the PR33c list: confirm one HA device per saved camera with working
availability, confirm deleting a camera removes its device via an empty
retained discovery payload, and confirm a pushed location reaches a connected
camera's geotag within one update cycle.

---

# Critical risk: BLE and WiFi coexistence

This is the section that decides whether this whole document is a good idea.

## The hardware constraint

ESP32 and ESP32-S3 have one 2.4 GHz radio. The ESP-IDF RF coexistence guide
states it plainly: each board has only one 2.4 GHz ISM band RF module, shared by
two or three protocol modules, so packet transmission and reception is managed
by time division multiplexing.

There is no second antenna and no second PHY. Every millisecond WiFi spends
transmitting is a millisecond BLE cannot. The camera link and the MQTT link are
in direct competition for the same resource, always.

## What is already configured

Verified in the repo. Software coexistence is already enabled in all five
committed sdkconfigs:

| File | Line | Setting |
|---|---|---|
| `sdkconfig.m5stick-c` | 843, 844 | `CONFIG_ESP_COEX_ENABLED=y`, `CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y` |
| `sdkconfig.m5stick-c-plus` | 843, 844 | same |
| `sdkconfig.m5stack-core` | 843, 844 | same |
| `sdkconfig.m5stack-core2` | 843, 844 | same |
| `sdkconfig.m5stick-s3` | 1022, 1023 | same |

The four ESP32 boards additionally carry the deprecated alias
`CONFIG_SW_COEXIST_ENABLE=y` at line 2666, and
`CONFIG_BTDM_CTRL_FULL_SCAN_SUPPORTED=y` at `sdkconfig.m5stick-c-plus:671`,
which the ESP32 coexistence guide names as the fix for BLE scan windows being
cut short by WiFi.

This matters because the ESP-IDF guide says the coexistence option must be
enabled through menuconfig after writing a coexistence program. It already is.
No sdkconfig change is needed to turn coexistence on, which removes the largest
build system risk from this work. PR00's rule that committed sdkconfigs stay
untouched can hold.

## What the support matrix says

For ESP32, the coexistence table marks WiFi STA against every BLE state, scan,
advertising, connecting and connected, as supported with stable performance.

For ESP32-S3, WiFi STA with BLE is supported. The cells marked as supported but
with unstable performance are the SoftAP connecting and connected rows. furble
will never run SoftAP under this plan, which puts every use case here inside the
stable quadrant on both chip families.

That is the good news. It is not the same as saying there is no cost.

## Where the cost actually lands

The link is unlikely to drop. furble's connection parameters are already
generous, at `lib/furble/Camera.h:180-185`:

- min interval `BLE_GAP_INITIAL_CONN_ITVL_MIN`, which is 30 ms
- max interval `BLE_GAP_INITIAL_CONN_ITVL_MAX`, which is 50 ms
- slave latency 1, one packet may be skipped
- supervision timeout `2 * BLE_GAP_INITIAL_SUPERVISION_TIMEOUT`, which is
  2 times 0x0100 units of 10 ms, so 5120 ms

A supervision timeout of 5.12 seconds against a 50 ms connection interval means
roughly 100 consecutive connection events can be missed before the link drops.
WiFi does not hold the radio for five seconds. The link survives.

What does not survive unharmed is latency. A shutter command travels through
`Control::sendCommand` (`src/FurbleControl.cpp:183-185`) to a per camera task,
then waits for the next connection event. With a 30 to 50 ms interval and a
contended radio, that wait can stretch by several intervals. A photographer
pressing a button on a phone dashboard will not notice 100 ms. A photographer
timing a shot to a moment will.

The worst case is not steady state MQTT traffic. It is a WiFi scan. An active
scan sweeps fourteen channels with a dwell time on each. During it, WiFi is
given extended radio allocation by the coexistence scheduler, exactly as the
ESP-IDF policy describes: when WiFi is scanning or connecting it takes extended
allocation and Bluetooth time slices are adjusted down.

## Mitigations, in order of value

1. **Never scan while a camera is connected.** PR33b stores BSSID and channel
   and connects directly. Direct connect touches one channel instead of
   fourteen. If a scan is genuinely needed, refuse it while `Control::getState()`
   is `STATE_ACTIVE` and tell the user why.
2. **Never run OTA while a camera is connected.** A firmware download saturates
   the radio for a minute or more. `plans/34-ota-partitions.md` makes this a
   hard precondition, not advice.
3. **Keep the existing connection parameters.** They are already coexistence
   friendly and nothing in this document should change them. If
   `plans/10-adaptive-conn-params.md` lands and shortens the interval while
   shooting, re-run the latency test with WiFi up before accepting it.
4. **`esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`.** Station sleeps between DTIM
   beacons, which is radio time returned to BLE.
5. **Publish on change, not on a fast timer.** The cadence rules in PR33c exist
   for this reason, not to save bytes.
6. **Do not use `esp_coex_preference_set`.** It exists in IDF 5.4 at
   `components/esp_coex/include/esp_coexist.h:126` and takes
   `ESP_COEX_PREFER_BT`, which looks like exactly the knob this problem wants.
   It is marked deprecated in that same header, which says to use
   `esp_coex_status_bit_set()` and `esp_coex_status_bit_clear()` instead. Do not
   build a feature on a deprecated API. If measurement shows the default balance
   is not good enough, raise it upstream with numbers rather than reaching for
   the deprecated call.

## Testing plan

This is a measurement, not an opinion. The console from
`plans/27-usb-console.md` is what makes it possible, which is another reason
the maintainer's ordering is correct.

Host script drives `shutter hold 200` 200 times at 2 second intervals, and
timestamps the command send and the observed camera response. Report the
distribution, not the mean: p50, p95, p99 and max.

Run the same 200 shot sequence in six states:

| State | What it isolates |
|---|---|
| WiFi off, master build | the baseline |
| WiFi off, this branch | proves the code costs nothing when disabled |
| WiFi associated, idle, no MQTT | the cost of association alone |
| WiFi associated, MQTT connected, no traffic | the cost of a live TCP session |
| WiFi associated, MQTT publishing at the PR33c cadence | steady state |
| WiFi scanning during the sequence | the worst case, deliberately provoked |

Also record, in every state: BLE disconnect count, `esp_get_free_heap_size()`
minimum, and idle current.

Acceptance: states 1 and 2 must be indistinguishable. States 3 to 5 must show no
BLE disconnects over 200 shots and a p99 latency increase that is stated as a
number in the PR body, whatever it is. State 6 is expected to be bad and is
measured so that the "never scan while connected" rule has evidence behind it
rather than caution.

Hardware is Fujifilm on the StickS3. Other vendors get code review only, as the
roadmap index requires, and the PR body must say so. Nothing here is vendor
specific, since all of it lands above `Control::cmd_t`.

---

# Flash and RAM budget

## Current measured state

From `.pio/build/*/firmware.bin` at commit `2b79ce8`:

| Environment | App image | Flash size |
|---|---|---|
| `m5stick-c` | 1,001,584 | 4 MB |
| `m5stick-c-plus` | 1,002,240 | 4 MB |
| `m5stack-core` | 1,037,616 | 4 MB |
| `m5stack-core2` | 1,037,728 | 16 MB |
| `m5stick-s3` | 1,034,256 | 8 MB |

S3 section breakdown, from `xtensa-esp32s3-elf-size -A`:

```
.iram0.text     103,811
.dram0.data      15,188
.dram0.bss       18,976
.flash.text     691,826
.flash.rodata   221,916
```

Static DRAM in use before any heap allocation is roughly 34 KB. NimBLE with
`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=9` (`sdkconfig.m5stick-s3:635`) and LVGL take
the rest at runtime.

## What WiFi adds

Do not guess. Measure with `pio run -t size` and report it. What is known:

- The precompiled WiFi blobs in the IDF tree are large. On ESP32-S3,
  `libnet80211.a` is 1.35 MB and `libpp.a` is 682 KB on disk. The linker takes a
  subset, but that subset is the dominant new cost, and it is measured in
  hundreds of kilobytes, not tens.
- lwIP, `esp_netif` and `esp_event` add on top of that.
- TLS is the item that can be traded. `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` and
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y` are already set
  (`sdkconfig.m5stick-s3:1998-1999`), with
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS=200` at line 2004. The source
  bundle `cacrt_all.pem` is 235 KB on disk. If TLS is needed and the image does
  not fit, switching the bundle from full to common is the first lever, and it
  is a committed sdkconfig change that has to be argued separately.

## What WiFi costs in RAM

- `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10`,
  `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32` and
  `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32`
  (`sdkconfig.m5stick-s3:1527-1532`) are the buffer pools. They are allocated at
  `esp_wifi_init()` and freed at `esp_wifi_deinit()`, which is why PR33b insists
  on full deinit rather than disconnect.
- `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` and
  `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096`
  (`sdkconfig.m5stick-c-plus:1770-1771`) mean a single TLS session peaks at
  roughly 20 KB of buffers on top of the handshake working set.

That last one is the real hazard, and it is worst on the ESP32 boards, not the
S3. `m5stick-c` is an ESP32 with 320 KB of DRAM, no PSRAM, already running
NimBLE with nine connection slots and LVGL. Adding WiFi buffers plus a 20 KB TLS
session on top may simply not fit.

Position: PR33b and PR33c target the S3 and the headless environment. Building
them for `m5stick-c` is not a goal and should not block the PR. State that
explicitly in the PR body, measure the free heap on the S3 with everything
running, and let the ESP32 boards be a follow up with evidence rather than an
assumption in either direction.

## Interaction with plans/34

`plans/34-ota-partitions.md` sizes OTA app slots at 1700K, which is 1,740,800
bytes. Against the current largest image of 1,037,728 bytes that is 686 KB of
headroom. WiFi plus lwIP plus MQTT plus TLS plus the certificate bundle is
plausibly most of it.

These two documents are budget coupled. Neither can be finalised without the
other's measured number. The correct order is: land 34a first so the slot size
is fixed and the size check is enforced by the build, then measure each of 33b
and 33c against it. If WiFi does not fit alongside OTA on a 4 MB board, that is
a real finding and the answer is a reduced feature build for those boards, not a
smaller OTA slot.

---

# Considered and rejected

**SoftAP provisioning.** furble raises its own access point, the user joins it
from a phone and types the SSID. Rejected because it needs a captive portal and
an HTTP server, which is most of the WebUI cost that PR33d defers, and because
the ESP32-S3 coexistence table marks SoftAP with BLE as unstable. The console
already exists and the cable is already plugged in.

**Improv over BLE or serial.** ESP Web Tools supports Improv and the existing
web installer manifest already sets `new_install_improv_wait_time: 0`. Rejected
for now because Improv over BLE means advertising a provisioning service, and
`5564b73` removed BLE advertising from furble deliberately. Worth revisiting as
Improv over serial only, after the console lands, since it would reuse the same
transport for zero new radio surface.

**WiFi on by default when credentials exist.** Rejected. The device is
battery powered by default and the cost is roughly half the runtime. An explicit
`WIFI` enable makes the trade visible.

**A single `NETWORK` setting instead of separate WiFi, NTP and MQTT enables.**
Rejected. Three independent features with three independent costs. A user who
wants NTP once a day should not have to run an MQTT client.

**MQTT over TLS as the only option.** Rejected as a starting position. TLS may
not fit in the app slot budget alongside OTA. Ship plain MQTT, which is what a
LAN broker on a studio network normally speaks, and add TLS when the size is
known.

**Publishing camera state as individual topics per field.** Rejected. Six
topics per camera per update multiplies radio time on a shared radio for no
gain. JSON with a Home Assistant value template does the same job in one
publish.

**A separate `esp_timer` publishing task.** Rejected. The MQTT event task and
the existing control task are enough. Another task is another stack and another
priority interaction with the per camera tasks at priority 3
(`src/FurbleControl.cpp:257`) and the control task at priority 4
(`src/main.cpp:32`).

---

# Dependencies

```
plans/27 (console)  -> 33a runtime toggle, 33b, 33c
33a                 -> 33b, 33c
33b                 -> 33c
33b                 -> plans/34 PR34a (esp_https_ota needs a network)
plans/00b           -> plans/27 -> everything here
plans/09 (backoff)  -> 33b reuses the shape, not the code
plans/02 (battery)  -> 33b wifi status current readout, 33c battery topic
plans/05 (diag)     -> optional read-only WiFi status page
plans/12 (display off) -> 33a true display off in CONSOLE mode
plans/34            -> budget coupled with 33b and 33c, land 34a first
plans/90            -> 33b NTP is a second time source for the same path
```

`plans/27` is a hard dependency on everything here. There is no other way to
type an SSID.

`plans/40-thinknode-port.md` is a feasibility study for running furble headless
on an Elecrow ThinkNode as a GPS sidecar. It reaches the same conclusion PR33a
does from a different direction, that `FurbleUI` and its 2136 lines of LVGL are
simply dropped for a device with no screen. The two are not the same work.
PR33a is a build profile for an existing supported SoC and it is proposed here.
`plans/40` is a port to a different vendor's hardware with a different radio
stack and a different provisioning story, and it is a study, not a proposal.
The relationship is one directional: if `plans/40` ever becomes real work, it
should build on PR33a's `FURBLE_NO_DISPLAY` gate and the headless main loop
rather than inventing a second one. Nothing in PR33a should be shaped around
ThinkNode. Note that `plans/40` is not in the plans branch at the time of
writing, so treat this cross reference as provisional.

---

# References

All fetched and verified.

- ESP-IDF v5.4, RF coexistence, ESP32-S3. Support matrix for WiFi STA with BLE,
  the time division policy, and `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/coexist.html
- ESP-IDF v5.4, RF coexistence, ESP32. The single shared 2.4 GHz RF module
  statement, the stable WiFi STA with BLE matrix, and
  `CONFIG_BTDM_CTRL_FULL_SCAN_SUPPORTED`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-guides/coexist.html
- ESP-IDF v5.4, WiFi driver API. `esp_wifi_init`, `esp_wifi_set_config`,
  `esp_wifi_connect`, `esp_wifi_set_ps`, `wifi_config_t.sta.bssid` and
  `channel`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/network/esp_wifi.html
- ESP-IDF v5.4, WiFi driver guide. Station lifecycle, scan behavior and events:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/wifi.html
- ESP-IDF v5.4, low power mode in WiFi scenarios. Modem-sleep DTIM1 40.1 mA,
  DTIM3 38.7 mA, DTIM10 38.2 mA, and the DTIM1 160 MHz DFS case at 113.5 mA max
  and 15.0 mA min, all measured in a shielded box. `WIFI_PS_NONE`,
  `WIFI_PS_MIN_MODEM`, `WIFI_PS_MAX_MODEM`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/low-power-mode/low-power-mode-wifi.html
- ESP-IDF v5.4, ESP-MQTT. `esp_mqtt_client_config_t` with `broker.address.uri`,
  `credentials`, `session.last_will`, `session.keepalive`,
  `session.disable_clean_session`, `network.reconnect_timeout_ms`, all three QoS
  levels, automatic reconnect, and the `MQTT_EVENT_*` names:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/mqtt.html
- ESP-IDF v5.4, system time. `esp_netif_sntp_init`,
  `ESP_NETIF_SNTP_DEFAULT_CONFIG`, `esp_netif_sntp_sync_wait`,
  `SNTP_SYNC_MODE_IMMED` versus `SNTP_SYNC_MODE_SMOOTH`, and
  `CONFIG_LWIP_SNTP_UPDATE_DELAY` defaulting to one hour:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/system_time.html
- ESP-IDF v5.4, console. The REPL used by `plans/27`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/console.html
- Home Assistant, MQTT integration. Discovery topic
  `<discovery_prefix>/<component>/[<node_id>/]<object_id>/config`, the device
  form `<discovery_prefix>/device/<object_id>/config` requiring `dev` and `o`,
  the `unique_id` and `availability_topic` requirements, retained discovery and
  its downside, and the `homeassistant/status` birth and will topic:
  https://www.home-assistant.io/integrations/mqtt/
- gkoh/furble issue 248, WIFI feature. The WebUI, MQTT, REST, NTP, location
  parser and OTA list, and the maintainer comment naming MQTT as the first
  target:
  https://github.com/gkoh/furble/issues/248
- gkoh/furble issue 249, Board-Only support. The ESP32-S3 studio use case and
  the maintainer's four stage plan:
  https://github.com/gkoh/furble/issues/249
- PlatformIO, Espressif 32 platform. Board definitions and `board_build`
  options:
  https://docs.platformio.org/en/latest/platforms/espressif32.html

## Hardware verification, pass 3, 2026-08-18

Verdict: PARTIAL, blocked on device access.

Build evidence:

- The `esp32-s3-headless` env compiles from this branch (fork/feat/33-wifi-hub).
  As plans/33 predicted, the headless profile still pulls and compiles LVGL even
  though it never renders, confirming the noted size trade off.

On-device checks not run this pass, blocked:

- The M5StickS3 was bricked during the combined image camera walk (a disconnect
  during connect hang de-enumerated USB, see plans/25). Recovery needs the
  physical rescue, hold the side button while replugging USB until the green LED
  flashes, then reflash. The headless image was therefore not flashed.

Owed on the user checklist, once the device is recovered and the headless image
is flashed:

- Boot without a display and confirm the console comes up.
- `settings set display_mode console` and `settings set display_mode gui`
  toggle, confirm the display stops and starts.
- The console `pair` command responds.
- With the GPS unit attached, confirm geotag still pushes to a connected camera.
