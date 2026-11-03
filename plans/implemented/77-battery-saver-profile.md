# 77 - Battery Saver profile

## Motivation

The power audit in `plans/98-power-optimization-audit.md` found that every
battery mechanism furble ships is already merged and hardware verified, and
every one of them defaults off. The whole battery story is seven runtime
settings spread across four submenus, all off out of the box. A user who never
opens those menus gets none of the savings.

The audit put hard numbers on the cost of the defaults, modelled on the
250 mAh StickS3 cell with the plan 63 profiler:

- `SLEEP_CONN` off is the single largest addressable number in the audit.
  Connected with the screen off draws 43.6 mA with it off versus 3.6 mA with
  it on, a factor of twelve. That is about 6 hours versus 69 hours on the
  StickS3 cell.
- `DISPLAY_OFF` defaults to Dim with a Never inactivity timeout. Dim keeps the
  APB lock so the MCU never light sleeps (50.6 mA, of which only 10.4 is the
  display). A true screen-off after a short idle is worth roughly 40 mA
  whenever the user forgets the device.
- `CONN_SAVER` off leaves the link at a 37.5 to 62.5 ms interval forever. On is
  a 250 to 300 ms idle interval, roughly a 6x drop in connection-event rate.
- `RECON_BACKOFF` off means a flat 5 s retry burns scan-at-full-duty forever
  when a camera stays off.
- `SCAN_MODE` defaults to Full (100 percent duty). Balanced (25 percent) costs
  little discovery latency.

The audit's top action is a one-switch Battery Saver profile that flips this
safe bundle together (audit "Top 10 actions", item 1, and the "missing plan 77"
note in the gap analysis). All mechanisms already exist and are verified, so
the effort is small and the risk is low. This plan implements that switch.

## Non-goals

- No deep sleep, no interval deep sleep (plan 19), no auto-off (plan 13). Those
  are separate follow-ups.
- No change to the GPS `PERMANENT_LOCK` behaviour. Separate follow-up.
- No new build environment. The audit weighed a `-lowpower` build env against a
  runtime toggle and noted the runtime toggle "serves users better and needs no
  extra CI envs". This plan ships the runtime toggle. The `-lowpower` env
  remains a possible future addition for the web flasher (tracked in
  `plans/97-followups.md` group 3).

## What the profile sets

When Battery Saver is on, the effective value of each setting below is forced.
Encodings are verified against the UI tables in `include/FurbleUI.h` and the
`Scan::Mode` enum in `lib/furble/Scan.h`.

| Setting | Forced value | Meaning |
|---|---|---|
| `SLEEP_CONN` | true (StickS3 only) | light sleep while connected |
| `CONN_SAVER` | true | idle connection parameters |
| `RECON_BACKOFF` | true | exponential reconnect backoff |
| `SCAN_MODE` | 1 | `Scan::Mode::BALANCED`, 25 percent duty |
| `INACTIVITY` | 2 | screen off after 60 seconds |
| `DISPLAY_OFF` | 1 | panel off (not Dim, not "Off, remote on") |

`DISPLAY_OFF` is deliberately the plain Off mode, not "Off, remote on". Plain
Off releases the APB lock and lets the MCU light sleep, which is the whole
point. On the StickS3 it also drops the green power LED (plan 71).

## Opt-in design and chosen semantics

The setting is `BATTERY_SAVER`, a bool, default false, NVS key `batt_saver`,
wire id 0. Default false means today's behaviour is unchanged. Wire id 0 keeps
it off the companion protocol wire, so no protocol golden changes; it is a
local UI setting only.

Two implementations were considered, per the task framing:

1. Write-through: on enable, snapshot the individual settings and overwrite
   them with the bundle values; on disable, restore the snapshot.
2. Override: the profile never touches the stored individual settings. Reads
   that drive power behaviour consult the profile and return the bundle value
   when it is on, otherwise the stored value.

This plan uses the override approach. It is the cleaner non-clobbering design:
the stored individual settings are never modified, so the Power and Bluetooth
submenus keep showing the user's own choices, and turning the profile off
restores those choices instantly with zero bookkeeping and no snapshot storage.
The write-through approach can lose a user's mid-profile edit on restore; the
override approach cannot, because there is nothing to restore.

The override arithmetic is a pure header, `include/FurbleBatterySaver.h`, with
no NVS or BLE dependency, mirroring `include/FurbleReconnectBackoff.h`. It is
unit tested directly on the host. `Settings` exposes NVS-backed effective
accessors built on it:

- `Settings::sleepConnEffective()`
- `Settings::connSaverEffective()`
- `Settings::reconBackoffEffective()`
- `Settings::scanModeEffective()`
- `Settings::inactivityEffective()`
- `Settings::displayOffEffective()`

