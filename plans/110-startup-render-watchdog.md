# 110 - Startup render task-watchdog stall

## Motivation

Live boot logging on the M5StickS3 (current master firmware) caught the ESP-IDF
task watchdog firing once at about 8194 ms uptime:

```
E (8194) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (8194) task_wdt:  - IDLE (CPU 0)
E (8194) task_wdt: Tasks currently running:
E (8194) task_wdt: CPU 0: main
I furble: Index entries: 2
I furble: Loading index entry: 7B1E779AC452
I furble: Loading index entry: 585EB0EF2376
I furble: invalidate: 358/s last=(17,38)-(116,137)
```

The main task ran for more than 5 s without yielding, so IDLE0 never got the CPU
and the task watchdog (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`,
`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y`) tripped. Panic is off
(`CONFIG_ESP_TASK_WDT_PANIC` is not set), so it warns and continues: the device
runs fine afterwards (heap 8.2 MB healthy). The user experience is a one-time
~5 s freeze at boot. It reproduces at the same ~8.2 s timestamp every boot and
correlates with two saved cameras (X-E5 + X100VI) plus AUTOCONNECT.

## Root cause

The whole LVGL menu tree is built synchronously on the main task in the `UI`
constructor (`UI::UI` -> `addMainMenu` -> `addConnectMenu` / `addScanMenu` /
`addDeleteMenu` / `addIRMenu` / `addSettingsMenu` / `addConnectedMenu` and their
sub pages: display, timer, GPS, diagnostics, power, four intervalometer spinner
pages, bulb, features, feedback, theme, transmit power). The task loop with its
`vTaskDelay(5)` only starts after the constructor returns, so this entire build
is one unyielded stretch on CPU0. On the 240 MHz S3 it runs long enough to
starve IDLE0 past the 5 s window.

An addr2line pass on the flashed image PCs (resolved against a matching
`m5stick-s3-debug` build, LVGL being a managed component at a stable base)
confirms the hot path is LVGL style resolution during page layout/draw. The
recurring frame `0x420539B1` (repeated 8x) is:

```
lv_obj_style_state_compare   managed_components/lvgl__lvgl/src/core/lv_obj_style.c:559
  <- get_selector_style_prop  lv_obj_style.c:1168
  <- lv_obj_get_scrollbar_area lv_obj_scroll.c
  <- draw_label (roller)       widgets/roller/lv_roller.c
style_init                    themes/default/lv_theme_default.c
```

and the surrounding frames are the constructor builders (`Furble::UI::UI`
FurbleUI.cpp:265, `addSpinnerPage` ~4049, `addDiagnosticsMenu` ~5617,
`configShutterControl` ~2700). Every menu item carries
`LV_OBJ_FLAG_STATE_TRICKLE` and a `LV_LABEL_LONG_SCROLL_CIRCULAR` label, both of
which force per-object style resolution and text measurement while the tree is
laid out, so the build cost grows faster than linearly with the object count.

The "invalidate: 358/s" line is diagnostic of the phase: an invalidate-area rate
can only be printed if `lv_obj_invalidate` fires spread across a real second.
A single blocked render does not generate a stream of invalidations, it consumes
the queued invalid areas. So the 358/s reflects hundreds of objects being
created and laid out over multiple wall-clock seconds. That is the build, not a
single refresh.

This was confirmed empirically in the host simulator by timing the two phases
with `std::chrono::steady_clock`:

| phase                      | 0 cameras (median) | 2 cameras (median) |
| -------------------------- | ------------------ | ------------------ |
| UI constructor (tree build)| ~52 ms             | ~85 ms             |
| first `lv_task_handler`    | ~3.5 ms            | ~5 ms              |

The constructor dominates the first render by roughly 15-17x, and later handler
iterations are ~0 (the tree is fully drawn on the first call, nothing keeps
invalidating). The host is roughly 30-60x faster than the S3, so an ~85 ms
constructor scales to the multiple-second range on device, landing in the 5 s
watchdog window, while the ~5 ms render scales to a fraction of a second. Two
saved cameras raise the constructor cost about 1.6x over empty, matching the
"triggered by two or more saved cameras" report. This also rules the sustained
render storm out as the sole cause: the `ui.task()` loop yields every 5 ms, so a
steady invalidation rate cannot by itself hold CPU0 for 5 s.

Two saved cameras plus AUTOCONNECT push the unyielded window over the edge: the
initial `lv_menu_set_page(m_MainMenu.page)` at the end of `addMainMenu` fires the
main-page display handler, which calls `CameraList::getSaveCount()` (the "Index
entries: 2 / Loading index entry" log) and, with AUTOCONNECT on, `doConnect`
synchronously, switching to the connected page and forcing a second full layout
pass inside the same constructor.

## Fix

Two targeted changes, no menu restructuring.

1. `UI::bootYield()` (FurbleUI.cpp) yields the main task once per page while the
   tree is built. It is called at the end of `addMenu`, which is the per-page
   construction boundary and is only reached during startup. `vTaskDelay(1)`
   hands CPU0 to IDLE0 long enough to reset the task watchdog, breaking the one
   long synchronous build into per-page chunks that each finish well under the
   5 s window. It is a no-op under `FURBLE_SIM` (no task watchdog, virtual
   clock). Cost on device is one tick per page at boot, nothing at steady state.

2. Guard the connect-progress setters (FurbleUI.cpp, `connectUpdate`). The
   connect timer fires every 50 ms while connecting and previously called
   `lv_label_set_text(ctx->label, name)` and `lv_bar_set_value(..., LV_ANIM_ON)`
   unconditionally, relabelling and redrawing the progress box on every tick.
   They now only fire when the camera name or progress actually changes, using
   `ConnectContext_t::connectingName` / `connectProgress`, reset at the start of
   each `doConnect`. This is the LVGL periodic-setter trap from CLAUDE.md and it
   trims the sustained invalidation that the AUTOCONNECT path piles onto the
   already-heavy startup.

Prefer fixing the stall over widening the watchdog: a 5 s main-task freeze is a
real UX regression, and `bootYield` removes it at source while preserving
behavior (both saved cameras still render and autoconnect as before).

## Verification

- Build: five release envs + m5stick-s3-debug.
- Host ctests: unchanged, still pass.
- clang-format clean, no em-dashes.
- Sim: the connect-progress guard is simulator-observable via the invalidation
  profiler; the `bootYield` is device-only.

## On-device re-check owed (hardware-gated)

Reflash m5stick-s3 with two or more saved cameras and AUTOCONNECT on. Confirm no
`task_wdt` trip at ~8 s and no multi-second boot freeze, and that both cameras
still render and autoconnect.
