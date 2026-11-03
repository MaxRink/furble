# Display, backlight, and UI power

Status: research lane, display and UI. This file is an evidence base, not a
release gate.

Measurement status: no hardware current measurement was made for this lane.
Every milliamp figure below is a simulator model estimate, a datasheet figure, a
published measurement made by someone else, or an explicit inference. The
committed simulator baselines are relative model evidence, not hardware
measurements. Plan 158 still gates any absolute claim.

## Scope and method

Read for this lane:

- `plans/98-power-optimization-audit.md`, `plans/98b-power-followup.md`
- `plans/implemented/12-display-off.md`, `plans/63-sim-power-analysis.md`,
  `plans/implemented/77-battery-saver-profile.md`, `plans/111-power-recommendations.md`,
  `plans/285-power-accounting.md`
- `src/FurbleUI.cpp`, `src/FurblePower.cpp`, `src/FurblePlatform.cpp`,
  `src/FurbleSettings.cpp`, `src/main.cpp`
- `include/FurbleUI.h`, `include/FurblePower.h`, `include/FurbleSettings.h`,
  `include/FurbleBatterySaver.h`
- all `sdkconfig.*` files
- `sim/power_profiler.cpp`, `sim/scenarios/*`, `tools/power-model/*`
- M5GFX 0.2.27 and M5Unified 0.2.20 board configuration, because this tree has
  no Furble display or backlight board file. The only file in `boards/` is the
  headless Waveshare S3 Ethernet board. Panel pins, backlight drive, and
  brightness mapping come from M5GFX.
- ST7789V, ST7735S, ILI9342C, AXP192, IP5306, and ESP32-S3 datasheets where a
  number is claimed.

Evidence labels used here follow `docs/power/README.md`: measured, modeled,
inferred, unknown. `datasheet` and `published-measurement` name the source of a
number that is not our measurement.

## Current display path

The firmware has three display states: `ACTIVE`, `DIM`, and `OFF`. There is no
staged dim-then-off path.

- Active: backlight at the stored brightness, panel normal mode, APB frequency
  lock held, LVGL rendering on demand.
- Dim: backlight at `m_MinimumBrightness` (32 on most boards, 48 on StickC and
  StickC Plus), panel normal mode, APB lock still held, LVGL still active.
- Off: panel SLPIN, backlight zero, APB lock released, icon timer paused. The
  rest of the UI timers and the UI task keep running.

Key code:

- APB lock acquired for the whole screen-on lifetime:
  `src/FurbleUI.cpp:371-374`.
- Brightness loaded and applied at startup: `src/FurbleUI.cpp:383-385`.
- Per-board dim floor: `src/FurbleUI.cpp:396-411`.
- Sleep and wake: `src/FurbleUI.cpp:1294-1348`.
- Inactivity state machine: `src/FurbleUI.cpp:9837-9891`.
- Flush path: `src/FurbleUI.cpp:1366-1376`.
- UI task loop: `src/FurbleUI.cpp:10177-10218`.
- Defaults: brightness 128, inactivity 0 (Never), screen off 0 (Dim):
  `src/FurbleSettings.cpp:569-575`.
- Battery Saver override: `include/FurbleBatterySaver.h:22-27`.

### Board differences

| Board | Panel | Backlight drive | Brightness mapping | Display power note |
| --- | --- | --- | --- | --- |
| M5StickC | ST7735S, 80x160 | AXP192 LDO2, voltage dimmed | `Light_M5StickC`, brightness mapped to AXP192 register 5..15 | 0.9 mA panel typ, 15 uA sleep-in. No LEDC. |
| M5StickC Plus | ST7789, 135x240 | AXP192 LDO2, voltage dimmed | same `Light_M5StickC` mapping | 6.0 mA panel typ, 15 uA sleep-in. No LEDC. |
| M5StickS3 | ST7789, 135x240 | LEDC PWM, GPIO38, channel 7, 256 Hz, 9-bit | `Light_PWM`, roughly linear duty | 6.0 mA panel typ, 15 uA sleep-in. APB lock has a real reason here. |
| M5Stack Core | ILI9342, 320x240 | LEDC PWM, GPIO32, 44.1 kHz | `Light_PWM` | 6 to 8 mA panel estimate, ILI9342C has no current table. IP5306 cuts power below 45 mA after 32 s. |
| M5Stack Core2 | ILI9342, 320x240 | AXP192 DCDC3, voltage dimmed | `Light_M5StackCore2`, 2.4 to 3.3 V | 6 to 8 mA panel estimate. No LEDC. |

Sources: `src/M5GFX.cpp` in M5GFX 0.2.27 for the StickS3 PWM setup
(`_set_pwm_backlight(GPIO_NUM_38, 7, 256, false, 16)`), the Core PWM setup
(`GPIO_NUM_32, 7, 44100`), and the AXP192 light implementations. Panel currents
are from ST7789V v1.3 Table 3 page 40 and ST7735S v1.1 Table 3 page 30. The
ILI9342C and Core2 backlight figures are `estimated` in
`tools/power-model/board-currents.yaml`.

### State costs

| State, S3 | Modeled mA | Basis |
| --- | --- | --- |
| Active, model's full-brightness basis | 81.2 | `menu-idle-30s` baseline. Model charges the full 35 mA backlight regardless of the stored brightness. |
| Active, inferred at default brightness 128 | about 63.8 | Same MCU and panel, backlight scaled linearly from 35 mA to 128/255. This is an inference, not a measurement. |
| Dim at 32 | 50.6 | `screen-dimmed-30s` baseline. Model uses 32/255 of full backlight. |
| Off | 0.25 | `screen-off-30s` baseline. |
| Connected, screen off, `SLEEP_CONN` off | 43.6 | Plan 98 what-if. |
| Connected, screen off, `SLEEP_CONN` on | 3.61 | Plan 98 what-if. |

The model overstates the active default because `sim/power_profiler.cpp:1232-1236`
charges `display_panel_on + display_backlight` for every millisecond in the `on`
state. It never reads `Settings::BRIGHTNESS`. The firmware default is 128 and
the slider maximum is 240, so the committed 81.2 mA figure is an upper bound at
a full-brightness setting the UI slider cannot select, not the out-of-box
default. Plan 63 describes the baseline as full brightness; plan 98 calls the
same number default brightness. The scenario does not seed 255
(`sim/scenarios/menu-idle-30s.txt`). Correct the label or the model before
quoting 81.2 mA as the default case.

