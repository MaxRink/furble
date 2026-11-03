# 101: Power-on boot splash

Status: implemented on branch `feat/boot-splash`, off fork/master. Sim and
firmware builds pass. On-device look is PENDING HARDWARE RETEST (see below).

## Motivation

furble boots to a black screen until the LVGL UI constructor finishes, which
runs last, after settings, display, feedback, storage, Bluetooth and the
companion service have all come up. On a cold boot that gap is a second or more
of nothing, and a boot that recovered from a watchdog reset or an OTA fallback
gives the user no signal that anything happened. A power-on splash fills that
gap with the furble identity and real boot progress, and briefly surfaces the
firmware version, board and last reset reason, which are exactly the facts you
want when a device just reset itself.

## Behavior

- The splash is drawn straight to the panel with M5GFX, not LVGL. The display
  comes up inside `Platform::init()`, well before LVGL exists, so M5GFX is the
  only drawing surface available during the init stages. This also means the
  splash has no focusable widget and cannot draw an unwanted focus outline.
- Layout is proportional to the panel and verified to fit 80x160, 135x240 and
  320x240: a centered `furble` wordmark, a small info block (version, board,
  reset reason), a stage label and a progress bar near the bottom.
- `app_main()` drives the progress. `BootScreen::begin(6)` draws the splash
  right after `Platform::init()`, then a `step()` call follows each real init
  stage so the bar tracks actual work, not a timer:
  1. Infrared (`IR::init`)
  2. Feedback (`Feedback::init`)
  3. Storage (`SD::init`)
  4. Power (CPU frequency, watchdog arm)
  5. Bluetooth (`Device::init`, the slow stage)
  6. Companion (`Companion::init`)
- `BootScreen::finish()` fills the bar, shows "Ready", and holds only if the
  boot outran a 700 ms minimum so the splash never flashes past. The pad is
  skipped in the simulator. The UI constructor then runs LVGL and the main menu
  paints over the splash, so there is nothing to tear down.
- No mutex is held across the hold, and it runs on the boot task before the UI
  or watchdog-feed loop starts, so it blocks neither.

## Toggle and default

- New setting `Settings::BOOT_SPLASH` (bool, NVS key `boot_splash`, wire id 44),
  surfaced as a switch on Settings -> Features.
- DEFAULT: ON. The user asked for this feature, so it ships enabled. Turning it
  off restores the previous no-splash boot exactly: `begin/step/finish` all
  self-gate to no-ops. Flip the default in `Settings::init()` (the `BOOT_SPLASH`
  case) if a silent boot is preferred.
- Wired through every exhaustive settings surface: `appliesImmediately` (false,
  it only reads at boot) and `isDangerous` (false) in FurbleSettings; the type,
  print and set switches in FurbleConsole; `settingType`/`settingValue` in
  FurbleCompanionService; serialize/import in FurbleSD. Because it is wire
  exposed, the protocol goldens were regenerated.

## Verification

- Sim mirrors the boot path in `sim/main.cpp` runSimulator, so the same
  `begin/step/finish` sequence runs under SDL. `simQueryState("boot_splash")`
  reports `shown`, `partial` or `off`. Two e2e scenarios:
  `sim/scenarios/e2e/boot-splash.txt` asserts the splash rendered every stage
  and boot reached the main menu; `boot-splash-disabled.txt` seeds the toggle
  off and asserts the splash was suppressed and boot still reached the menu.
- Overflow sweep run at 135x240, 80x160 and 320x240; the splash is M5GFX text
  so it is outside LVGL's overflow query, layout was sized to fit each panel.
- Firmware `m5stick-s3-debug` builds clean, which is the `-Werror=switch`
  backstop for the new enum value.

## PENDING HARDWARE RETEST

The on-device appearance needs a bench pass on the M5StickS3: confirm the
wordmark, info block and progress bar are readable and unclipped, that the bar
advances during boot (Bluetooth is the visible stage), and that the transition
to the main menu is clean with no flash or leftover pixels. The 80x160 StickC
and a 320x240 core should also get a visual check if available.
