# PR28: host simulator for the furble UI

## Goal

Run the furble user interface natively on macOS and Linux, in a window, driven
by the keyboard, with no ESP32 attached. Use it to iterate on menus and layout
in seconds instead of minutes, and to catch layout regressions in CI with
screenshot comparison.

All line anchors below were read at commit `2b79ce8` on `master`.

## Motivation

Every UI change in furble costs a full firmware build, a flash cycle and a walk
through the menu tree with three buttons. That is roughly a minute per
iteration on a good day. Layout work is the worst case, because the interesting
question is usually "does this still fit on a 135x240 screen", and the only way
to answer it is to look at the device.

There are five shipping board geometries. A developer has one or two of them.
Changes to shared UI code are checked on the hardware in reach and reasoned
about for the rest. `src/FurbleUI.cpp` is 2136 lines and every plan in this
series that touches the UI adds more.

There is also no automated test of any kind for the UI. CI builds five
firmware images and runs clang-format. Nothing checks that a menu still renders,
that a page still fits, or that a widget did not move.

A host build fixes all three. It gives sub-second iteration, it renders every
board geometry from one machine, and it makes screenshots a CI artifact.

## Draft issue

Open this before any code. Motivation only, no design.

> **No way to see UI changes without flashing hardware**
>
> Working on the menus means a build and flash cycle for every change, then
> navigating there with the buttons to look at it. Layout questions on the
> narrow 135x240 screens need a device in hand, and there are five different
> screen geometries across the supported boards, so most of them get checked by
> reasoning rather than by looking. There is also nothing in CI that would
> notice if a UI change broke a page on a board nobody tested.
>
> M5GFX and M5Unified already support building for the desktop against SDL2,
> including a M5StickS3 window at the correct 135x240 size, and LVGL is
> portable. It looks like the furble UI could be built and run on a PC with a
> fake camera and fake GPS behind it. Would a developer-only host build be
> welcome, kept out of the shipping environments?

## Options

### Option 1: LVGL host simulator on SDL2

Build `src/FurbleUI.cpp` and its neighbours for the host, render into an SDL2
window, drive input from the keyboard, and put fakes behind the camera, GPS and
platform layers.

The important finding is that this does not need a hand written LVGL SDL port.
M5GFX and M5Unified already ship a desktop build path, and furble already goes
through it.

What already exists, verified against the versions pinned in
`platformio.ini:16-18`:

- M5GFX 0.2.19 contains `src/lgfx/v1/platforms/sdl/Panel_sdl.hpp`. Its SDL
  autodetect path handles `board_M5StickS3` and sizes the window 135x240,
  the same as the real panel. It also handles `board_M5Stack`,
  `board_M5StackCore2`, `board_M5StickC` and `board_M5StickCPlus`, which covers
  all five shipping environments.
- M5Unified 0.2.13 defines `M5UNIFIED_PC_BUILD` in
  `src/utility/m5unified_common.h` whenever `SDL.h` is present and
  `ESP_PLATFORM` is not. Under that macro it shims `esp_err_t`,
  `esp_log_level_t`, `i2c_port_t` and friends, and it maps the arrow keys onto
  the buttons: LEFT to `BtnA`, DOWN to `BtnB`, RIGHT to `BtnC`, UP to `BtnPWR`.
- M5Unified ships `examples/PlatformIO_SDL` with a working `platform = native`
  PlatformIO environment, including the Homebrew include and library paths
  needed on Intel and Apple Silicon macs.
- LVGL 9.4.0 is already vendored at `managed_components/lvgl__lvgl`. That tree
  contains `src/drivers/sdl` and `src/others/snapshot`. Nothing needs
  downloading.

How much of furble compiles host side. The M5 surface is small and it is
concentrated. `src/FurbleUI.cpp` has 39 references to `M5.`, across about
fifteen distinct calls: `M5.Display.width/height/setBrightness/pushImageDMA/
setTouchCalibrate`, `M5.getBoard()`, `M5.Power.getBatteryLevel/
getBatteryCurrent`, `M5.Touch.isEnabled/getCount/getDetail`, and
`M5.BtnPWR/BtnA/BtnB/BtnC` with `isPressed/isReleased/wasDoubleClicked`. Every
one of those exists in the M5Unified PC build. The display flush at
`src/FurbleUI.cpp:371-378` calls `M5.Display.pushImageDMA<uint16_t>`, which
`Panel_sdl` implements. So the display and the buttons need no furble change at
all.

The ESP-IDF surface in `src/` is also small. Across the whole directory it is
`xTaskCreate`, `vTaskDelay`, `xQueueCreate`, `xQueueSend`, `xQueueReceive`,
`xQueueReset`, `vTaskDelete`, `pdMS_TO_TICKS`, plus `esp_timer_get_time`,
`esp_restart`, `esp_pm_configure`, `esp_sleep_disable_wakeup_source`,
`heap_caps_aligned_alloc` and the `esp_power_level_t` enum. That is a shim of
roughly 200 lines over `std::thread`, a condition variable queue and the host
clock. The FreeRTOS POSIX port is an alternative and is more faithful, but it
is heavier and its macOS story is weaker. The thin shim is the better trade
here because furble uses six FreeRTOS calls.

What actually needs work is the BLE seam. `include/FurbleControl.h:7` includes
`Camera.h`, which includes `NimBLEAddress.h`, `NimBLEClient.h` and
`NimBLEDevice.h`. `lib/furble/Scan.h` includes `NimBLEScan.h`. So the UI cannot
even parse its own headers without NimBLE present. There are two ways out:

