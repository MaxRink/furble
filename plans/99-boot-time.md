# 99 - Boot timeline and safe boot-time reductions

## Goal

The reported M5StickS3 startup "took a while". Measure the real stages first,
then make only changes whose failure mode is understood. The 12.8 s UI settle
was the largest number in the report, but most of that span had no
instrumentation, so this plan does not guess at it.

## Baseline

Serial timestamps from the reporter, M5StickS3 debug, MQTT enabled, WiFi
associated to "The Internet of Shitty Things", ms since reset:

| Stage | ms |
| :--- | ---: |
| ROM/bootloader to app start | 1300 |
| PSRAM ready | 2000 |
| M5GFX board detect | 2300 |
| BLE controller init | 2900 |
| Console ready | 3200 to 3600 |
| WiFi associated | 4600 |
| DHCP/IP | 5700 |
| MQTT connected | 6800 |
| UI/LVGL settled | 12800 |

Those numbers are measured. Every "expected" number in this plan is inferred.

## Instrumentation

`include/FurbleBootTimeline.h` and `src/FurbleBootTimeline.cpp` keep a fixed
array of atomic timestamps. The first occurrence of each mark wins. There is no
heap allocation, no mutex, and no per-mark log line.

Marks cover: `app_main`, PSRAM readiness, Settings, crash log, netif, Platform,
display, PMIC, boot splash shown, IR, feedback, SD, CPU policy, BLE, WebUI TLS,
companion, MQTT init, control task, WiFi request, console ready, splash done,
UI constructor start, UI menu start and end, UI constructor end, first UI
frame, headless service ready, WiFi association, IP, NTP sync, and MQTT
connect.

`BootTimeline::begin()` also samples the scheduler-not-started cycle-counter
timestamp. That value includes the ROM bootloader and the PSRAM memory test,
which run before `esp_timer_get_time()` has an origin. The console command
reports it as `boot.reset_to_app_ms`.

The single summary line is emitted after the first UI frame or the first
headless service pass. `boot timing` prints the retained marks at any later
point.

## Changes

### Settings seed batching and version marker

`Settings::init` now opens each NVS namespace once instead of once per setting.
Missing defaults are written through a deferred-commit `Preferences` handle,
so each namespace is one `nvs_commit` instead of one commit per key. A
`cfg_def_ver` marker in `FURBLE_STR` is written last. A matching marker skips
the key scan on later boots. A missing or old marker performs the full scan. A
failed commit leaves the marker unwritten, so the next boot rescans and retries
the seed.

`SettingsBootstrap::DEFAULTS_VERSION` must be bumped whenever a setting is
added or a default changes. The host `settings_boot_test` pins three commits
for a fresh store, zero writes with the current marker, and a marker-only
upgrade when every key already exists.

### WiFi direct-join fallback

The station already remembered BSSID and channel. It now also remembers the
auth mode, sets `WIFI_FAST_SCAN` explicitly on the direct-join path, and applies
the remembered auth mode as the driver threshold. When a direct join fails
before obtaining an IP, the remembered BSSID is cleared and one scan runs
before the next attempt. A scan that finds the SSID saves the new BSSID and
auth mode. A disconnect after a live session keeps the remembered AP for a
fast reconnect.

This is a reliability change first. It does not speed up a normal boot that
already has a valid remembered AP.

### WiFi starts before companion and MQTT

WiFi init moved from after the control task to after BLE and the WebUI TLS
identity, before companion and MQTT. Association and DHCP overlap their init.
It does not start before BLE or the TLS identity, because the identity
generator needs the unfragmented internal heap and the BLE controller init
should not race the first scan. Ethernet builds keep WiFi after
`Ethernet::init()` so a live wire still defers the station.

### PSRAM memory test

The three S3 release sdkconfigs disable `CONFIG_SPIRAM_MEMTEST`. The S3 debug
environments merge `sdkconfig.spiram-memtest` back in, so every diagnostic
profile still aborts loudly on a bad part or wrong mode. The debug sdkconfig
patch now accepts comma-separated fragments for this layering.

The removed test runs before the firmware timeline starts. The reported 700 ms
gap between app start and PSRAM ready is the basis for the expected saving, not
a measured post-change result.

### Boot splash minimum

`BootScreen::finish()` now measures the minimum visible time from `app_main`,
not from `begin()`. A slow Platform init can no longer be extended by another
700 ms. The measured 6.8 s boot already exceeded the minimum, so the expected
saving on that boot is zero. A build that reaches `finish()` less than 700 ms
after `app_main` still holds the splash.

## Rejected

- Deferring `WebUI::initEarly()` until after WiFi. The TLS identity generation
  needs the unfragmented internal heap. The old ordering is preserved.
- Starting WiFi before BLE or before the WebUI identity. The radio and BLE
  coexist, but the identity heap contract is explicit and a first-scan race is
  not worth the overlap.
- Building the menu lazily or moving LVGL page construction off the UI task.
  That is the likely source of the remaining 6 s gap, but LVGL is not
  thread-safe and a lazy menu changes navigation and memory ownership across
  every page. Measure `ui_menu_start` to `ui_menu_done` first.
- Disabling the SD probe, GPS probe, IR init, or crash log. They either run on
  their own task or cost too little to justify a behavior change.
- Tuning NTP or MQTT. Both are already asynchronous and do not block the UI.

## Verification

- `boot_timeline_test` covers first-mark-wins, unset marks, the cycle-counter
  value, summary formatting, and truncation.
- `boot_policy_test` covers the settings marker decision, channel validation,
  auth-mode acceptance, and the direct-join scan fallback.
- `settings_boot_test` covers the commit and open counts, the current-marker
  fast path, upgrade preservation, and a failed commit that leaves the marker
  unset.
- `console_commands_test` registers `boot timing` and checks its output.
- Host suites and firmware builds are run from the PR.

The full host build needed one pre-existing link gap repaired:
`sim_scheduler_test` now links `sim/heap_model.cpp` and `sim/CrashLogSim.cpp`,
which `sim/freertos.cpp` already calls after the issue #395 heap model landed.
That is a build-only fix and does not change simulator behavior.

No simulator scenario is added. The simulator compiles the timeline source and
prints the boot summary, but its clock is virtual, so a timing assertion would
not be a hardware measurement. The scenario manifest is also owned by the
concurrent simulator memory work in `sim-mem-crash`. The only simulator change
is the source list entry needed to link the new file.

## Open hardware gates

- Measure release PSRAM boot with and without `sdkconfig.spiram-memtest`.
- Capture `ui_ctor_start` to `ui_first_frame` on the StickS3. The 6 s gap after
  MQTT is not explained yet.
- Capture `wifi_started`, `wifi_assoc`, and `wifi_ip` on Core2, the headless S3
  profile, and the Waveshare board.
- Confirm that a settings upgrade from a build without the marker performs one
  scan and that later boots skip it.

## Documentation

- `docs/boot-timing.md` carries the command, the measured baseline, the
  expected per-board numbers, and the tradeoffs.
- `docs/console-commands.md`, the README console block, and the in-repo wiki
  command reference list `boot timing`.
- `CLAUDE.md`, `CONTRIBUTING.md`, and `AGENTS.md` describe the S3 PSRAM
  memory-test layering.