## What is already good, and should not regress

- Panel sleep is real SLPIN, not just backlight zero:
  M5GFX `sleep()` is `setBrightness(0)` then `setSleep(true)`; `wakeup()` is
  `setSleep(false)` then restore brightness. `Panel_LCD::setSleep()` writes
  SLPIN/SLPOUT. Code: `src/FurbleUI.cpp:1305`, `src/FurbleUI.cpp:1335`.
- The 120 ms SLPIN to SLPOUT dwell is enforced in both directions:
  `DISPLAY_SLEEP_DWELL_MS`, `include/FurbleUI.h:897-898`,
  `src/FurbleUI.cpp:1299-1303`, `src/FurbleUI.cpp:1325-1331`.
- The APB lock is released when the panel sleeps and reacquired before wake:
  `src/FurbleUI.cpp:1316-1317`, `src/FurbleUI.cpp:1333-1334`.
- The icon timer is paused while the panel is off:
  `src/FurbleUI.cpp:1314`, `src/FurbleUI.cpp:1344`.
- StickS3 status LED is cut on display off and restored on wake:
  `src/FurblePlatform.cpp:221-254`.
- LVGL uses partial rendering, two DMA buffers, and `pushImageDMA`:
  `src/FurbleUI.cpp:435-438`, `src/FurbleUI.cpp:1373-1374`.
- Page-local timers are mostly paused when their page is closed, for example
  diagnostics, GPS data, NMEA, satellites, level, interval, and bulb.
- Most periodic labels already use changed-only setters. The battery page is the
  main exception.
- There is no continuous 60 fps render loop. LVGL refreshes when invalidated,
  capped by `CONFIG_LV_DEF_REFR_PERIOD=33`, about 30 fps:
  `sdkconfig.m5stick-s3:3359`.

## Model limitations that affect this lane

1. Brightness is absent. Every `on` millisecond is charged the full backlight.
   At the default 128 this overstates the S3 by about 17.4 mA if the PWM
   backlight is linear. That is a large fraction of the 81.2 mA figure.
2. Dim is fixed at 32/255 on every board. The UI dim floor is 48 on StickC and
   StickC Plus, and AXP192 boards are voltage dimmed, not linear duty.
3. Flushed pixels and invalidation events are counted but never charged. The
   model has no SPI, DMA, or CPU draw cost for a refresh.
4. The model has no panel frame rate term. The panel self-refreshes at its
   configured rate even when LVGL is idle.
5. The S3 MCU model has 80, 160, and 240 MHz rows but no 40 MHz row, so a DFS
   improvement below 80 MHz is invisible.
6. The screen-off baseline reports 0.25 mA, but the real firmware never stops
   the 5 ms UI loop. The model assumes the UI task sleeps through those delays.
   That gap is unmeasured.
7. The model has no touch controller, LEDC timer, or panel content term.
8. The 3.3 mA connected-idle floor in `plans/00-hardware-experiments.md` is a
   published or previously recorded observation under different conditions. It
   is not a validation of the model's 3.6 mA what-if.

No default should be changed from these numbers alone. The safe-now list below
is about code hygiene and low-risk mechanism scoping, not about trusting the
absolute model.

## Opportunities

### D-001 Scope the APB lock to boards that need it

- Change: acquire `APB_FREQ_MAX` only where the backlight is an LEDC PWM whose
  visible frequency or duty depends on APB. Today that is StickS3. Keep the lock
  around explicit PWM reconfiguration. Do not hold it for the whole screen-on
  lifetime on the AXP192 boards or the Core, whose backlight is not a 256 Hz
  APB-clocked PWM.
- Current behavior: `UI::UI()` acquires the lock unconditionally and holds it
  until the panel sleeps. The comment names the backlight PWM as the reason.
  StickC, StickC Plus, and Core2 use AXP192 voltage dimming with no LEDC. Core
  uses 44.1 kHz PWM, where an APB change alters frequency but not duty, and
  44.1 kHz remains inaudible and invisible.
- Evidence: modeled plus code-cited. The model charges 25.5 mA for the ESP32 at
  80 MHz and 40.2 mA for the S3 at 80 MHz. ESP-IDF documents that an
  `ESP_PM_APB_FREQ_MAX` lock forces CPU and APB to 80 MHz. Removing it lets DFS
  choose the configured minimum when no other lock is held.
- Estimated saving: upper bound is the modeled MCU term, 25.5 mA on the ESP32
  boards. Expected saving is smaller, about 5 to 10 mA inferred, plus any light
  sleep gained between UI ticks. No S3 saving from this row; see D-007. This is
  not measured.
- Risk: medium. APB changes affect SPI and I2C timing. ESP-IDF SPI master and
  I2C hold their own APB lock during transactions, so the display and touch
  transfers should be safe, but SD, panel timing, and the M5GFX direct paths
  need a bench check.
- Effort: small.
- Required test: A/B build on StickC Plus and Core2. Menu idle, connected idle,
  settings navigation, level page, SD write, touch. Scope SPI clock and the
  backlight rail. Confirm no visual artifact and at least a 5 percent whole
  device current reduction on menu idle. Repeat on Core if time allows.
- Citation: `src/FurbleUI.cpp:371-374`, `src/FurbleUI.cpp:1316-1317`,
  `src/FurbleUI.cpp:1333-1334`; M5GFX `Light_M5StickC` and
  `Light_M5StackCore2`; ESP-IDF Power Management, lock table and DFS section.

### D-002 Add a staged dim then off display policy

- Change: after the inactivity timeout, dim to the minimum; after a second
  fixed interval, sleep the panel. Implement as an additive Display option
  ("Dim then off") or a separate off-delay roller. Do not change the meaning of
  the existing Dim, Off, and Off remote on values.
- Current behavior: mode 0 dims forever. Modes 1 and 2 sleep immediately with no
  visual warning. The default is mode 0 with a Never timeout, so a forgotten
  device can sit at 50.6 mA modeled indefinitely.
