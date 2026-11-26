# 355 - Review feedback for the CMake entry point status

## Scope

This is the retrospective, adversarial review of PR 517 (`3284b617`,
`docs/355-cmake-entry-point-status`), which merged on green CI plus author
verification without an independent review. Re-read at `c644f39e`.

Verdict: the statement is true. The CMake target builds, all three stock
scenarios exit 0, `nm -m` binds `_SDL_PollEvent` to `libSDL2-2`, and reverting
PR 506's link order puts the binary back to exit 134 with the reported
`__stack_chk_fail` abort. No defect; two wording nits.

## What was attacked

### The link order

`sim/CMakeLists.txt` adds `SDL2::SDL2` at line 290 and the SDL3
`-needed_library` through `target_link_libraries` at line 313, after it. The
generated link line for the configured build is:

```
... lvgl-build/lib/liblvgl.a /opt/homebrew/lib/libSDL2-2.0.0.dylib
    -framework Cocoa -Wl,-needed_library,/opt/homebrew/lib/libSDL3.dylib ...
```

SDL2 is before SDL3, so ld64 binds the SDL2 entry points to libSDL2-2. A
`target_link_options` form (the pre-506 code) emits the flag before the
libraries; the same build then has no `-needed_library` after SDL2.

### The build and the three stock scenarios

```
cmake -S sim -B /tmp/rv/cmakebuild
cmake --build /tmp/rv/cmakebuild --parallel 4          # exit 0
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  /tmp/rv/cmakebuild/furble-sim --script sim/scenarios/e2e/hw-psram-octal-sticks3.txt  # 0
  ... hw-profile-stickc.txt                                                             # 0
  ... watchdog-feed.txt                                                                 # 0
nm -m /tmp/rv/cmakebuild/furble-sim | grep _SDL_PollEvent
# (undefined) external _SDL_PollEvent (from libSDL2-2)
otool -L ... | grep -i SDL
# /opt/homebrew/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib
# /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib
```

The build prints the four `M5PM1 access failed, retrying (n)` lines the plan
says both binaries print, and no abort. The LVGL sub-build still reports the
duplicate-library warnings the plan records; they are warnings, not failures.

### The revert

Restoring the pre-506 `target_link_options` form, reconfiguring and relinking
the same object tree, then running `hw-profile-stickc.txt`, gives exit 134 with
only the four PM1 retry lines on stdout, exactly the plan's reproduction. The
`__stack_chk_fail` backtrace is in the plan and in the macOS crash report the
original finding cites. Restoring the `target_link_libraries` form relinks to
exit 0 and `_SDL_PollEvent (from libSDL2-2)` again.

### The coverage claim

`grep` over `.github/workflows` finds `cmake -S sim -B build-sim-static` only in
`static-analysis.yml`, where it exists to produce `compile_commands.json` for
`check_static_analysis.py`. No job runs `cmake --build` for that directory and
no job executes the CMake binary. The statement "no workflow builds or runs it"
is true, and so is "a regression in that path is invisible to CI".

## Finding 1: the status is accurate

Status: no defect.

`docs/sim.md` no longer claims the binary aborts. It keeps `sh sim/build.sh` as
the supported entry point, says the CMake target builds and runs the stock
scenarios since #506, and says no workflow builds or runs it.
`plans/README.md` describes the finding as fixed rather than open. The plan's
"resolved" state, the reproduction, the cause, the fix, the verification and the
remaining smoke-step gap all match the measured behaviour above.

## Finding 2: wording nits

Status: nits.

- `docs/sim.md` says the target "builds and runs the stock scenarios on macOS
  arm64 again since the #506 link-order fix". "Again" implies it worked before
  #445 introduced the `-needed_library` flag; the plan records it aborting on
  pristine master. "Now" would be exact.
- The plan's status line names `588e2d36` as the verified commit. The link
  order is unchanged at `c644f39e`, so the claim still holds; naming the commit
  is history, not a stale claim. No change needed.

No code or doc fix is made by this review.
