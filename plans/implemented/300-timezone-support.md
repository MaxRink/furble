# 300: local time zone support

## State

Merged as #364 (`2145cd9a`) from `feat/timezone-support`, based on `origin/master` at
`dd31f47e`.

- `Settings::TIMEZONE` uses the reserved wire id 42 and the NVS key `timezone`.
  The default is UTC, so an existing device and every camera sees the same clock
  it saw before the setting existed.
- `FurbleTimezone` holds a curated table of 39 POSIX TZ strings and 61
  representative points. It applies one with `setenv("TZ", ...)`, `tzset()`, and
  `localtime_r()`. No tzdata files are shipped.
- `Settings::TIMEZONE` can be `UTC`, one of the 38 manual zones, or
  `Auto (GPS)`. The default is UTC, so an existing device is never silently
  moved to a derived zone.
- `FurbleTimezoneGPS` reads only the public GPS snapshot. It accepts a fresh
  fix when the receiver is enabled, the fix is LIVE, the source is the wired
  UART, and location, date, and time are within `GPS::FRESH_FIX_MS`.
- `TimeKeeper` remains the UTC source of truth. `Control::updateGPS` adds a
  local civil view and the zone offsets to `Camera::timesync_t` before dispatch.
- The console `time status` prints UTC, local time with its offset, the
  effective zone, `timezone_source`, `timezone_derived`, the tzdata review
  version, and the current DST state.
- A new `Settings > Time` page holds the zone roller, live Local and UTC rows,
  and a heading of the form `Time zone: <zone> (<source>)`. The page is read
  through the existing diagnostics timer.
- Vendor writes use the local view only where the protocol expects civil time.
  UTC fields stay in the same struct for protocols that carry absolute time.

## Selection and fallback

The selection order is: manual choice, then GPS-derived zone, then UTC. A
manual selection is never overridden by GPS. Auto uses a derived zone only
while the last fresh wired fix is at most 30 minutes old. A candidate change is
held for at least 5 minutes after the previous change, so a noisy border fix
cannot churn the camera clock. A coordinate farther than 30 degrees from every
representative point falls back to UTC.

The GPS selection only changes the local representation and vendor timezone
fields. TimeKeeper UTC remains the authority for timestamps and camera packets
that carry absolute time.

## Design

The table is one row per supported zone. The first ten rows are the primary
destinations; the rest extend coverage to major population regions:

| Zone | POSIX TZ | Standard offset | DST shift |
| :--- | :--- | :--- | :--- |
| UTC | `UTC0` | 0 | 0 |
| Europe/Berlin | `CET-1CEST,M3.5.0,M10.5.0/3` | +60 | 60 |
| Europe/London | `GMT0BST,M3.5.0/1,M10.5.0` | 0 | 60 |
| US Eastern | `EST5EDT,M3.2.0,M11.1.0` | -300 | 60 |
| US Central | `CST6CDT,M3.2.0,M11.1.0` | -360 | 60 |
| US Mountain | `MST7MDT,M3.2.0,M11.1.0` | -420 | 60 |
| US Pacific | `PST8PDT,M3.2.0,M11.1.0` | -480 | 60 |
| Australia/Sydney | `AEST-10AEDT,M10.1.0,M4.1.0/3` | +600 | 60 |
| Asia/Tokyo | `JST-9` | +540 | 0 |
| India | `IST-5:30` | +330 | 0 |

Additional coverage: Europe/Athens, Europe/Moscow, Europe/Lisbon,
America/Halifax, America/St_Johns, America/Sao_Paulo, America/Santiago,
America/Argentina/Buenos_Aires, America/Mexico_City, America/Phoenix,
America/Anchorage, Pacific/Honolulu, Africa/Cairo, Africa/Johannesburg,
Africa/Lagos, Africa/Nairobi, Africa/Casablanca, Asia/Dubai, Asia/Karachi,
Asia/Bangkok, Asia/Shanghai, Asia/Seoul, Asia/Kathmandu, Asia/Jerusalem,
Asia/Tehran, Asia/Jakarta, Australia/Perth, Australia/Brisbane, and
Pacific/Auckland.

The POSIX strings came from IANA tzdata through
`nayarsystems/posix_tz_db`. The table records the review version (`2026d`).
`tools/timezone_table.py --check` validates unique names, offsets, and POSIX
strings. `--zone-tab <path>` prints refresh coordinates from a tzdata `zone.tab`.
To update a zone, replace its POSIX string and standard offset, bump the review
version, extend the host test vectors at both 2026 DST transitions, and run the
script.

## Vendor matrix

| Vendor | Field path | Value sent | State |
| :--- | :--- | :--- | :--- |
| Fujifilm | `C52EDBCE` 12-byte UTC packet | UTC fields, standard offset, DST flag | host `fujifilm-registration-gate` |
| Fujifilm | `0F36EC14` geotag | local civil time | host `fujifilm-camera` |
| LUMIX | clock packet | local civil time, standard offset, DST flag | host `lumix-session` |
| Sony | 95-byte location form | UTC fields, current offset, DST difference | host `sony-location` |
| Ricoh | Date Time | local civil time, no zone field | host `ricoh-focus` plus manual clock check |
| Canon EOS Smart | GPS timestamp | UTC epoch, unchanged | host `canon-time` |
| Nikon Smart | 41-byte geotag | local civil time | code review, path unreachable |
| DJI Osmo, Canon Remote, Nikon Remote | none | no time field | unchanged |