- Shim all of NimBLE and compile the whole of `lib/furble`. The vendor code
  uses `NimBLEUUID` 134 times, `NimBLERemoteCharacteristic` 52 times,
  `NimBLEAdvertisedDevice` 52 times, and eight other classes. That is 400 to
  600 lines of stub headers that do nothing, and they break every time a vendor
  class reaches for a new API.
- Shim only enough NimBLE for the headers to parse, and do not compile
  `lib/furble` at all. The UI only touches fifteen library methods:
  `Control::getInstance/sendCommand/getState/connectAll/disconnect/addActive/
  getConnectingCamera/setPower`, `CameraList::load/size/get/last/save/remove/
  clear/getSaveCount/addFauxNY`, and `Camera::getName/isActive/setActive/
  getConnectProgress`. A host implementation of that set is a few hundred lines
  and can be made to do useful things, like park in `STATE_CONNECT_FAILED` on
  demand. The NimBLE stub shrinks to declarations only, roughly 200 lines.

The second is clearly right. It also means the host build needs no refactor of
the shipping sources. A thin platform abstraction layer would be nicer, but it
is not a precondition, and proposing a refactor of `Camera.h` as the price of
admission for a dev tool is the wrong order.

The remaining gaps are all small and all solvable with shim headers on the host
include path, with no edit to any shipping file:

- `include/FurblePlatform.h:4` includes `M5PM1.h` unconditionally, and the
  class holds an `M5PM1 m_M5PM1` member. A stub `M5PM1.h` with the four methods
  used at `src/FurblePlatform.cpp:34-37` and `src/FurblePlatform.cpp:74` costs
  about 20 lines.
- `include/FurbleGPS.h:4` includes `driver/uart.h` and holds a
  `uart_port_t m_UART`. A stub `driver/uart.h` with the typedef and
  `UART_NUM_2` costs about 10 lines. `src/FurbleGPS.cpp` itself is replaced
  host side by a version that feeds `TinyGPSPlus` from a recorded NMEA file.
  TinyGPSPlus is plain C++ and builds on the host as is.
- `lib/preferences/Preferences.cpp` is 344 lines over NVS. The header is 75
  lines. A host implementation over a `std::map` persisted to a file is around
  150 lines, and it is a feature rather than a cost, because it makes the
  settings state scriptable.

What it buys:

- Menu and layout iteration measured in seconds.
- All five board geometries from one machine, by changing one build flag.
- Deterministic screenshots. `src/FurbleUI.cpp:83` already calls
  `lv_tick_set_cb(tick)` and `src/FurbleUI.cpp:380-382` routes that to
  `Platform::tick()`. The host build controls `Platform::tick()`, so it controls
  LVGL's clock. That makes animations and the 250 ms icon timer reproducible,
  which is what a golden image test needs.
- A place to reach UI states that are painful on hardware: connect failed,
  infinite reconnect, low battery, GPS with no fix, intervalometer mid run.

What it cannot test: the BLE stack and every vendor camera protocol, real GPS
hardware and UART behaviour, power and sleep, the M5PM1, the IMU, flash and NVS
wear, timing under real task scheduling, and anything to do with the actual
panel driver. Those stay on hardware. The simulator is a UI tool and should be
sold as one.

**Verdict: recommended.** The M5Unified PC build removes the hard part. The
remaining work is a shim layer and a fake camera layer, both of which are
bounded and both of which live in one new directory.

### Option 2: QEMU with Espressif's ESP32-S3 fork

Espressif maintains a QEMU fork with an ESP32-S3 machine, wired into
`idf.py qemu`. The question is whether the furble firmware can boot in it.

What the S3 machine actually models, read from
`hw/xtensa/esp32s3.c` on the `esp-develop` branch:

- CPU, memory, cache, PSRAM up to 32 MB including octal PSRAM, SPI flash on
  SPI1, eFuse, GPIO with strapping, UART, timer groups, systimer, RNG, GDMA,
  SHA, AES, RSA, HMAC, DS, XTS-AES, TWAI, SD/MMC, OpenCores ethernet, and a
  USB-Serial/JTAG device.
- A virtual RGB panel (`ESPRgbState rgb`, `hw/display/esp_rgb.h`), reached with
  `idf.py qemu --graphics` and the `espressif/esp_lcd_qemu_rgb` component.

What it does not model, and this is where furble dies:

- No I2C controller. `esp32.c` instantiates `TYPE_ESP32_I2C`. `esp32s3.c` only
  carries a comment referring to the ESP32 function. So `M5.In_I2C` has nothing
  behind it. `src/FurblePlatform.cpp:34` calls `m_M5PM1.begin(&M5.In_I2C)` on
  the S3, and M5Unified's own board detection leans on I2C probing. `M5.begin()`
  at `src/FurblePlatform.cpp:26` will not produce a StickS3.
- No SPI2 or SPI3 master. Only `spi1` exists, and it is there for the flash.
  There is no way to drive an ST7789. The virtual RGB panel is the only display,
  and it is a device that does not exist on real silicon, so using it means
  writing a QEMU-only display path that no hardware ever runs.
- No BLE controller. There is no Bluetooth device in the machine at all.
  `Furble::Device::init()` at `src/main.cpp:29` calls into NimBLE, which
  initialises the controller. That fails.
- No IMU, no PMIC, no AXP. All of those are I2C anyway.

