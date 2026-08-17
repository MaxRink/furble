# sim/ (host SDL simulator)

Host build of the furble UI over M5GFX/M5Unified SDL. Developer tool only.
It never changes firmware behavior: no shipping source under src/, include/,
or lib/ may be modified for the simulator. All adaptation happens through the
shim headers and the fake implementations here.

## Build entry points

- `sim/build.sh`: the verified direct-clang path on macOS. Run
  `python3 tools/gen_lv_conf.py sdkconfig.m5stick-s3 sim/lv_conf.h` first if
  the sdkconfig changed.
- `sim/CMakeLists.txt`: the CMake path for machines with CMake installed.
- `sim/platformio.ini`: planned `platform = native` environment for networked
  developer machines.
- Keep the firmware source list in `sim/build.sh` and `sim/CMakeLists.txt` in
  sync. Both carry a note.

## Dependency resolution

- M5GFX, M5Unified, and TinyGPSPlus come from the repo-local PlatformIO cache
  `.pio/libdeps/m5stick-s3` (populate it with a firmware build, override with
  `FURBLE_DEP_ROOT`).
- LVGL comes from `managed_components/lvgl__lvgl`, which tracks the version
  pinned by `src/idf_component.yml` (override with `FURBLE_LVGL_DIR`).
- SDL2 comes from Homebrew or /usr/local.

## Determinism caveats

- The virtual clock makes scripted runs reproducible: two smoke runs produce
  byte-identical PNGs.
- Exception: `gps.txt` renders the TinyGPSPlus fix age from the real host
  clock, so `gps.png` is not byte-reproducible and must not be a golden
  baseline as-is.
- The fake scan delivers its result and the scan end callback in the same
  `update()` tick. The fake UART never emits error events and captures all
  writes; dump them with the `uart-dump` script verb.
