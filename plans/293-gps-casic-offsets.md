# 293 - GPS CASIC offset corrections

## Motivation

Two CASIC decodes used field offsets that do not match the specification. The
GPS research lane found them while cross-checking the receiver code against
CASIC v3.6 and v4.2:

- `CFG-NAVX` `navSystem` was written at offset 6. Offset 6 is `minSVs`;
  `navSystem` is at offset 13. A receiver that accepted the masked write
  therefore ignored the constellation value, while `$PCAS04` was only sent as a
  timeout fallback.
- `MON-HW` read AGC from offset 4, antenna status from 8, and the first jamming
  byte from 9. The specification places `agcData0` at 12, `antStatus` at 20,
  and the jamming word array at 24.

The host test and the simulator fixture pinned the same wrong offsets, so the
suite certified the incorrect decode.

## Implementation state

- `GPS::NAVX_NAV_SYSTEM_OFFSET` is 13. The header documents the full
  `CFG-NAVX` offset map, and `FurbleGPS.cpp` pins both writable offsets with
  `static_assert`.
- `Casic::MonHw` reads `noisePerMs0` at 0, `agcData0` at 12, `antStatus` at 20,
  and `jamming[0]` at 24 as a 32-bit word.
- `gps monhw` prints the 32-bit jamming word.
- The host vector and the simulator fake receiver now use the specification
  offsets.

## Validation

- `gps_casic_test`: PASS (389 checks), including the corrected MON-HW vector.
- Host build passes.

## Remaining gate

The offsets are specification-derived. A receiver check is still owed:
set constellation to GPS-only, open the satellite page, and confirm only
`$GPGSV` talkers remain; poll `gps monhw` on the live unit and compare the
decoded fields with the raw bytes.