- Evidence: modeled. `screen-dimmed-30s` is 50.6 mA; `screen-off-30s` is
  0.25 mA. The difference is 50.3 mA modeled. The panel sleep path and the
  120 ms dwell already exist.
- Estimated saving: up to 50.3 mA modeled when the new policy replaces an
  indefinite dim. At the default brightness this is the same because dim is
  already at 32.
- Risk: low to medium. The mechanism is proven. The risk is UX surprise and a
  new state transition that must not issue SLPIN twice or wake unintentionally.
- Effort: medium.
- Required test: host state machine test for Active to Dim to Off and wake from
  each state; simulator scenario with a short timeout; hardware 30 minute soak;
  confirm one SLPIN per timeout and wake below 150 ms; docs and UI screenshots.
- Citation: `src/FurbleUI.cpp:9837-9891`, `src/FurbleUI.cpp:1294-1348`,
  `include/FurbleUI.h:659-663`, `include/FurbleUI.h:897-898`;
  `tools/power-model/baseline/screen-dimmed-30s.json` and
  `screen-off-30s.json`.

### D-003 Do not flush to a sleeping panel

- Change: in `displayFlush`, if the panel is off, call `lv_disp_flush_ready`
  without `lv_draw_sw_rgb565_swap` and `pushImageDMA`. On wake, invalidate the
  full screen once before the next render. Optionally suspend the LVGL refresh
  timer while off.
- Current behavior: the icon timer is paused, but the inactivity, battery, GPS,
  and connect timers still run, and any invalidation is rendered and pushed to a
  panel in SLPIN. The committed blind-remote baseline records 31,301 flushed
  pixels over 30 s while `display_ms.off` is 30,000 ms. That is wasted SPI, DMA,
  and CPU work.
- Evidence: simulated counter plus code-cited. `displayFlush` has no display-off
  check. The baseline report records nonzero `flushed_pixels` with the panel
  off.
- Estimated saving: below 1 mA inferred. The transfer itself is small at
  1.04k pixels/s, but it is pure waste and it keeps the UI task busy.
- Risk: low. The main risk is a black or stale screen on wake if the full
  invalidate is missing, or a DMA race if the panel sleeps during an in-flight
  transfer. Call `M5.Display.waitDMA()` before SLPIN.
- Effort: small.
- Required test: extend `blind-remote-shutter` to assert flushed pixels are
  zero while off; wake screenshot matches the pre-sleep screen; 100 sleep and
  wake cycles; no tearing or stale pixels.
- Citation: `src/FurbleUI.cpp:1366-1376`, `src/FurbleUI.cpp:1294-1318`,
  `sim/power_profiler.cpp:1669-1673`,
  `tools/power-model/baseline/blind-remote-shutter.json`.

### D-004 Make battery labels changed-only and page-visible only

- Change: use the existing `setLabelIfChangedFmt` and `setLabelTextIfChanged`
  helpers for the battery page rows. The header label is already changed-only in
  the icon timer. Skip the page row writes unless the Battery page is the active
  menu page, and call `lv_timer_ready` when the page opens so the first view is
  current.
- Current behavior: `batteryUpdate` runs every 5 s and unconditionally writes up
  to five labels. The labels are created at UI construction, so they exist even
  when the page is hidden. The timer also has to run for the low-battery policy,
  so it cannot simply be paused.
- Evidence: modeled counter plus code-cited. The menu-idle baseline records
  63,823 invalidated pixels over 30 s. The battery timer is a plausible
  contributor; this is not proven by a differential run.
- Estimated saving: below 0.5 mA inferred. The value is fewer invalidations and
  less hidden-page rendering, not a large current term.
- Risk: low.
- Effort: small.
- Required test: extend `redraw-steady` to menu idle and the Battery page;
  assert the invalidation count stays low and values still update within one
  sample period; check the header battery label and icon.
- Citation: `src/FurbleUI.cpp:575`, `src/FurbleUI.cpp:8394-8424`,
  `src/FurbleUI.cpp:8434-8478`; `sim/scenarios/e2e/redraw-steady.txt`.

### D-005 Reduce the icon timer from 4 Hz to 1 Hz

- Change: set the icon timer period to 1000 ms. Battery sampling remains at
  5 s and GPS service at 1 s, so 1 Hz is sufficient. Keep immediate refresh on
  state changes where the code already has one.
- Current behavior: the icon timer fires every 250 ms, 120 times per 30 s in the
  menu-idle baseline. It checks the battery icon, battery label, and GPS icon.
- Evidence: code-cited plus simulator timer counts.
- Estimated saving: below 0.1 mA inferred. This is a wakeup and callback
  reduction, not a large current lever.
- Risk: low. Battery and GPS icons can lag by up to 1 s.
- Effort: tiny.
- Required test: confirm the battery icon and GPS icon update within 1.2 s of a
  state change; confirm the timer stays paused while the panel is off.
- Citation: `src/FurbleUI.cpp:581-631`,
  `tools/power-model/baseline/menu-idle-30s.json` (`icon_timer` count 120).

### D-006 Remove permanent scroll animations from settings and menu rows

- Change: replace `LV_LABEL_LONG_SCROLL_CIRCULAR` on setting labels and icon
  menu labels with `LV_LABEL_LONG_DOT` or `LV_LABEL_LONG_WRAP`, or scroll only
  while the row is focused. Keep the camera-name rows wrapped, as they already
  are.
- Current behavior: setting labels and icon menu labels use circular scrolling.
  When the text is wider than the object, LVGL runs a permanent animation that
  invalidates the row every frame. The project guide names this exact redraw
  trap. The existing `redraw-steady` scenario covers the connected page, the
  level page, and a steady tilt, but not an idle settings page.
- Evidence: code-cited plus the project redraw rule. The battery and settings
  pages are the likely idle redraw sources, but no differential run isolates
  this one.
- Estimated saving: unknown, likely page-local. It could be a few percent of the
  UI work while a settings page is open, not a whole-device figure.
- Risk: low to medium. Truncated labels or a changed layout need screenshot
  review.
- Effort: small to medium.
- Required test: add a `redraw-steady` variant on Settings > Display and
  Settings > Power, assert the invalidation count over 1 s stays at or below the
  existing threshold, and capture screenshots for text readability.