To get a usable boot you would need a QEMU-specific build that skips
`M5.begin()`, skips NimBLE, and routes the LVGL flush at
`src/FurbleUI.cpp:371-378` to `esp_lcd_qemu_rgb` instead of
`M5.Display.pushImageDMA`. That is the same abstraction layer Option 1 needs,
built a second time, on an emulator that renders a display furble does not have,
and it still cannot test BLE. Estimated two days minimum, with a permanent
maintenance cost.

The one genuinely useful thing QEMU offers is the emulated USB-Serial/JTAG
device. If the firmware booted, the PR27 console would work over it, which would
allow scripted UI-free tests. But the firmware does not boot, for the reasons
above, so this is hypothetical.

There is a narrower use that costs almost nothing: a CI job that boots the built
image far enough to reach `app_main` and prints the version line at
`src/main.cpp:25`, as a smoke test that the image is not corrupt and the
partition table is sane. That is worth maybe an hour and catches a real if rare
class of bug. It is not a UI story.

**Verdict: not recommended for UI work.** Optional later as a boot smoke test
only. The missing I2C controller on the S3 machine is fatal for a board whose
entire personality is on I2C.

### Option 3: Wokwi

Wokwi simulates the ESP32 family in the browser and has a CLI for CI.

- ESP32-S3 is supported, including a `psramSize` attribute, custom partition
  tables in ESP-IDF format, and USB CDC via a `serialInterface` chip attribute.
- The CLI is genuinely good for CI. `wokwi-cli <dir>` runs headless, takes
  `--expect-text` and `--fail-text` against serial output, supports scenario
  files, and can capture screenshots with `--screenshot-time`,
  `--screenshot-part` and `--screenshot-file`.
- The ESP32 peripheral table marks Bluetooth as not implemented, with a bare
  cross in every chip column. I2C is master only. USB Serial and JTAG is listed
  as serial supported, JTAG not.

BLE being absent removes the only thing furble does. Beyond that, there is no
first party ST7789 part. Community custom chips exist and the pattern is a
`.chip.c` plus a `.chip.json` plus wiring in `diagram.json`, but a StickS3 rig
would need a custom ST7789 chip, a custom M5PM1 chip, a custom BMI270 chip and
a board definition, all written in a C-to-WASM custom chip API with no
relationship to anything else in this project. Call it a week to get something
that renders, and it would still be a display simulation, not a BLE one.

Licensing is the other problem. The Community plan is free and covers unlimited
public simulations, and Wokwi does advertise a free licence for open source
projects. But CI minutes are a paid line item: the pricing page lists CI minutes
only on the Pro plan at 2000 per month, while the CI getting started page
mentions a 50 minute free tier. The two pages disagree, which by itself is a
reason not to build a CI gate on it. CI also means uploading firmware to a third
party service on every push.

**Verdict: no.** No BLE, no display part, a week of custom chip work, and an
unclear paid dependency for the CI use that was the main attraction. Wokwi is
built for WiFi and Arduino teaching. Furble is a BLE remote with a hand rolled
LVGL UI. Wrong tool.

### Option 4: hardware console plus framebuffer dump

Keep hardware as the test rig, and make it scriptable. This is the tier that
already has a plan: PR27 adds an `esp_console` REPL over USB-Serial/JTAG, and
the `FAUXNY` setting at `src/FurbleSettings.cpp:20` plus
`lib/furble/FauxNY.cpp` already give a fake camera that connects, fires and
disconnects with no real hardware in the room.

The new idea here is a screenshot command over that console. It is cheaper than
it sounds, and it does not need `LV_USE_SNAPSHOT`.

`src/FurbleUI.cpp:371-378` is the flush callback. It receives every rendered
region as an `lv_area_t` plus an RGB565 buffer, already byte swapped by
`lv_draw_sw_rgb565_swap` on line 375. A debug build sets a capture flag from a
console command, and the flush callback then writes each region to the console
as a small header plus base64 payload before pushing it to the panel. The
command follows up with `lv_obj_invalidate(lv_screen_active())` to force a full
repaint so every region is emitted. The host script reassembles the regions into
a full frame, unswaps the bytes and writes a PNG.

Cost and size. A StickS3 frame is 135 x 240 x 2, which is 64800 bytes, or about
86 KB base64. Over USB-Serial/JTAG that is well under a second. Zero extra RAM,
because the existing partial render buffers at `src/FurbleUI.cpp:125-126` are
reused. Zero LVGL config change. Roughly 80 lines of firmware and 60 lines of
host Python.

The `lv_snapshot_take` route is the alternative. LVGL 9.4 ships
`src/others/snapshot`, but `sdkconfig.m5stick-s3:2755` has
`CONFIG_LV_USE_SNAPSHOT` unset, and `CONFIG_LV_CONF_SKIP=y` at line 2426 means
LVGL takes its configuration from Kconfig. `lv_conf_internal.h:3290-3296` guards
with `#ifndef LV_USE_SNAPSHOT` before consulting `CONFIG_LV_USE_SNAPSHOT`, so a
`-DLV_USE_SNAPSHOT=1` build flag should win, provided the flag actually reaches
the LVGL component's compilation and not just furble's own sources. That needs
checking before relying on it. Snapshot also allocates a full frame buffer at
call time, which the flush hook does not. Prefer the flush hook.

What this tier buys: hardware truth, real BLE, real GPS, real power, scripted
sequences, and a screenshot from a board the developer owns. What it cannot
buy: speed. It is still a build and flash cycle, and it needs a device per
geometry.

