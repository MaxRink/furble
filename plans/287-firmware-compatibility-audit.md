# Camera firmware compatibility audit

## State

A private firmware corpus was built for Fujifilm X100VI 1.31, Fujifilm X-S20
3.30, Ricoh GR III 2.20, and Ricoh GR IV / GR IV HDF 1.20. It contains the
vendor images, decoded sections, extracted `bled` binaries, official Android app
analysis, and per-family dossiers. The corpus is not part of this repository.
The summarized evidence is in `docs/camera-research/fuji-canon-nikon.md`,
`docs/camera-research/ricoh.md`, and
`docs/camera-research/fujifilm-secure-gatt.md`.

Two verified Ricoh behavior fixes are included:

1. Location enable now writes the Camera Service GEO Tag characteristic
   `A36AFDCF-6B67-4046-9BE7-28FB67DBC071`, not WLAN Network Type
   `9111CDD0-9F01-45C4-A2D4-E09E8FB0424D`.
2. The 32-byte GPS Information packet ends with WGS84 datum `0`, not a
   centisecond.

Fujifilm changes are limited to removing unused or misnamed constants and
correcting comments. The Secure UUID set already matches X100VI 1.31 and
X-S20 3.30. Basic support remains for older bodies but cannot pair with these
two Secure firmware versions because `91F1DE68` is absent.

## Evidence

Firmware evidence is `firmware-static`: the Fujifilm images contain the same
70-entry UUID table, mapped to 13 services, 69 characteristics, and 21 CCCDs.
The Ricoh images contain all 17 Ricoh UUIDs used by Furble, and
`GPSInformation::Write` parses three byte-swapped float64 values, a
little-endian year, a six-field timestamp, and a final datum byte.

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
- Fujifilm Basic is not removed. X100VI 1.30+ and X-S20 3.30 are Secure-only,
  and the docs now say so.

## Validation

- Full host build completed with Clang.
- All 120 host tests passed, including `ricoh-focus` with the new geotag
  regression coverage.
- The new test asserts the Camera Service GEO Tag write, the 32-byte GPS
  packet, and the final datum byte.
- Hardware validation remains pending. The PR must declare Ricoh untested on
  hardware unless the M5StickS3 bench run covers it.