- Citation: `src/FurbleUI.cpp:1879-1900`, `src/FurbleUI.cpp:1921-1924`,
  `src/FurbleUI.cpp:8150`, `src/FurbleUI.cpp:8204`, `src/FurbleUI.cpp:8235`;
  `sim/scenarios/e2e/redraw-steady.txt`.

### D-007 S3: move the backlight PWM to RC_FAST and keep it alive in light sleep

- Change: configure the StickS3 LEDC timer to `LEDC_USE_RC_FAST_CLK` and the
  channel to `LEDC_SLEEP_MODE_KEEP_ALIVE`, then release the APB lock while the
  panel is on. Keep the current APB path as a fallback. M5GFX does not expose
  the clock source or sleep mode through `Light_PWM` config, so this needs a
  small M5GFX patch or a direct LEDC reconfiguration after display init.
- Current behavior: M5GFX configures 256 Hz, 9-bit PWM on GPIO38 and leaves the
  clock source at the default, which selects APB. The UI therefore holds
  `APB_FREQ_MAX` for the whole screen-on lifetime.
- Evidence: code-cited plus datasheet and ESP-IDF. The ESP32-S3 LEDC supports
  RC_FAST, which is dynamic-frequency-scaling compatible and light-sleep
  compatible. `KEEP_ALIVE` keeps the PWM output during light sleep at the cost
  of keeping the LEDC clock and power domain alive.
- Estimated saving: up to 40.2 mA modeled, the full S3 MCU term at 80 MHz.
  Realistically 10 to 30 mA inferred if the CPU can sleep between frames, but
  this depends on the 5 ms UI loop and BLE locks. It is the largest display
  adjacent opportunity and the least certain.
- Risk: high. Flicker, duty drift from the RC oscillator, extra sleep current
  from `KEEP_ALIVE`, backlight failure on wake, and BLE timing interactions.
  RC_FAST is about 17.5 MHz and is temperature dependent; 9-bit at 256 Hz still
  has ample divisor headroom, but the duty must be verified.
- Effort: medium to large.
- Required test: scope GPIO38 across DFS and light sleep; photodiode or
  high-speed camera for flicker; current at screen on idle with the APB lock
  released; 100 wake cycles; 24 hour soak; BLE connect and shutter latency.
  Reject if flicker is visible or if light sleep residency does not improve.
- Citation: M5GFX `src/M5GFX.cpp` StickS3 backlight setup and
  `src/lgfx/v1/platforms/esp32/Light_PWM.inl`; ESP-IDF LEDC Power Management
  section, RC_FAST and `LEDC_SLEEP_MODE_KEEP_ALIVE`; `src/FurbleUI.cpp:371-374`.

### D-008 Throttle UI polling and housekeeping while the panel is off

- Change: raise the UI task delay from 5 ms to 20 ms when the panel is off, and
  consider 10 ms while on. Slow the inactivity check to 2 s when off. Keep the
  battery sample at 5 s for the low-battery policy and keep the connect liveness
  poll. Enable `CONFIG_PM_RTOS_IDLE_OPT` as part of the same measurement.
- Current behavior: the UI task calls `Platform::update()`, service work, and
  `lv_task_handler()` every 5 ms, 200 times per second, whether the panel is on,
  dim, or off. `CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP=3` means the idle task
  must see 3 ticks of idleness before sleeping, so a 5 ms delay leaves at most
  about 2 ms of possible light sleep per cycle before overhead. The simulator
  assumes the UI task sleeps through those delays and reports 0.25 mA for screen
  off, which is why the model cannot settle this.
- Evidence: code-cited plus sdkconfig. The simulator records 6,002 UI task
  delays over 30 s, exactly 200 per second.
- Estimated saving: unknown. The gap between the modeled 0.25 mA and the real
  screen-off current is the prize. Fewer wakeups also reduce the chance that the
  tickless idle path never reaches its 3 tick threshold.
- Risk: medium. Input latency, watchdog feed margin, intervalometer timing,
  connect liveness, and blind-remote shutter response all depend on this loop.
- Effort: small to medium.
- Required test: bench screen-off disconnected current at 5, 10, 20, and 50 ms
  delays; measure wake latency from each input; run the intervalometer 5 frame
  scenario and blind remote; 24 hour soak with the watchdog enabled; confirm no
  missed feed and no BLE drop.
- Citation: `src/FurbleUI.cpp:10217`, `src/FurbleUI.cpp:10177-10218`,
  `sdkconfig.m5stick-s3:2466`, `sdkconfig.m5stick-s3:2491-2492`,
  `sdkconfig.m5stick-s3:2115`; `sim/power_profiler.cpp:1675-1699`.

### D-009 Change the default screen policy

- Change: ship inactivity 60 s and Screen off Off by default, or make Battery
  Saver the default. Keep an opt-out. This is a policy change, not a mechanism.
- Current behavior: fresh NVS gets brightness 128, inactivity 0 (Never), and
  screen off 0 (Dim). Battery Saver is opt-in and off by default. A user who
  never opens Settings leaves the panel on at the stored brightness forever.
- Evidence: modeled plus the plan 98, 77, and 111 analyses. The committed
  menu-idle baseline is 81.2 mA on the model's full-brightness basis, about
  63.8 mA inferred at the actual default brightness. Screen off is 0.25 mA
  modeled.
- Estimated saving: up to 81.2 mA modeled, about 64 mA inferred at default
  brightness, after the timeout. This is the largest user-visible saving in the
  lane, but it is a product decision and it is not measured.
- Risk: medium to high. Users may think the device died. On M5Stack Core the
  IP5306 hard-powers-off after 32 s below 45 mA, so a disconnected screen-off
  state there is effectively device off. Accessibility and outdoor use also
  matter.
- Effort: small code change, large verification and documentation effort.
- Required test: fresh NVS boot on all five release boards; confirm the timeout
  and mode; confirm Core behavior is documented; 30 minute soak per board;
  wake latency; user-facing docs and screenshots. Do not flip until the bench
  measurement in the measurement plan supports it.