**Verdict: yes, as the complement, not the primary.** It answers "does it work"
where the simulator answers "does it look right". It also provides the reference
images that prove the simulator is not lying.

## Recommended plan

Option 1 as the primary, Option 4 as the parallel hardware track. Option 2 as an
optional smoke test much later. Option 3 dropped.

Everything lives under one new top level `sim/` directory. No shipping source
file changes. No change to the five shipping PlatformIO environments. No change
to any committed `sdkconfig.*`.

### Phase A: host build boots the UI

Scope. A `platform = native` PlatformIO environment that builds the furble UI
against M5Unified's PC build and shows it in an SDL2 window.

Files, all new:

- `sim/platformio.ini` or a new `[env:sim]` block in the existing
  `platformio.ini`. `platform = native`, no `framework`, SDL2 flags copied from
  M5Unified's `examples/PlatformIO_SDL/platformio.ini` including the Homebrew
  prefix handling, `-DM5GFX_BOARD=board_M5StickS3`, `-DM5GFX_SCALE=2`,
  `-DFURBLE_M5STICKS3`, `-DFURBLE_SIM`.
- `sim/main.cpp`. The `lgfx::Panel_sdl::main(user_func, ...)` entry point from
  M5Unified's `sdl_main.cpp`, calling the same sequence as `src/main.cpp:21-40`.
- `sim/shim/` on the include path: `M5PM1.h`, `driver/uart.h`,
  `esp_pm.h`, `esp_sleep.h`, `esp_bt.h`, `esp_random.h`, `esp_mac.h`,
  `esp_timer.h`, `esp_heap_caps.h`, `esp_log.h`, `nvs_flash.h`,
  `freertos/FreeRTOS.h` and `freertos/task.h`. Roughly 300 lines in total,
  mostly typedefs and no-ops. `heap_caps_aligned_alloc` maps to
  `std::aligned_alloc` for `src/FurbleUI.cpp:125-126`.
- `sim/shim/nimble/`. Declaration-only `NimBLEAddress.h`, `NimBLEClient.h`,
  `NimBLEDevice.h`, `NimBLEScan.h`, `NimBLEUUID.h`, `NimBLEConnInfo.h`,
  `NimBLEAdvertisedDevice.h`, `NimBLERemoteCharacteristic.h`,
  `NimBLERemoteService.h`, `NimBLEAttValue.h`. Enough for `Camera.h`,
  `CameraList.h`, `Scan.h`, `Device.h` and `FurbleControl.h` to parse. Roughly
  200 lines.
- `sim/freertos.cpp`. Task and queue shim over `std::thread`,
  `std::mutex` and `std::condition_variable`. Roughly 200 lines.
- `sim/FurblePlatformSim.cpp`. Replaces `src/FurblePlatform.cpp`. `tick()`
  returns a virtual clock. `update()` calls `M5.update()`. `powerOff()` exits.
- `sim/FurbleControlSim.cpp`, `sim/CameraSim.cpp`, `sim/CameraListSim.cpp`.
  Implement the fifteen methods the UI uses, backed by a list of FauxNY style
  fake cameras, with a settable connect outcome and connect progress ramp.
- `sim/FurbleGPSSim.cpp`. Replaces `src/FurbleGPS.cpp`. Feeds the real
  `TinyGPSPlus` from a recorded NMEA file, or from nothing for the no-fix case.
- `sim/PreferencesSim.cpp`. Replaces `lib/preferences/Preferences.cpp`. Backed
  by a file so settings survive a restart and can be seeded by a test.
- `sim/lv_conf.h` plus `tools/gen_lv_conf.py`. The host build cannot use
  Kconfig, so it needs an `lv_conf.h`. Generate it from `sdkconfig.m5stick-s3`,
  which carries 72 `CONFIG_LV_*` entries. Generating rather than hand writing is
  the whole point, because a hand written copy drifts.

Compiled from `src/`: `FurbleUI.cpp`, `FurbleUIIntervalometer.cpp`,
`FurbleSpinValue.cpp`, `FurbleCalibrate.cpp`, `FurbleSettings.cpp`. Not
compiled: `main.cpp`, `FurblePlatform.cpp`, `FurbleGPS.cpp`,
`FurbleControl.cpp`, and all of `lib/furble`.

Effort: two to four days. Roughly 1200 lines of new code, none of it clever.

Unblocks: sub-second UI iteration. Layout checks on all five geometries.
Screenshots for PR descriptions. Makes PR03, PR05, PR12 and PR25 much cheaper
to build and to review.

Risk to retire first, before writing anything else: confirm that M5Unified
0.2.13 and M5GFX 0.2.19 as pinned actually build and run under
`platform = native` on the developer's machine. Build M5Unified's own
`examples/PlatformIO_SDL` with `-DM5GFX_BOARD=board_M5StickS3` first. That is a
thirty minute experiment and it decides the whole plan.

### Phase B: deterministic clock, scripted input, screenshots

Scope. Make the simulator reproducible and make it produce PNGs.

- `sim/FurblePlatformSim.cpp` gains a virtual clock that only advances when the
  driver says so. `src/FurbleUI.cpp:83` already routes LVGL's tick through
  `UI::tick`, so this needs no furble change.
- `sim/driver.cpp`. Reads a script of steps: press a key, advance the clock,
  capture. Uses `lv_indev` injection or synthetic SDL events.
