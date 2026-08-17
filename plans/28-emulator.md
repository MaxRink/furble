# PR28: host simulator for the furble UI

## Motivation

Every furble UI change normally needs a firmware build, a flash cycle, and a
walk through the menu tree with three buttons. Layout work is especially slow
because the useful question is whether a page still fits on a 135 by 240
screen. There is no automated check that a shared UI change still renders.

A host simulator makes UI iteration fast. It also provides deterministic PNGs
for review. It does not replace hardware validation. BLE, real UART timing,
power management, the panel driver, and camera protocols remain hardware
concerns.

## Goal

Build the furble UI natively on macOS with M5GFX and M5Unified's SDL panel.
Drive it with keyboard events. Use fake platform, BLE, camera, preferences,
and GPS services. Keep all simulator code out of the shipping firmware.

The original plan selected a native PlatformIO environment. This update records
the implementation of phases A and B and the build fallback used on the
development machine.

## Implementation state

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
- `sim/driver.cpp` reads `wait`, `advance`, `key`, `press`, `capture`, and
  `exit` commands.
- `sim/capture.cpp` reads the M5GFX SDL panel and writes RGB PNG files without
  adding an image library dependency.
- `.gitignore` excludes the local native build output and the temporary
  PlatformIO core directory.

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

## Remaining work

Phase C can add a CI job and golden image comparison. Phase D can add scripted
failure, battery, stale-fix, and reconnect states. Other M5 board geometries
and a hardware screenshot comparison remain future work.