- Citation: `src/FurbleSettings.cpp:569-575`,
  `include/FurbleBatterySaver.h:22-27`, `src/FurbleUI.cpp:386-390`;
  `plans/98-power-optimization-audit.md:124-156`,
  `plans/implemented/77-battery-saver-profile.md:45-62`,
  `plans/111-power-recommendations.md:29-86`.

### D-010 Lower the default brightness, or add a profile

- Change: change the default brightness from 128 to 96 or 64, or provide an
  indoor and outdoor profile. Keep the slider range and the live preview.
- Current behavior: default 128 of 255, slider 32 to 240 in steps of 16. The
  backlight is roughly linear with duty on the S3.
- Evidence: code-cited plus modeled backlight. The S3 full backlight is
  estimated at 35 mA. A 32 count change is about 4.4 mA inferred.
- Estimated saving: 128 to 96 about 4.4 mA inferred; 128 to 64 about 8.8 mA
  inferred. That is per 32 brightness counts, two slider steps. S3 only. AXP192
  boards are voltage dimmed and nonlinear; measure them separately.
- Risk: medium. Outdoor readability and user preference. A darker default can
  make the device look dim.
- Effort: small.
- Required test: bench brightness sweep with a fixed page, current and lux or
  photo measurement; outdoor readability check; document the new default and
  the slider range.
- Citation: `src/FurbleSettings.cpp:569-571`, `src/FurbleUI.cpp:8153-8199`,
  `tools/power-model/board-currents.yaml` StickS3 `backlight_full`.

### D-011 Teach the model effective brightness and per-board dim floors

- Change: add the effective brightness to the simulated display state, charge
  the backlight proportionally for the S3 PWM path, use the real dim floor per
  board, and add a 40 MHz operating point from a measurement or a suitable
  datasheet row. Keep the current full-brightness field as a separate
  calibration anchor.
- Current behavior: the model charges 35 mA for any `on` state and 32/255 of 35
  for any `dim` state, regardless of `Settings::BRIGHTNESS`, the 48 dim floor on
  StickC and StickC Plus, or the AXP192 voltage curve.
- Evidence: code-cited. This is a tooling correction, not a device saving. It
  removes a 17.4 mA overstatement at the default brightness and prevents the
  next ranking from treating the 81.2 mA full-brightness number as the default.
- Estimated saving: none directly. It changes which opportunities look large.
- Risk: none to the device. The risk is churn in the committed baselines and
  the comparator.
- Effort: small.
- Required test: unit tests for the brightness term and per-board dim floor;
  regenerate baselines with a stated reason; run `compare.py` in both
  directions; document the model change and the new digest.
- Citation: `sim/power_profiler.cpp:1232-1236`,
  `tools/power-model/board-currents.yaml`, `src/FurbleUI.cpp:396-411`,
  `src/FurbleSettings.cpp:569-571`.

### D-012 Lower the panel frame rate while idle

- Change: use the ST7789 `FRCTRL2` command to run a lower panel refresh rate
  when the UI is idle, and restore 60 Hz for animated pages.
- Current behavior: the panel runs at its configured rate, about 60 Hz, and
  self-refreshes from GRAM. LVGL only writes when invalidated. The datasheet
  Table 3 current is specified at 60 Hz only.
- Evidence: datasheet plus code. There is no datasheet row for 40 or 30 Hz, so
  the saving is unknown.
- Estimated saving: likely below 1 mA. Drop the idea if the bench does not show
  at least a 1 mA panel current reduction.
- Risk: medium. Flicker, tearing, and interaction with LVGL's own refresh. M5GFX
  deliberately leaves `FRCTRL2` out of its ST7789 init sequence because panel
  variants in this class have different specifications, so this must be board
  scoped and verified.
- Effort: medium, because M5GFX has no high-level frame rate API for this.
- Required test: panel current at 60, 40, and 30 Hz with a fixed image; visual
  flicker check; LVGL navigation and level page; reject below 1 mA.
- Citation: ST7789V v1.3 Table 3 page 40 and `FRCTRL2 (C6h)`; M5GFX
  `Panel_ST7789.hpp`, where `CMD_FRCTR2` is commented out for the M5StickCPlus
  panel variants.

### D-013 Use the touch controller low-power monitor on Core2 and Tough

- Change: put the touch controller into its monitor or low-power mode while the
  panel is off, with an interrupt to wake on tap. Keep the current always-on
  behavior as a fallback.
- Current behavior: plan 12 deliberately leaves the touch controller running so
  a tap can wake the screen. Its current is not in `board-currents.yaml`.
- Evidence: code and plan cited; current unknown.
- Estimated saving: unknown. It is board-specific and may be a few hundred
  microamps to a few milliamps.
- Risk: medium. Wake reliability and touch calibration after resume.
- Effort: medium.
- Required test: touch controller current in monitor mode versus idle; tap
  wake from panel off; 24 hour soak; calibration retained.
- Citation: `plans/implemented/12-display-off.md:121-124`, `src/FurbleUI.cpp:1416-1428`,
  `src/FurbleUI.cpp:1294-1348`.

### D-014 Stop the LEDC timer when the backlight is zero

- Change: pause or stop the LEDC timer when brightness reaches zero or the
  panel sleeps, and reconfigure it before restoring brightness.
- Current behavior: `M5.Display.sleep()` calls `setBrightness(0)`, which writes
  duty zero, but the LEDC timer keeps running at 256 Hz.
- Evidence: code-cited. The LEDC timer current is not specified in the
  datasheet or the model.
- Estimated saving: below 0.1 mA inferred.
- Risk: low to medium. A failed restore leaves the backlight dark.
- Effort: small.
- Required test: current delta at panel off; 100 wake cycles; backlight restore
  at every brightness; no flicker.
- Citation: M5GFX `LGFXBase.hpp` `sleep()` and `Light_PWM.inl`;
  `src/FurbleUI.cpp:1305`, `src/FurbleUI.cpp:1335`.

## Ranked lists

### Safe now

These are low-risk code or configuration changes whose mechanism is already
proven. They still need the stated test before merge, but they do not require a
new bench measurement to decide whether to implement them.

1. D-001 Scope the APB lock to StickS3. Applies to every screen-on minute on
   four boards, so it has the widest coverage in the safe list: up to 25.5 mA
   modeled MCU, about 5 to 10 mA inferred.