- `sim/capture.cpp`. Reads back the `Panel_sdl` surface and writes a PNG.
- `sim/scripts/*.txt`. One script per page worth capturing. Start with the main
  menu, the Settings tree, the Connected page, the GPS Data page and the
  intervalometer pages.

Effort: one to two days.

Unblocks: golden images. A before and after pair for any UI PR.

### Phase C: CI job

Scope. A sixth matrix entry, or a separate job, that builds the sim and runs the
scripts headless. SDL2 supports a dummy video driver, and the `format` and
`build` jobs in `.github/workflows/main.yml` already give the shape.

- `.github/workflows/main.yml`. New `sim` job. `apt install libsdl2-dev`,
  `platformio run -e sim`, run the scripts, compare against
  `sim/golden/*.png`, upload the diffs as artifacts.

Effort: half a day.

Unblocks: automatic detection of layout regressions on board geometries nobody
owns. Start with the comparison as a soft failure that uploads artifacts, and
only make it blocking once the images have proven stable across a few weeks of
merges.

### Phase D: fake state injection

Scope. Make the hard-to-reach UI states reachable from a script.

- `sim/FurbleControlSim.cpp` gains commands to force each of the six
  `Control::state_t` values at `include/FurbleControl.h:24-37`.
- `sim/FurblePlatformSim.cpp` gains a settable battery level and current for the
  icon timer at `src/FurbleUI.cpp:159-181`.
- `sim/FurbleGPSSim.cpp` gains fix, no fix and stale fix modes for
  `src/FurbleUI.cpp:1555-1589`.

Effort: one day.

Unblocks: golden images for connect failed, reconnect, low battery and no fix.
These are exactly the states that regress silently today.

### Phase E: hardware screenshot over the console, in parallel

Depends on PR27. Independent of phases A to D and can be done by a different
person.

- `src/FurbleUI.cpp:371-378`, `UI::displayFlush`. Add a capture hook behind the
  same compile-time gate PR27 uses.
- `src/FurbleConsole.cpp` from PR27. Add a `screenshot` command that arms the
  hook, invalidates the active screen and disarms when the frame is complete.
- `tools/furble-screenshot.py`. Reassembles regions and writes a PNG.

Effort: one day.

Unblocks: the reference images that prove the simulator matches hardware, and
remote UI verification for boards the developer does not own.

### Phase F, optional and much later: QEMU boot smoke test

Only worth doing if someone wants it. A CI job that runs the built image under
Espressif's QEMU and checks that the version line from `src/main.cpp:25`
appears before the first crash. It will crash, at `M5.begin()` or at NimBLE
init. The value is limited to catching a broken image or a broken partition
table. Do not build a UI story on it.

## Risks

- **The pinned M5 library versions might not build native.** The code is present
  in 0.2.13 and 0.2.19, verified by reading those tags. Whether they compile
  cleanly on a current macOS and Linux toolchain is a separate question. Retire
  this first, with M5Unified's own example, before writing any furble code.
- **`lv_conf.h` drift.** The host build configures LVGL differently from the
  Kconfig-driven firmware build. If they diverge, the screenshots stop meaning
  anything. Mitigate by generating `sim/lv_conf.h` from
  `sdkconfig.m5stick-s3` and failing CI if the generated file differs from the
  committed one.
- **Two builds of the same sources.** Any change to `src/FurbleUI.cpp` now has to
  compile twice. Mitigate by running the sim build in CI on every push, so a
  break is caught at the same time as the firmware break, not weeks later.
- **Golden images are brittle.** Font hinting, SDL versions and LVGL bumps all
  move pixels. Mitigate by keeping the set small, by pinning the CI image as the
  only reference, and by treating diffs as review artifacts before treating them
  as failures.
- **The simulator can lie.** It has no BLE, no real timing and no real panel.
  Anything it says about behaviour is a hypothesis. Document that clearly, and
  keep the hardware track in phase E as the check.
- **Upstream appetite.** gkoh may not want a second build system in the tree.
  Mitigate by keeping every new file under `sim/` and `tools/`, by touching no
  shipping source and no shipping environment, and by opening the issue above
  before writing code.
- **macOS specifics.** The M5Unified example carries explicit Homebrew include
  and library paths and a separate note for Apple Silicon, which suggests the
  native build is fiddly on macOS. Budget for it.

## Verification

- `platformio run -e sim` builds on macOS and on Ubuntu.
- The sim window opens at 135x240, titled M5StickS3, showing the main menu.
- Arrow keys navigate: LEFT and RIGHT move the selection, UP acts as `BtnPWR`.
- The FauxNY connect flow runs end to end: Scan, select, connect progress,
  Connected page, shutter, disconnect.
- Every settings page renders with nothing clipped, on all five values of
  `M5GFX_BOARD`.
- Two runs of the same script on the same machine produce byte-identical PNGs.
- A Linux CI run and a macOS developer run produce either identical PNGs or a
  documented, understood difference, with the CI image as the reference.
- Cross-check against hardware once phase E lands: a console screenshot from a
  real StickS3 and a sim screenshot of the same page, compared side by side.
  Any structural difference is a simulator bug and is worth fixing before
  anyone trusts a golden image.
- The generated `sim/lv_conf.h` matches the committed one, checked in CI.

## Relationship to other plans