The Fujifilm 7-byte local characteristic is still not written because its layout
is not verified. The Nikon Smart 10-byte `timesync_msg_t` with timezone fields
is still unused because the pairing path cannot complete without Bluetooth
Classic.

## Tests

- `timezone`: table, name and label lookup, invalid input, both 2026 DST
  transitions for Berlin, London, New York, and Sydney, no-DST Tokyo and
  Kolkata, unique names, every POSIX string applied, nearest-zone Berlin,
  London, New York, and Tokyo, outside-radius fallback, manual override, the
  five-minute change hold, and the 30-minute retention expiry.
- `settings-table`: ledger and wire id contract.
- `settings-nvs-roundtrip`: NVS type, default, and round-trip for `TIMEZONE`.
- `console-commands`: `time status` local, UTC, auto, and source rows,
  `settings get/set timezone`, immediate apply, and invalid input.
- `fujifilm-camera`, `fujifilm-registration-gate`, `lumix-session`,
  `sony-location`: packet fields at the peer.
- `tests/protocol`: regenerated golden corpus includes wire id 42.
- `tools/timezone_table.py --check`: table refresh sanity outside the C++ test.
- `sim/scenarios/e2e/timezone-auto-gps.txt` and
  `timezone-manual-override.txt`: auto derivation from a Munich fix and manual
  override under a live fix.

## Deviations and limits

- DST rules and coordinates are curated, not a full tzdata or polygon database.
  A government rule or border change needs a table edit and a new review
  version. The table is 39 zones and 61 points, not every IANA zone.
- Auto is a nearest-zone approximation. Large countries, borders, islands, and
  remote regions can resolve to a neighbouring rule. Beyond 30 degrees from a
  point, Auto falls back to UTC. Morocco's Ramadan exception is not modelled by
  its POSIX string.
- The default is UTC, not the build host or a guessed region. This preserves
  existing camera time behavior and avoids silently changing a camera clock.
- Fujifilm and LUMIX send the standard offset next to a DST flag. Sony sends the
  current offset and the DST difference. The camera-side interpretation is
  capture-gated; only packet shape is covered on the host.
- Nikon Smart is unreachable because pairing needs Bluetooth Classic. Its local
  time change is code review only.
- The GPS Data page keeps its UTC timestamp. Local and UTC together are on the
  new Time page, so the fix page keeps its existing row budget.
- The Time page is refreshed by the diagnostics timer on the UI task. The
  console remains the machine-readable surface.
- Auto derivation runs on the UI housekeeping timer and the headless service
  loop, not inside `FurbleGPS.cpp`. A camera can see the previous zone for at
  most one GPS tick after the first fresh fix.

## Review follow-ups (2026-11-05)

The post-merge review of PR #364 found six gaps, all closed here:

- `lib/furble/protocol/UtcTime.h` is now declared in
  `tools/portable_core_manifest.txt`, so the portability inventory stays
  complete.
- The duplicate `FurbleTimezone.cpp` entries are gone from `control_e2e_test`
  (it links `furble_host_camera`, which already owns the translation unit) and
  from `console_commands_test` (it listed the file twice).
- The Android metadata now carries all 39 curated zones plus Auto (GPS) at 39.
  `isSettingValueValid` therefore accepts the full firmware selector instead of
  rejecting values 10 through 39.
- Companion writes and provisioning reject `TIMEZONE` values above Auto instead
  of persisting a raw value that would silently clamp to UTC.
- SD import accepts Auto (39) and still rejects 40 and above, so an exported
  backup round-trips.
- A fresh fix outside the 30-degree snap radius now clears the derived zone and
  applies UTC immediately instead of retaining the old zone for 30 minutes.

The wiki walkthrough image is also present under `docs/wiki/img`, so the
published page no longer links a missing file. Host coverage adds the
out-of-radius fallback, provisioning range, companion range, SD Auto round-trip
and Android option-list assertions.

## Documentation follow-up (2026-11-05)

The Auto fallback documentation was synchronized across every published
surface after the #366 review: the README, settings reference, generated UI
walkthrough, wiki walkthrough, wiki console reference and console reference all
now say that Auto falls back to UTC after 30 minutes without a fresh fix **and**
immediately when a fresh fix is outside the 30-degree nearest-zone radius. This
is a documentation-only follow-up; no wire or behavior change.

## Simulator documentation follow-up

`seed timezone` was accepted by the scenario parser but missing from the
effective-seed list in `docs/sim.md`; the source-text doc-token gate caught it.
The seed is now documented and the gate runs as a sim-e2e CI step, so the next
undocumented simulator token fails before the build. No simulator behavior or
wire value changed.