2. D-002 Add staged dim then off. Up to 50.3 mA modeled after the second
   timeout, with no new hardware mechanism.
3. D-003 Skip flush while the panel is off. Removes 1.04k flushed pixels per
   second in the blind-remote baseline; below 1 mA inferred.
4. D-004 Battery labels changed-only and visible-only. Below 0.5 mA inferred.
5. D-005 Icon timer 4 Hz to 1 Hz. Below 0.1 mA inferred.
6. D-006 Remove permanent scroll animations from settings and menu rows.
   Unknown saving, but it removes a known idle redraw trap on open pages.

These savings are not additive. D-001 and D-002 overlap in the screen-on
window, D-002 and D-009 both change how long the panel stays on, and D-010
changes the active brightness that D-001 and D-009 are measured against.

### Needs measurement

These need a bench trace or a product decision before they can be ranked or
enabled by default.

1. D-007 S3 RC_FAST LEDC with `KEEP_ALIVE`. Largest possible saving, highest
   risk: up to 40.2 mA modeled, 10 to 30 mA inferred.
2. D-008 Throttle the UI loop and housekeeping while off. The prize is the gap
   between the modeled 0.25 mA screen-off floor and the real 5 ms loop.
3. D-009 Default screen policy. About 64 mA inferred at the actual default
   brightness, up to 81.2 mA modeled at full brightness, after the timeout.
4. D-010 Default brightness. About 4.4 mA per 32 brightness counts on the S3,
   inferred.
5. D-011 Model brightness and per-board dim floors. No direct saving; required
   before the next ranking is trustworthy.
6. D-012 Panel frame rate. Only if the bench shows at least 1 mA.
7. D-013 Touch low-power monitor on Core2 and Tough. Board-specific and
   unmodeled.
8. D-014 Stop the LEDC timer at zero brightness. Below 0.1 mA inferred.

### Rejected

| ID | Idea | Why rejected |
| --- | --- | --- |
| R-01 | Use ST7789 idle mode `IDMON` while the UI is on | Idle mode reduces the panel to 8 colors and corrupts the UI. Table 3 gives only 5.0 mA for partial plus idle versus 6.0 mA normal, about 1 mA, and no full-display idle row. Not worth the visual break. |
| R-02 | Enlarge the LVGL draw buffers | The buffer is already 1/15 of the largest panel, with two buffers and DMA. A larger buffer reduces the number of window transactions for a full frame but not the pixels transferred. RAM cost is not justified by the small expected saving. |
| R-03 | Drop LVGL to 8-bit color or dither | The panel idle mode is RGB111 only. A real 8-bit path needs dithering and still changes the UI. The backlight, not the panel data format, dominates display power. |
| R-04 | Use a dark theme as a display power lever | The backlight current is independent of content. The datasheet does not give a content-dependent panel current table, and the UI already has a dark theme for preference. No measurable basis for a power claim. |
| R-05 | Avoid the full repaint on level rotation | The level page rotates the panel and changes the logical resolution. A full invalidate plus `lv_refr_now` is the correctness fix for the half-written screen bug. The cost is a rare 32,400 pixel pass on the 135x240 sticks, not a steady load. |
| R-06 | Remove short bar, slider, and roller animations | These are bounded to an interaction and do not run while idle. The saving is negligible and the UX loss is visible. |
| R-07 | Change the backlight PWM frequency | 256 Hz on the S3 is already low, and switching loss is negligible compared with LED current. A frequency change does not reduce the LED current at a fixed duty. |
| R-08 | Shorten or skip the boot splash | It runs for a few seconds per boot, not in steady state. The saving is too small to matter and the splash is a deliberate feature. |
| R-09 | Add adaptive brightness | No supported board has an ambient light sensor. The BLE camera link and the UI do not provide a lux measurement. |
| R-10 | Pause the inactivity timer while the panel is off | The timer is what wakes the panel after activity from the IMU or another non-input source, and it enforces the display policy. Slow it under D-008, do not pause it. |
| R-11 | Disable the touch controller outright while the panel is off | It removes tap to wake on Core2 and Tough. D-013 is the low-power monitor version of this idea. |
| R-12 | Reset or power-cycle the panel controller on every sleep | SLPIN is already 15 uA typical, 30 uA maximum on ST7789. A reset adds no measurable benefit and risks panel state loss. |
| R-13 | Enable the ST7789 content adaptive brightness control (CABC) | It reduces panel current by changing image brightness and contrast, not by reducing the LED backlight. The UI would visibly shift. The datasheet target is a panel-side reduction, and the panel is only about 6 mA against a 35 mA full backlight on the S3. Not a clean power lever. |

## Measurement plan

No measurement was made for this report. The plan below is what would close the
gaps. Use one electrical boundary and keep it identical across every run.

### Instrumentation

- Calibrated inline power analyzer: Joulescope JS220, Nordic PPK2, Otii Arc, or
  Keysight N6705B. Record model, serial, firmware, range, sample rate, supply
  voltage, cable, battery presence, and whether USB data is attached.
- Measure at the battery lead or the complete device input with charging
  disabled. Do not mix a battery-side run with a USB-input run.
- M5StickS3 has no usable battery current telemetry in M5Unified and no
  documented M5PM1 current register. `power stats` and `power log` are state
  evidence only on that board, not current measurements.
- Scope or logic analyzer on the S3 backlight GPIO38, SPI clock and MOSI, and
  the panel SLPIN/SLPOUT commands where accessible. A photodiode on the panel
  is the practical flicker detector.
- Flash `m5stick-s3-debug` or the matching debug env for synchronized firmware
  state. Use a GPIO marker or a distinctive UI action to align the analyzer
  trace with the firmware log.
- Use `sim/build/furble-sim` and `tools/power-model/compare.py` only for
  relative regression checks. A simulator report is not a bench measurement.

### States to capture

