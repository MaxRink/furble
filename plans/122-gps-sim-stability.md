# 122 - GPS simulator stability and coherent snapshots

## Motivation

`GPS::task()` decodes NMEA on a FreeRTOS task while the LVGL GPS timers and
console read TinyGPSPlus fields from other tasks. TinyGPSPlus is mutable even
through several of its value accessors, and the old public `GPS::get()` returned
the parser by non-const reference. The SDL simulator really creates a
background host thread for the GPS task, so this was a simulator-visible data
race. It could produce mixed coordinates, counters, and timestamps, and it
made the old SDL render failures harder to distinguish from application state
errors.

The prior SDL pixel readback failure remains a separate infrastructure limit:
`M5.Display.readRectRGB()` can crash in hosted software GL. The simulator e2e
gate therefore continues to assert rendered state without pixel readback.

## Design

1. Keep TinyGPSPlus private to `GPS` and serialize its mutation and reads with
   one mutex.
2. Replace the mutable `GPS::get()` escape hatch with
   `getStatusSnapshot()`, which returns one coherent copy of every parser field
   used by the console, GPS Data page, and Raw NMEA page.
3. Use that same snapshot for fix freshness and burst sequence accounting, so
   a single update cannot combine fields from different parser states.
4. Add an e2e scenario that repeatedly visits the GPS Data and Raw NMEA pages
   while the fake UART and GPS task run concurrently.
5. Start the SDL render loop only after `Platform::init()` has registered the
   single simulated panel. Pinned M5GFX mutates its global monitor list on the
   simulator thread while the default `Panel_sdl::main()` traverses it on the
   main thread, so this one-time barrier removes a separate startup race without
   patching the third-party dependency.

No GPS protocol, timing, or default setting changes are intended. The parser
mutex is held only while accessing TinyGPSPlus and never across UART, LVGL,
console output, NVS, or camera operations.

## Verification

- `gps-concurrent-pages.txt` must pass on the M5StickS3 simulator and the
  other panel builds where the pages are present.
- Run the full host CTest suite for unrelated regressions.
- Build a focused simulator with `FURBLE_SIM_SANITIZE=thread` and run
  `gps-concurrent-pages.txt` in CI. Any reported race or deadlock fails the
  simulator E2E job.
- Hardware validation is not required for this ownership-only change. A
  bench run should still confirm the GPS console output and live page after a
  real receiver fix.

## Implementation state

Implemented on `test/gps-sim-stability`: all TinyGPSPlus access is serialized,
callers consume the status snapshot, and the concurrent-page scenario is added.
The SDL pixel readback issue is explicitly deferred to simulator
infrastructure because it is independent of the GPS race.