Every read site that drives power behaviour was routed through these:

- `src/FurbleUI.cpp` display-off mode and inactivity timeout at UI init.
- `src/FurbleUI.cpp` scan mode when a scan starts.
- `src/FurbleControl.cpp` connection saver, reconnect backoff, and the
  sleep-while-connected lock hold.

The settings-editor reads that show the user's own stored choice (the Screen
off and Inactivity rollers, the Scan mode roller) were left on the raw stored
value on purpose, so the editors keep displaying what the user set.

`appliesImmediately` is false for `BATTERY_SAVER`. The Control-side reads pick
the profile up on the next connect or hold update without a reboot, but the
display bundle is read at UI init, so a reboot guarantees the whole bundle. The
on-device hint says the profile applies after a reboot. `isDangerous` is true,
matching the `SLEEP_CONN` it bundles, so an over-the-air write would be gated
the same way (it cannot be written over the wire anyway at wire id 0).

## Per-board applicability

- `SLEEP_CONN` is forced only on the StickS3. It is the only board with a
  Bluetooth controller configured for modem sleep, and the only board with the
  GPS burst power lock. Forcing light sleep while connected on the other boards
  would drop the UART clock during GPS NMEA bursts and lose sentences (audit,
  plan 15 gap). The gate lives in `Settings::sleepConnEffective()` behind
  `FURBLE_M5STICKS3`, and on the other boards the effective value falls back to
  the stored `SLEEP_CONN`, which their UI keeps off. This matches the existing
  UI, which only offers the `SLEEP_CONN` switch on the StickS3.
- The rest of the bundle (`CONN_SAVER`, `RECON_BACKOFF`, `SCAN_MODE`,
  `INACTIVITY`, `DISPLAY_OFF`) is safe and useful on all boards, so the switch
  is shown on every board.
- M5Stack Core (IP5306) cannot light sleep and hard-powers-off below 45 mA, so
  its only honest policy is screen-on or screen-off; the profile's screen-off
  and connection-saver parts still help, and nothing in the bundle is harmful
  there.

## On-device hint

A static, non-focusable wrap label under the switch on the Power page, matching
the one-button-mode hint precedent in `addFeaturesMenu`. It reads:

  Battery Saver: one switch for connection saver, 60s screen off, reconnect
  backoff, balanced scan, and light sleep while connected on StickS3. Applies
  after a reboot and keeps your own settings.

## Verification

Host and build verification done in this PR:

- New host unit suite `tests/host/battery_saver_test.cpp` asserts: profile off
  reproduces the stored value for every setting; profile on forces the bundle;
  the `SLEEP_CONN` board gate holds (forced only when supported); turning the
  profile off returns every setting to its stored value. Registered as the
  `battery-saver` ctest.
- The existing `settings-table` host test continues to pass, confirming the new
  NVS key is within the fifteen character limit and the wire id rule holds.
- All five release envs plus `m5stick-s3-debug` build clean. The debug env is
  the one that surfaces `-Werror=switch`, so it proves every exhaustive
  `Settings::type_t` switch got the new case (settings, console, companion, SD).

On-device drain measurement still owed (bench, StickS3, follow-up):

- With a camera connected and the screen off, toggle Battery Saver on and off
  and read `power stats` / `power log`. Expect the connected-plus-screen-off
  draw to drop from about 43.6 mA toward the 3.6 mA light-sleep floor when the
  profile is on, reproducing the audit's headline number end to end.
- Confirm the screen blanks 60 s after the last input with the profile on, and
  that turning the profile off restores the user's own screen-off and scan
  settings with no reboot needed for those stored values to reappear in the
  editors.
- Confirm on a non-StickS3 board with GPS attached that enabling the profile
  does not drop NMEA sentences (the `SLEEP_CONN` gate).

## Implementation state

Rebased onto current fork master (the `DISPLAY_MODE` console setting and the
plan 13 auto-off / low-battery rows landed after this branch was first cut).
The reconciliation kept both new `Settings::type_t` members side by side:
`DISPLAY_MODE` (wire id 36, guarded by `!FURBLE_NO_DISPLAY`) then
`BATTERY_SAVER` (wire id 0), in the enum, the `storage_type` specializations,
and the settings table. Wire ids do not collide. The exhaustive
`appliesImmediately` and `isDangerous` switches carry both cases. All five
release envs plus `m5stick-s3-debug` build clean after the rebase, and the full
host ctest suite passes including `battery-saver` and `settings-table`.

The maintainer decision on whether `SLEEP_CONN` should eventually default on,
versus staying opt-in and reachable through this profile, is written up
separately in `plans/111-power-recommendations.md`. This PR does not change any
setting default.