| ID | State | Setup | Duration and repeats | What it answers |
| --- | --- | --- | --- | --- |
| M-01 | Menu idle, brightness 128 | Disconnected, main menu | 10 min, 3 repeats | Baseline active current at the real default, not the model's full-brightness basis |
| M-02 | Brightness sweep | Fixed menu page, 32, 64, 96, 128, 160, 192, 240 | 5 min each, 2 repeats | Backlight linearity and D-010 saving |
| M-03 | Dim at minimum | Inactivity or console timeout, mode Dim | 10 min, 3 repeats | Dim floor and the model's 32/255 assumption |
| M-04 | Off, disconnected | Screen off mode Off | 30 min, 3 repeats | Real screen-off floor and D-008 gap |
| M-05 | Connected, screen off | Fujifilm connected, Battery Saver off then on | 10 min each, 3 repeats | Display-off plus `SLEEP_CONN` interaction, the plan 111 headline pair |
| M-06 | Blind remote | Connected, screen off, Off remote on, 10 shutter presses | 5 min | D-003 flush waste and per-press cost |
| M-07 | Settings page idle | Settings > Power or Display, no input | 10 min | D-006 scroll animation and D-004 hidden label redraw |
| M-08 | Settings navigation sweep | Open every settings page once | 2 min, 3 repeats | Full refresh and flush cost per transition |
| M-09 | Level page | Flat and fixed tilt, then rotate to side | 5 min | Level timer, full repaint on rotation |
| M-10 | Sleep and wake cycles | 100 off and on cycles with a button and with the IMU | 100 cycles | SLPIN/SLPOUT dwell, wake latency, energy per transition |
| M-11 | APB lock A/B, non-S3 | StickC Plus and Core2, lock held versus D-001 build | 10 min each | D-001 saving and any SPI or touch artifact |
| M-12 | APB and LEDC A/B, S3 | Current build versus D-007 RC_FAST `KEEP_ALIVE` build | 10 min each plus 24 h soak | D-007 saving, flicker, duty stability, light sleep residency |
| M-13 | UI loop sweep | Screen off, 5, 10, 20, and 50 ms UI delay | 10 min each | D-008 wakeup and current tradeoff |
| M-14 | Panel frame rate | Fixed image, 60, 40, and 30 Hz | 5 min each | D-012 panel current delta |
| M-15 | Touch monitor | Core2 or Tough, touch idle versus monitor mode | 10 min each | D-013 current and tap wake |
| M-16 | LEDC stop | S3, panel off with timer running versus stopped | 10 min each | D-014 delta and wake restore |

For every state record mean, median, p95, wake-event energy, and a confidence
interval over the same window. Retain the raw trace, not only the summary.
Reject a run that silently changes display state, brightness, connection
parameters, or GPS state.

### Acceptance thresholds

These are proposed decision thresholds, not calibrated hardware noise floors.
Set the real band from repeated baseline runs before enforcing any of them.

- D-001: at least 5 percent whole-device current reduction on StickC Plus menu
  idle; no SPI error, no touch failure, no visible artifact.
- D-002: steady off current within 0.2 mA of the existing Off mode; one SLPIN
  per timeout; wake below 150 ms; no double sleep.
- D-003: `flushed_pixels` is zero while `display_ms.off` covers the whole
  window; the wake screen is pixel-correct against a capture.
- D-004: no more than three invalidation events in a 1 s steady menu or battery
  page window, matching the existing `redraw-steady` threshold.
- D-005: icon state visible within 1.2 s; no increase in invalidation events.
- D-006: no periodic invalidation on a static settings page beyond the existing
  redraw-steady threshold; screenshots show readable labels.
- D-007: at least 10 mA reduction at screen on idle; light sleep residency above
  50 percent while idle; PWM frequency within 1 percent and duty within 2
  percent across DFS and sleep; no flicker; no lost BLE events over 30 min.
- D-008: screen-off average current lower than the 5 ms baseline; wake below
  50 ms; watchdog fed; intervalometer jitter within its current tolerance.
- D-009: after the timeout the current equals the Off state; no unexpected
  power-off on Core beyond the documented IP5306 behavior.
- D-010: current changes monotonically with brightness; the chosen default is
  readable outdoors; document the measured curve.
- D-011: model output changes only by the intended brightness term; comparator
  passes in both directions; the new model digest is recorded.
- D-012: at least 1 mA panel current reduction, otherwise reject.
- D-013: at least 0.5 mA reduction with tap wake working, otherwise reject.
- D-014: at least 0.05 mA reduction with reliable backlight restore, otherwise
  reject.

### Model work before the next ranking

1. Record effective brightness in the scenario state and charge the backlight
   from it. For the S3 PWM path, a linear duty term is a reasonable first
   approximation; label it inferred until the sweep exists.
2. Use the real per-board dim floor, 32 or 48, and a measured AXP192 voltage
   curve rather than a linear duty assumption.
3. Add a 40 MHz MCU row or remove the 80 MHz floor so D-001 and D-008 are
   visible to the model.
4. Charge flushed pixels separately from invalidated pixels. Invalidation is
   not energy; only flush and draw work is. The settings sweep already shows a
   7.2 to 1 invalidation to flush ratio.
5. Keep the simulator's display state transitions but stop claiming the screen
   off 0.25 mA is the hardware floor until M-04 exists.

## Full refresh and frame rate arithmetic

These are arithmetic bounds, not measurements.

- One full S3 frame is 135 x 240 = 32,400 pixels.
- At 16 bits per pixel and a 40 MHz SPI write clock, the raw pixel transfer is
  about 13.0 ms per full frame. That is the floor before command overhead,
  window setup, and CPU draw time.
- The LVGL buffer is 4,320 bytes, 2,160 pixels at 16 bpp, 1/15 of a 135 x 240
  panel. A full refresh is therefore about 15 partial flushes.
- The committed menu-idle baseline records 1.17k flushed pixels per second,
  about 0.036 full frames per second.
- The committed settings-navigation baseline records 47.8k flushed pixels per
  second, about 1.5 full frames per second, and 344k invalidated pixels per
  second. The invalidation to flush ratio is about 7.2 to 1.
- At 30 fps continuous full refresh the SPI transfer alone would be about 39
  percent duty. The UI does not do that; it renders on invalidation.
- These numbers explain why frame rate is a weak battery lever here. The panel
  current is specified at 60 Hz and the SPI transfer is a small duty cycle even
  during navigation. The dominant display cost is the backlight, then the MCU
  held at 80 MHz by the APB lock.