- Fork PR #44 (screenshot CI) depends on this PR. It builds the simulator in
  CI and compares the scripted captures against reviewed baselines. Note for
  that work: `gps.png` is not byte-reproducible, because TinyGPSPlus ages the
  fix with the real host clock, so the rendered fix age varies between runs.
  PR #44 must exclude it from the baseline set or mask the age field before
  comparing.
- PR27 (USB console) is a prerequisite for phase E only. Phases A to D do not
  need it.
- PR00b (dev USB debug) is a prerequisite for PR27 and therefore for phase E.
- This document is a developer tool. It is not on the dependency path of any
  feature PR, but it makes PR03, PR05, PR12 and PR25 cheaper to build and much
  cheaper to review.
- PR31 (S3 PSRAM) is unaffected. The simulator has no memory constraints and
  cannot say anything about PSRAM.

## References

All links checked on 16 August 2026.

Verified live:

- LVGL 9.4 SDL driver:
  https://lvgl.io/docs/open/9.4/details/integration/pc/sdl
  Confirms `LV_USE_SDL`, `lv_sdl_window_create()`, `lv_sdl_mouse_create()`,
  `lv_sdl_mousewheel_create()`, `lv_sdl_keyboard_create()`, and `-lSDL2`.
  Not used directly by this plan, because M5GFX's `Panel_sdl` sits in that role,
  but it is the fallback if the M5 native build does not work out.
- LVGL 9.4 snapshot module:
  https://lvgl.io/docs/open/9.4/details/auxiliary-modules/snapshot
  `lv_snapshot_take()`, gated on `LV_USE_SNAPSHOT`, supports RGB565 and
  ARGB8888, returns an `lv_draw_buf_t *` in LVGL 9 and allocates at call time.
  Note that `docs.lvgl.io/master/...` URLs now redirect to `lvgl.io/docs/open`,
  so prefer the versioned form.
- M5GFX PC setup guide:
  https://github.com/m5stack/M5GFX/blob/master/examples/PlatformIO_SDL/README.md
  Confirms the `native` PlatformIO environment, `brew install sdl2` on macOS,
  `libsdl2-dev` on Linux.
- M5Unified SDL example:
  https://github.com/m5stack/M5Unified/tree/master/examples/PlatformIO_SDL
  `platform = native`, `-DM5GFX_BOARD=board_M5StickCPlus`, `-DM5GFX_SCALE`,
  `-DM5GFX_ROTATION`, and the `lgfx::Panel_sdl::main(user_func, 128)` entry
  point.
- m5stack/lv_m5_emulator: https://github.com/m5stack/lv_m5_emulator
  M5Stack's own LVGL desktop emulator. MIT licensed, LVGL v8 by default with a
  documented v9 switch. Useful as a worked example of LVGL over `Panel_sdl`,
  not as a dependency.
- ESP-IDF QEMU guide for ESP32-S3:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/tools/qemu.html
  `idf.py qemu monitor`, `idf.py qemu gdb`, `--graphics` for the virtual
  framebuffer, `esp_lcd_qemu_rgb`, eFuse emulation.
- Espressif QEMU ESP32-S3 notes:
  https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/esp32s3/README.md
  Lists crypto peripherals, OpenCores ethernet, 2 to 32 MB PSRAM with octal
  support, the virtual RGB panel, timer group watchdog, GPIO with strap mode,
  eFuses and UART. Secure boot not supported on S3.
- `esp_lcd_qemu_rgb` component:
  https://components.espressif.com/components/espressif/esp_lcd_qemu_rgb
  Driver for the virtual QEMU RGB panel. Needs the Espressif QEMU fork from
  8.1.3-20231206. ARGB8888 and RGB565.
- Wokwi ESP32 peripheral support table:
  https://docs.wokwi.com/guides/esp32
  Bluetooth shows a bare cross across every chip column. I2C is master only.
  USB Serial and JTAG is "Serial supported, JTAG not".
- Wokwi CI getting started: https://docs.wokwi.com/wokwi-ci/getting-started
  Mentions 50 CI minutes per month on the free tier, 200 on Hobby, 2000 on Pro.
- Wokwi CLI usage: https://docs.wokwi.com/wokwi-ci/cli-usage
  `WOKWI_CLI_TOKEN`, headless operation, `--expect-text`, `--fail-text`,
  `--scenario`, `--screenshot-time`, `--screenshot-part`, `--screenshot-file`.
- Wokwi pricing: https://wokwi.com/pricing
  Community free with unlimited public simulations and a free open source
  licence, but CI minutes appear only on the Pro plan at 2000 per month. This
  contradicts the CI getting started page. Treat Wokwi CI as a paid dependency
  of unclear terms.
- FreeRTOS POSIX port:
  https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/main/portable/ThirdParty/GCC/Posix
  Exists, with `port.c` and `portmacro.h`. Platform support beyond Linux is not
  documented on that page. Considered and rejected in favour of a thin shim,
  because furble uses six FreeRTOS calls.

Read from source rather than documentation:

- `espressif/qemu`, branch `esp-develop`, `hw/xtensa/esp32s3.c`. The
  `Esp32s3SocState` struct lists the emulated devices. There is no I2C device.
  `hw/xtensa/esp32.c` has `TYPE_ESP32_I2C` and `esp32_machine_init_i2c()`; the
  S3 file only carries a comment referring to it. `esp32s3.c` realises exactly
  one SPI controller, `spi1`, for the flash.
- `m5stack/M5GFX` tag `0.2.19`, `src/M5GFX.cpp`. The SDL `autodetect()` path
  creates a `Panel_sdl`, sets the window title from the board, and sizes
  `board_M5StickS3` at 135x240.
