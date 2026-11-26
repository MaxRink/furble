# 355: the documented CMake entry point builds a simulator that aborts

State: finding recorded, build path unfixed. The false doc sentence is
corrected in this PR: `docs/sim.md` now names `sh sim/build.sh` as the only
supported entry point. Repairing `sim/CMakeLists.txt` and adding the smoke step
remain open.

## What two agents reported

Two independent agents observed the documented CMake entry point building a
`furble-sim` that aborts in `lgfx::v1::Panel_sdl::_event_proc()` on pristine
master, while the `sim/build.sh` binary runs the same script cleanly. Both
halves reproduce.

## Reproduction on pristine master

Tree: `git archive origin/master` (a5cb2e14) extracted to
`/tmp/s3sim-align-cmake-finding`, with the PlatformIO dependency cache
symlinked as `.pio`. The LVGL managed component is not populated in this
sandbox, so the source is passed with `-DFURBLE_LVGL_DIR`; that is the only
deviation from the commands in `docs/sim.md`.

```sh
cd /tmp/s3sim-align-cmake-finding
cmake -S sim -B sim/build-cmake -DFURBLE_LVGL_DIR=/tmp/lvgl-9.4
cmake --build sim/build-cmake --parallel 4
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-cmake/furble-sim --script sim/scenarios/e2e/hw-psram-octal-sticks3.txt
```

Configure exits 0, the build exits 0, the run exits 134 (SIGABRT). Nothing is
printed to stdout or stderr; the process dies right after four
`M5PM1 access failed, retrying (n)` lines. The macOS crash report
(`~/Library/Logs/DiagnosticReports/furble-sim-*.ips`) records:

```
asi: {'libsystem_c.dylib': ['stack buffer overflow']}
__pthread_kill
__abort
__stack_chk_fail
lgfx::v1::Panel_sdl::_event_proc()
```

Two more stock scenarios abort the same way, so it is not scenario-specific:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-cmake/furble-sim --script sim/scenarios/e2e/hw-profile-stickc.txt   # 134
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-cmake/furble-sim --script sim/scenarios/e2e/watchdog-feed.txt       # 134
```

## Same tree, the other build path

In `/tmp/furble-s3sim` (branch `fix/sticks3-sim-align`; same M5GFX, same LVGL
9.4.0, same SDL libraries) two binaries were built from the same working tree:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-cmake/furble-sim --script sim/scenarios/e2e/hw-profile-stickc.txt
# exit 134

sh sim/build.sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build/furble-sim --script sim/scenarios/e2e/hw-profile-stickc.txt
# exit 0, asserts print, clean shutdown
```

Same scenario, same tree, one binary per build system: the crash tracks the
build path, not the source.

## What differs, and what does not

Checked, so nobody re-derives it:

- Every define matches `sim/build.sh`: `FURBLE_M5STICKS3`, `FURBLE_RIG`,
  `FURBLE_SIM`, `FURBLE_VERSION="sim"`, `FURBLE_TEST_VERSION=1`,
  `FURBLE_BATTERY_DEBUG=0`, `M5GFX_SCALE=2`, `M5GFX_BOARD=board_M5StickS3`,
  `LV_CONF_INCLUDE_SIMPLE`, `LV_LVGL_H_INCLUDE_SIMPLE`, `LV_KCONFIG_IGNORE`,
  `_THREAD_SAFE` (read from `sim/build-cmake/compile_commands.json`, generated
  with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`).
- `-include sdkconfig_sim.h` is applied to firmware and simulator translation
  units and not to the M5GFX and M5Unified dependencies, which is build.sh's
  contract too (`sdkconfig_flags_for`).
- Both binaries link the same SDL pair in the same direction. `otool -L` shows
  `libSDL2-2.0.0.dylib` (sdl2-compat 3201.74.0) and `libSDL3.0.dylib`
  (401.20.0). The CMake binary additionally carries `LC_RPATH /opt/homebrew/lib`.
- The CMake binary prints `M5PM1 access failed, retrying (n)` four times before
  the abort; the build.sh binary prints none of those lines. So the two builds
  also differ in the modelled PMIC access path. That is a second symptom, and
  probably the first thing to chase.
- The CMake target builds LVGL through LVGL's own `CMakeLists.txt`, including
  its examples and demos (`liblvgl_examples.a`, `liblvgl_demos.a`, and a link
  warning about duplicate libraries). build.sh compiles `LVGL_DIR/src/**/*.c`
  directly with the simulator's flag set and builds neither. That is the
  largest structural difference between the two paths.

## Which one is wrong

The build path is wrong. The docs sentence is wrong in consequence.

- `docs/sim.md` said the CMake entry point has "the same source and board
  defaults" as the direct build, with `sim/CMakeLists.txt` as that entry point.
  For sources and defines that holds. It does not hold in effect: the binary
  this path produces aborts on its first scenario on unmodified master.
- No workflow builds or runs that binary. `.github/workflows/static-analysis.yml`
  runs `cmake -S sim -B build-sim-static` only to obtain a compile database; it
  never compiles the target, so every gate stays green while the path is broken.
- The only build any gate exercises is `sh sim/build.sh` (CI `sim-e2e`, the
  plan 348 gates, `sim/scripts/*.sh`), and that binary works.

Fix: repair `sim/CMakeLists.txt` (start with the LVGL sub-build and the PMIC
access difference above) and add a smoke step that builds the CMake target and
runs one scenario through it. Until that passes, `docs/sim.md` names
`sh sim/build.sh` as the only entry point rather than presenting both as
equivalent, which this PR changed.