## Screen transitions and animation

- Menu page changes invalidate the full page. The settings sweep is the upper
  bound for normal use at about 1.5 full frames per second. There is no
  continuous transition animation to remove.
- The level page rotation is the only forced synchronous full repaint:
  `lv_obj_invalidate(screen)` then `lv_refr_now(display)` then
  `M5.Display.waitDMA()` at `src/FurbleUI.cpp:6259-6267`. It exists to avoid a
  half-written rotated screen. Keep it.
- Short animations on the connect progress bar, sliders, and rollers are
  bounded to an interaction. They are not idle loads.
- Permanent circular scrolling on labels is the one animation that can run
  while the user is reading a static page. D-006 addresses it.
- The display sleep path itself blocks the UI task for up to 120 ms to honor the
  panel dwell. That is a latency cost, not an energy cost.

## UI polling inventory

| Source | Period | Runs while panel off | Note |
| --- | --- | --- | --- |
| UI task loop | 5 ms | Yes | `Platform::update()`, request service, `lv_task_handler()`; 200 Hz |
| Inactivity timer | 1 s | Yes | Drives dim, off, auto off, low battery, IMU poll |
| Battery timer | 5 s | Yes | PMIC sample and low-battery policy; cannot be paused |
| Icon timer | 250 ms | No, paused | Battery and GPS icons |
| Connect timer | 50 ms connecting, 500 ms active | Yes when connected | Liveness poll cannot be paused without losing drop detection |
| Companion pairing timer | 250 ms | Yes if companion enabled | Default off |
| Gesture timer | 20 ms | Yes if IMU gestures enabled | Default IMU off |
| Diagnostics timer | 1 s | Only when page open | Page local |
| GPS data, NMEA, satellite timers | 1 s | Only when page open | Page local |
| Level timer | 50 ms | Only when page open | Holds display awake by design |
| Interval and bulb timers | 100 ms and page refresh | Only during a run | Functional timers |

The 5 ms UI loop is the largest polling term. The 1 s inactivity and 5 s
battery timers are needed for policy and should be slowed only with a stated
behavior change. The 500 ms connected liveness poll is already gentle.

## References

### Furble code

- `src/FurbleUI.cpp`
- `src/FurblePower.cpp`
- `src/FurblePlatform.cpp`
- `src/FurbleSettings.cpp`
- `include/FurbleUI.h`
- `include/FurbleBatterySaver.h`
- `sim/power_profiler.cpp`
- `tools/power-model/board-currents.yaml`
- `tools/power-model/README.md`
- `tools/power-model/baseline/*.json`

### Furble plans

- `plans/implemented/12-display-off.md`
- `plans/63-sim-power-analysis.md`
- `plans/implemented/77-battery-saver-profile.md`
- `plans/98-power-optimization-audit.md`
- `plans/98b-power-followup.md`
- `plans/111-power-recommendations.md`
- `plans/285-power-accounting.md`

### Datasheets

- Sitronix ST7789V, version 1.3, Table 3 page 40: normal mode 6.0 mA typ,
  7.5 mA max; sleep-in 0.015 mA typ, 0.03 mA max at 60 Hz, VDD 2.75 V.
  `https://newhavendisplay.com/content/datasheets/ST7789V.pdf`
- Sitronix ST7735S, version 1.1, Table 3 page 30: normal mode 0.9 mA typ,
  2.0 mA max; sleep-in 0.015 mA typ, 0.03 mA max.
  `https://cdn.sparkfun.com/assets/9/0/2/5/8/ST7735S_v1.1.pdf`
- ILITEK ILI9342C: no operating current table in the datasheet; the model uses
  an ST7789V analogy, tagged estimated.
  `https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/ILI9342C-ILITEK.pdf`
- Espressif ESP32-S3 Series Datasheet v2.2, Tables 5-9 and 5-10: 80, 160, and
  240 MHz active and idle rows. There is no 40 MHz row in the model.
  `https://documentation.espressif.com/esp32-s3_datasheet_en.pdf`
- X-Powers AXP192 datasheet: LDO2 and DCDC3 voltage control and per-block
  quiescent figures.
  `http://images.shoutwiki.com/mindworks/8/8b/2020_infrasonic_wildfire_detector_APX192_Enhanced_Single_Cell_datasheet_en.pdf`
- Injoinic IP5306 datasheet v1.01: standby 50 uA, boost cutoff below 45 mA for
  32 s.
  `https://www.laskakit.cz/user/related_files/ip5306.pdf`

### Library and platform documentation

- M5GFX 0.2.27, `src/lgfx/v1/LGFXBase.hpp`: `sleep()` is
  `setBrightness(0)` then `setSleep(true)`; `wakeup()` is `setSleep(false)` then
  restore brightness.
  `https://github.com/m5stack/M5GFX`
- M5GFX `src/lgfx/v1/panel/Panel_LCD.inl`: `setSleep()` writes SLPIN or SLPOUT.
- M5GFX `src/lgfx/v1/platforms/esp32/Light_PWM.inl`: 9-bit PWM, default clock
  source, duty mapping.
- M5GFX `src/M5GFX.cpp`: StickS3 `_set_pwm_backlight(GPIO_NUM_38, 7, 256, false,
  16)`, Core `_set_pwm_backlight(GPIO_NUM_32, 7, 44100)`, AXP192
  `Light_M5StickC` and `Light_M5StackCore2`.
- ESP-IDF Power Management for ESP32-S3: `ESP_PM_APB_FREQ_MAX` forces CPU and
  APB to 80 MHz; no locks allows the configured minimum and light sleep.
  `https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html`
- ESP-IDF LEDC for ESP32-S3: RC_FAST is DFS and light-sleep compatible;
  `LEDC_SLEEP_MODE_KEEP_ALIVE` keeps the PWM output during light sleep.
  `https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/ledc.html`
- LVGL 9.4 refresh period, partial rendering, and `LV_LABEL_LONG_SCROLL_CIRCULAR`
  behavior. The build pins LVGL 9.4.0 in `sim/platformio.ini`; the ESP-IDF
  component uses `CONFIG_LV_DEF_REFR_PERIOD=33`.