- `m5stack/M5Unified` tag `0.2.13`, `src/utility/m5unified_common.h`. Defines
  `M5UNIFIED_PC_BUILD` when SDL is present and `ESP_PLATFORM` is not, and shims
  `esp_err_t`, `esp_log_level_t`, `i2c_port_t`, `ESP_OK`, `ESP_FAIL`.
- `m5stack/M5Unified`, `src/M5Unified.cpp`. Under `M5UNIFIED_PC_BUILD` the
  button raw state comes from emulated GPIO 36 to 39, which `Panel_sdl` drives
  from the arrow keys: LEFT is `BtnA`, DOWN is `BtnB`, RIGHT is `BtnC`, UP is
  `BtnPWR`.
- `managed_components/lvgl__lvgl` at 9.4.0. Contains `src/drivers/sdl` and
  `src/others/snapshot`. `src/lv_conf_internal.h:3290-3296` guards
  `LV_USE_SNAPSHOT` with `#ifndef` before consulting `CONFIG_LV_USE_SNAPSHOT`.
## Implementation state

Rebase notes, ported from base `2b79ce8` to current fork master:

- The plans document keeps the full reviewed plan; the implementation state
  from the branch is appended below it.
- The fakes were updated to master's grown interfaces: `Scan` gained `Mode`,
  `setMode`, `setTimeout` and the three-argument `start` with an end
  callback; the sim `Platform` implements `watchdogEnable`, `watchdogFeed`,
  `setCPUMaxFreq`, `getCPUMaxFreq`, `getPMConfig`, `dumpPMLocks`,
  `hasTicklessIdle`, and the battery caps, sample, capacity and fail count
  readers; `Platform::setSleep` no longer exists and was dropped.
- New shim headers: `esp_chip_info.h`, `esp_flash.h`, `esp_idf_version.h`,
  and a `FurbleCompanion.h` fake that reports the companion disabled (the
  real one needs NimBLE). `esp_system.h` adds reset reasons and heap
  getters, `esp_err.h` adds `esp_err_to_name`, `esp_pm.h` adds the lock
  API, and the fake UART accepts `uart_write_bytes` and `uart_wait_tx_done`
  with `UART_SCLK_XTAL`.
- `src/FurblePower.cpp` and `src/FurbleUIBulb.cpp` joined the sim source
  list; both are required by master's UI.
