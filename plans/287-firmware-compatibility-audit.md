# Camera firmware compatibility audit

## State

A private firmware corpus was built for Fujifilm X100VI 1.31, Fujifilm X-S20
3.30, Ricoh GR III 2.20, and Ricoh GR IV / GR IV HDF 1.20. It contains the
vendor images, decoded sections, extracted `bled` binaries, official Android app
analysis, and per-family dossiers. The corpus is not part of this repository.
The summarized evidence is in `docs/camera-research/fuji-canon-nikon.md`,
`docs/camera-research/ricoh.md`, and
`docs/camera-research/fujifilm-secure-gatt.md`.

One firmware-confirmed and one app-static Ricoh behavior fix are included:

1. Location enable now writes the Camera Service GEO Tag characteristic
   `A36AFDCF-6B67-4046-9BE7-28FB67DBC071`, not WLAN Network Type
   `9111CDD0-9F01-45C4-A2D4-E09E8FB0424D`. Firmware confirms the role and the
   0/1 value check.
2. The 32-byte GPS Information packet ends with WGS84 datum `0`, not a
   centisecond. Firmware never reads that byte; the official apps write `0`.

Fujifilm code changes include a firmware-confirmed focus fix. The shutter
characteristic parser accepts only the little-endian values `0` release, `1`
half press, and `2` full press. The old focus action sent `03 00`, which the
parser rejects with `-30`. Focus press now writes `01 00` and focus release
writes `00 00`. The still capture path remains `01 00` then `02 00`, and
release remains `01 00` then `00 00`.

Other Fujifilm changes are naming, dead-constant, and comment corrections. The
Secure UUID set already matches X100VI 1.31 and X-S20 3.30. Basic support
remains for older bodies. The Basic service `91F1DE68` is absent from both
target images, so no Basic path is present; runtime reachability is not proven
either way.

DJI Osmo now accepts both documented device-ID encodings of the Action 4 and Action 5 Pro
device IDs. DJI's own command table and a checksum-valid Action 4 frame imply
`0xFF330000`, while one demo source file writes `0x33FF0000`. Accepting both
avoids rejecting a camera that reports the table encoding. The real camera
value still needs a hardware capture.

Canon EOS Smart focus press and release now send `00 03` AF_ON and `00 04`
AF_OFF to the shutter characteristic. The official Camera Connect app exposes
these separately from the `00 01` and `00 02` AF plus release shutter pair.
The Canon GPS request, source select, write type, and model gate remain
documented gaps and are not changed without a capture.

LUMIX GPS altitude is now a signed int16, matching the LUMIX Sync builder.
The clock packet now uses the app's standard-offset and DST byte order. Furble
still writes zero for both because it has no timezone source. The control
Confirm and Start sequence, the APS identity path, the GPS stop packet, and the
readiness notifications remain documented gaps.

## Evidence

Firmware evidence is `firmware-static`: the Fujifilm images contain the same
70-entry UUID table, mapped to 13 services, 69 characteristics, and 21 CCCDs.
The Ricoh images contain all 17 Ricoh UUIDs used by Furble, and
`GPSInformation::Write` parses three byte-swapped float64 values, a
little-endian year, and a six-field timestamp. It never reads the final byte.
The official apps write WGS84 datum `0` there.

Supporting references added by this PR:

- `docs/diagrams/pairing-security-flows.md` covers every family's
  advertisement, SMP, registration, GATT prerequisites, errors, and recovery.
- `docs/diagrams/state-machines.md` covers connection, mode, capture, GPS,
  transfer, and recovery states.
- `docs/camera-research/bt-api-surface.md` summarises every service,
  characteristic, command, notification, and unused feature by family.
- `docs/camera-research/fuji-canon-nikon.md` records the Nikon Remote short
  press `02 02`, long press `02 03`, no-op `02 00`, and the six-byte `2084`
  key/status indication.

App evidence is `app-static`: XApp 2.7.6 and Camera Remote 4.10.2 use the
Fujifilm Secure flow, the 23-byte geotag packet, and the 10-second sync
interval. Image Sync 2.1.32 and GR WORLD 2.0.0 write GEO Tag `1` and GPS datum
`0`. The official apps do not use a fixed Ricoh passkey.

## Deviations and limits

- No hardware capture was performed for this audit. Physical capture, pairing
  prompts, ATT errors, per-characteristic security, and the Fujifilm NOT1
  time-request semantics remain uncertified.
- Ricoh GR III and GR IIIx share the UUID families and now benefit from the
  location and datum fixes, but their power, WLAN, pairing, and operation-mode
  behavior differs from GR IV. They are not promoted to certified support.
- The fixed Ricoh passkey fallback is retained to avoid changing the currently
  tested GR IV HDF path without hardware. It is documented as a compatibility
  and security risk.
- Fujifilm Basic is not removed. X100VI 1.30+ and X-S20 3.30 have no static
  Basic path, and the docs now say so without claiming runtime impossibility.

## Validation

- Full host build completed with Clang.
- All 120 host tests passed, including `ricoh-focus` with the new geotag
  regression coverage and the advertisement dispatch test that links the DJI
  driver.
- The Python tooling suite passed: 192 tests, `OK`.
- `clang-format --dry-run --Werror` is clean for the changed C++ files.
- The new test asserts the Camera Service GEO Tag write, the 32-byte GPS
  packet, and that the final byte is `0`.
- The DJI dual device-ID acceptance compiles in the host dispatch test. It has
  no dedicated behavior test because the helper is private, and no hardware
  capture proves the camera-reported value.
- Canon Smart focus compiles into the host dispatch test. No Canon camera peer
  exists, so the AF_ON and AF_OFF writes are app-static and untested on
  hardware.
- Fujifilm focus uses the firmware-accepted `01 00` half press and `00 00`
  release. The host camera and protocol tests assert the new write sequence and
  no longer accept `03 00`.
- The pairing and state-machine references are documentation only. They cite
  firmware, app, and capture evidence and do not change the camera stack.
- Hardware validation remains pending. The PR must declare Ricoh and DJI
  untested on hardware unless a bench run covers them.
