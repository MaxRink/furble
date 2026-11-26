# 355: the documented CMake entry point aborted on macOS, and what fixed it

State: resolved. The abort was the link-order defect PR 506 fixed in
`sim/CMakeLists.txt` (`b549d36c`). At `588e2d36` the documented CMake entry
point builds a binary that runs this plan's three stock scenarios with exit
status 0 on macOS arm64. What remains is coverage, not a defect: no workflow
builds or runs that target, so a regression in it is still invisible to CI.
`docs/sim.md` names `sh sim/build.sh` as the supported entry point and records
the CMake target's status without claiming it aborts.

## What two agents reported

Two independent agents observed the documented CMake entry point building a
`furble-sim` that aborted in `lgfx::v1::Panel_sdl::_event_proc()` on pristine
master, while the `sim/build.sh` binary ran the same script cleanly. Both halves
reproduced, and the observation was correct. The cause was not the one this plan
first chased: it was the order of the two SDL libraries on the link line, and
PR 506 fixed it before this plan merged.

## Reproduction, tree before #506

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

Configure exited 0, the build exited 0, the run exited 134 (SIGABRT). Nothing
was printed to stdout or stderr; the process died right after four
`M5PM1 access failed, retrying (n)` lines. The macOS crash report
(`~/Library/Logs/DiagnosticReports/furble-sim-*.ips`) recorded:

```
asi: {'libsystem_c.dylib': ['stack buffer overflow']}
__pthread_kill
__abort
__stack_chk_fail
lgfx::v1::Panel_sdl::_event_proc()
```

Two more stock scenarios aborted the same way, so it was not
scenario-specific:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-cmake/furble-sim --script sim/scenarios/e2e/hw-profile-stickc.txt   # 134
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-cmake/furble-sim --script sim/scenarios/e2e/watchdog-feed.txt       # 134
```

## The cause: SDL3 ahead of SDL2 on the link line

The stack buffer overflow in the SDL panel was an SDL ABI mismatch, not a
simulator defect. PR 445 linked SDL3 directly so that macOS stops showing
sdl2-compat's blocking dialog, but it passed the flag to `target_link_options`:

```cmake
target_link_options(furble-sim PRIVATE
                    "LINKER:-needed_library,${FURBLE_SDL3_LIBRARY}")
```

CMake emits link options ahead of the target's libraries, so `-needed_library
libSDL3.dylib` landed before `libSDL2-2.0.0.dylib`. ld64 binds an undefined
symbol to the first input that exports it, so every SDL2 API call in the panel
resolved to SDL3. SDL3's `SDL_Event` is 128 bytes where the SDL panel compiles
against 56, so `SDL_PollEvent` wrote 128 bytes into a 56-byte frame and
`__stack_chk_fail` aborted with 134. `sim/build.sh` already ordered the two
libraries the other way, which is why one build path worked and the other did
not.

PR 506 moved the same flag into `target_link_libraries` after `SDL2::SDL2`:

```cmake
target_link_libraries(furble-sim PRIVATE
                      "-Wl,-needed_library,${FURBLE_SDL3_LIBRARY}")
```

The binding is visible in the binary, so this is the check that tells a bound
build from an unbound one:

```sh
nm -m sim/build-cmake/furble-sim | grep _SDL_PollEvent
#                  (undefined) external _SDL_PollEvent (from libSDL2-2)
```

## Verified after #506

This plan's correction was re-run on `588e2d36` with the same dependency cache
symlinked into the worktree, macOS 27.2.1 arm64, cmake 4.5.1, Apple clang
21.0.0, the default generator and no `CMAKE_BUILD_TYPE`:

```sh
cmake -S sim -B sim/build-cmake
cmake --build sim/build-cmake --parallel 6
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-cmake/furble-sim --script sim/scenarios/e2e/hw-psram-octal-sticks3.txt   # exit 0
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-cmake/furble-sim --script sim/scenarios/e2e/hw-profile-stickc.txt       # exit 0
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./sim/build-cmake/furble-sim --script sim/scenarios/e2e/watchdog-feed.txt           # exit 0
```

The run prints its asserts and its orderly shutdown, the four
`M5PM1 access failed, retrying (n)` lines are still there, and no abort, no
`__stack_chk_fail` and no crash report appear. `otool -L` shows the same SDL
pair as `sim/build.sh` in the same order, `libSDL2-2.0.0.dylib` (sdl2-compat
3201.74.0) then `libSDL3.0.dylib` (401.20.0), and both binaries bind the SDL2
entry points to libSDL2-2 in `nm -m`.

## What still differs from sim/build.sh

Two differences remain after the #506 fix, and neither breaks the binary:

- LVGL: the CMake target builds LVGL through LVGL's own `CMakeLists.txt`,
  including its examples and demos, plus thorvg. `sim/build.sh` compiles
  `LVGL_DIR/src/**/*.c` with the simulator's flag set and builds none of those.
  The link step reports `ld: warning: ignoring duplicate libraries` for
  `liblvgl.a`, `liblvgl_demos.a`, `liblvgl_examples.a` and `liblvgl_thorvg.a`.
  This is the largest remaining difference between the two paths.
- The CMake binary carries `LC_RPATH /opt/homebrew/lib`; the `sim/build.sh`
  binary does not. Both resolve libSDL2-2 and libSDL3 from the Homebrew prefix
  without it.

One claim in the first version of this plan did not reproduce on the corrected
tree: it said the `sim/build.sh` binary prints none of the four
`M5PM1 access failed, retrying (n)` lines the CMake binary prints before boot.
Both binaries print all four here, so the modelled PMIC access path does not
differ between the two builds, and the abort was never a PMIC symptom. That
observation came from a different worktree (`/tmp/furble-s3sim`, branch
`fix/sticks3-sim-align`) with its own dependency cache.

## What remains open

A smoke step that builds the CMake target and runs one scenario through it. No
workflow builds or runs the binary: `.github/workflows/static-analysis.yml`
runs `cmake -S sim -B build-sim-static` only to obtain a compile database, so
every gate stays green while the target is unbuilt and unrun. Until that step
exists, `docs/sim.md` names `sh sim/build.sh` as the supported entry point
rather than presenting the two as equivalent.