- Verified after the rebase: `sim/build.sh` builds (with `FURBLE_DEP_ROOT`
  and `FURBLE_LVGL_DIR` pointing at the worktree's PlatformIO caches), the
  headless smoke run exits 0 with four 135x240 captures, and the GPS run
  exits 0 with one capture.

Status at commit `2b79ce8`, 16 August 2026:

- Phase A is implemented for the M5StickS3 geometry, 135 by 240 pixels.
- Phase B is implemented with a virtual clock, scripted SDL key events, and
  RGB PNG framebuffer capture.
- The real `src/FurbleUI.cpp`, `src/FurbleUIIntervalometer.cpp`,
  `src/FurbleSpinValue.cpp`, `src/FurbleCalibrate.cpp`,
  `src/FurbleSettings.cpp`, and `src/FurbleGPS.cpp` are compiled.
- `src/FurbleGPS.cpp` is unchanged. The simulator supplies a fake
  `driver/uart.h` and a fake UART implementation that repeatedly emits valid
  RMC and GGA NMEA sentences. This preserves the real GPS task, UART event
  handling, TinyGPSPlus parsing, fix ageing, and control update path.
- The fake scan exposes one FauxNY camera. The fake control layer ramps the
  connection to active after 750 virtual milliseconds. Shutter, focus, GPS,
  and disconnect commands reach the fake camera.
- Preferences persist in a host file selected with `FURBLE_SIM_PREFS`.
- `sim/scripts/smoke.txt` covers the main menu, scan, connection progress,
  connected page, and shutter page.
- `sim/scripts/settings.txt` covers navigation through the settings menu.
- `sim/scripts/gps.txt` opens GPS Data and captures the parsed fake fix.
- `sim/lv_conf.h` is generated from `sdkconfig.m5stick-s3` by
  `tools/gen_lv_conf.py`.

## Files

The host implementation is under `sim/`.

- `sim/platformio.ini` contains the planned native PlatformIO environment.
- `sim/CMakeLists.txt` is a direct CMake build description for machines with
  CMake installed.
- `sim/build.sh` is the working macOS fallback. It uses cached M5GFX,
  M5Unified, LVGL, and TinyGPSPlus sources and invokes Apple Clang directly.
- `sim/shim/` supplies the ESP-IDF, FreeRTOS, M5PM1, and BLE-facing headers.
- `sim/*Sim.cpp` supplies fake platform, device, scan, camera, control, and
  preferences implementations.
- `sim/fake_uart.cpp` supplies the UART driver used by the unmodified GPS
  source.
- `sim/driver.cpp` reads `wait`, `advance`, `key`, `press`, `capture`,
  `uart-dump`, `home`, `back`, and `exit` commands.
- `sim/capture.cpp` reads the M5GFX SDL panel and writes RGB PNG files without
  adding an image library dependency.
- `.gitignore` excludes the local native build output and the temporary
  PlatformIO core directory.

## Screenshot CI

Screenshot CI is implemented on `feat/ui-screenshot-ci`, rebased onto the
merged simulator on master. The deterministic script captures the modeled
M5StickS3 135 by 240 UI pages, and `.github/workflows/ui-screenshots.yml`
builds the simulator, runs the capture under Xvfb, uploads `ui-screenshots`,
and updates one marker-based PR comment. The current simulator models only
this board class. The workflow is interim: plan 63 later consolidates it into
one combined sim-report job. `gps.png` is not byte-reproducible across runs,
so no baseline comparison may include it.

The first two live runs of the workflow failed before building anything.
`platformio pkg install -e sim` skipped the owner-less `M5GFX@0.2.19` and
`M5Unified@0.2.13` specs without an error, so `sim/.pio/libdeps/sim` held only
lvgl and TinyGPSPlus and `sim/build.sh` aborted on its M5GFX check. The
workflow now fetches all four dependencies as pinned shallow git clones and no
longer installs PlatformIO at all, since `sim/build.sh` only needs the source
trees. `sim/platformio.ini` now owner-qualifies the two M5Stack specs so the
PlatformIO route also resolves them. Fixing the dependencies exposed a second
break: plan 10 changed `Control::getTargets` to return a snapshot vector of
raw pointers, and the rebased sim shim still returned a reference to the
owning vector. `sim/FurbleControlSim.cpp` now mirrors the firmware
implementation.

## Deviations

The selected approach was tried first.

1. `pio run -c sim/platformio.ini` first stopped on the existing PlatformIO
   core lock permissions. Retrying with a worktree-local
   `PLATFORMIO_CORE_DIR` reached the native platform install, but the sandbox
   blocked its network download. The PlatformIO configuration remains for a
   normal networked developer environment.
2. CMake was prepared as the next fallback, but CMake is not installed in this
   macOS environment. The direct Clang script is therefore the verified build
   path. It discovers LVGL in a sibling worktree cache and M5 libraries in the
   repository `.pio/libdeps` cache. No dependency source was copied into the
   worktree.
3. The earlier planning document proposed replacing `src/FurbleGPS.cpp` in
   the simulator. The later requirement supersedes that proposal. The real GPS
   source is compiled unchanged, with only the fake UART driver substituted at
   the include and link seams.
4. Only the M5StickS3 board is wired in this first implementation. Other board
   geometries can be added by changing the M5GFX board definition and input
   mapping after the S3 path is stable.

## Verification

From the worktree root:

```sh
python3 tools/gen_lv_conf.py sdkconfig.m5stick-s3 sim/lv_conf.h
sh sim/build.sh
```

Run the deterministic smoke script headlessly:

```sh
mkdir -p /private/tmp/furble-sim-captures
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  FURBLE_SIM_PREFS=/private/tmp/furble-sim-prefs.bin \
  ./sim/build/furble-sim \
  --script sim/scripts/smoke.txt \
  --capture-dir /private/tmp/furble-sim-captures
```

Run the GPS capture:

```sh
mkdir -p /private/tmp/furble-sim-gps-captures
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  FURBLE_SIM_PREFS=/private/tmp/furble-sim-gps-prefs.bin \
  ./sim/build/furble-sim \
  --script sim/scripts/gps.txt \
  --capture-dir /private/tmp/furble-sim-gps-captures
```

The verified build produced `sim/build/furble-sim`, a macOS arm64 Mach-O
binary. The dummy-video smoke run exited with status 0 and captured four
135 by 240 RGB PNGs. The GPS run exited with status 0 and rendered a valid
fix at 48.12 degrees latitude, 11.52 degrees longitude, 545.40 meters, and
eight satellites. Two smoke runs produced byte-identical PNGs.

The dummy SDL driver rendered and supported panel readback on this macOS
machine. Linux CI may need an Xvfb-style display if a future SDL or M5GFX
version requires a display-backed surface. That is a Linux CI concern and is
not needed for the verified macOS build.

## Review fixes

Applied on the PR branch after review:

- `sim/CMakeLists.txt` now compiles `src/FurblePower.cpp` and
  `src/FurbleUIBulb.cpp`, matching `sim/build.sh`. The two lists carry a
  keep-in-sync note; a generated shared list was considered and rejected as
  more machinery than two entries justify.
- Both build paths preflight TinyGPSPlus and default `DEP_ROOT` to the
  repo-local `.pio/libdeps/m5stick-s3`. The hardcoded sibling worktree LVGL
  fallbacks are gone; only `managed_components/lvgl__lvgl` is probed, which
  tracks the LVGL version pinned by `src/idf_component.yml`.
- The fake `Scan` stores and invokes the scan end callback, so the UI leaves
  the scanning state, and it always delivers the scripted scan result even
  when FauxNY is already seeded. Deviation from hardware: the result and the
  end callback fire in the same `update()` tick, where real scans end on the
  scan timeout.
- The fake UART records every `uart_write_bytes` payload. The script driver
  gained a `uart-dump` verb that prints each captured command as a
  `uart-tx` line and clears the capture, so $PCAS sends are assertable.
- Unmodelled error branches in the fake UART: every `uart_*` call returns
  `ESP_OK`, and the event queue only ever carries `UART_PATTERN_DET`. The
  `UART_FIFO_OVF`, `UART_BUFFER_FULL`, break and parity paths in
  `src/FurbleGPS.cpp` are never exercised by the simulator.

## Remaining work

Phase C can add a CI job and golden image comparison (fork PR #44). Phase D
can add scripted failure, battery, stale-fix, and reconnect states. Other M5
board geometries and a hardware screenshot comparison remain future work.
