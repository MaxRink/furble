# 124 - Reliable incremental simulator builds

## Motivation

The direct SDL build previously decided whether an object was current from its
source timestamp and a broad simulator-shim timestamp scan. Editing a project
header such as `include/FurbleGPS.h` could therefore leave dependent objects
stale, hiding compile errors or linking behavior from the changed header.

## Scope

Simulator build tooling and its contributor documentation only. Firmware
behavior and the CMake simulator entry point are unchanged.

## Implementation state

- `sim/build.sh` asks clang for Make-compatible `.d` depfiles for every C and
  C++ object, including external M5GFX, M5Unified, LVGL, TinyGPSPlus, and icon
  sources.
- The incremental decision uses BSD/GNU `make -q` against the depfile. Missing
  objects or depfiles, newer prerequisites, deleted prerequisites, and a newer
  generated `sim/lv_conf.h` all force recompilation.
- `sim/scripts/test-build-deps.sh` performs a clean build, touches
  `include/FurbleGPS.h`, and verifies that `FurbleGPS.cpp` and `FurbleUI.cpp`
  rebuild while `FurbleBootScreen.cpp` remains cached. It restores the header
  timestamp on exit.
- The wrapper used by that self-test logs only compile sources and delegates to
  the selected compiler; it does not alter normal builds.

## Deviations

The direct build now requires the ubiquitous BSD or GNU `make` executable for
incremental cache checks. Clean builds and link behavior remain unchanged.
The CMake entry point keeps using CMake's native dependency scanner.

## Verification

Run the self-test with the same `FURBLE_DEP_ROOT` and `FURBLE_LVGL_DIR` values as
the simulator build. Follow it with the normal clean/incremental simulator
build and the applicable E2E scenarios. No hardware gate applies.
