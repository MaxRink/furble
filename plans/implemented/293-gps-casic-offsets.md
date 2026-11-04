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

- `GpsNavx::OFFSET_NAV_SYSTEM` is 13. `GpsNavx.h` documents the full
  `CFG-NAVX` offset map and pins every named offset plus the 44-byte payload
  with `static_assert`; `GPS::NAVX_*_OFFSET` aliases those constants so the
  production write path and the header cannot drift.
- `Casic::MonHw` reads `noisePerMs0` at 0, `agcData0` at 12, `antStatus` at 20,
  and `jamming[0]` at 24 as a 32-bit word.
- `gps monhw` prints the 32-bit jamming word and reports a response shorter
  than 56 bytes as unavailable instead of printing zeroed fields.
- The host vector and the simulator fake receiver now use the specification
  offsets.

## Validation

- `gps_casic_test`: PASS (397 checks), including the corrected MON-HW vector and the pinned CFG-NAVX offsets.
- Full host CTest: 140/140 after the rebase onto the static-analysis gate.
- Static analysis: PASS. The NMEA splitter now uses iterators, which also
  clears the cppcheck bounds false positive that fired when the file became a
  changed file; the GPS time loop and two string appends were tightened.
- Simulator: `gps-monhw-poll`, `gps-autobaud-ladder`, `gps-degraded-recovery`,
  and `gps-ephemeris-replay` pass on the M5StickS3 build.

## Documentation publication

The console reference exists in both `docs/console-commands.md` and the
published `docs/wiki/Console-Commands.md`; both were updated. The GPS claims in
`README.md`, `docs/power/gps-imu.md`, and `docs/power/opportunity-matrix.md`
are engineering references under `docs/power/` and are not mirrored to the
wiki.

## Hardware check

On 2026-11-04, master `a0488620` plus the `gps monhw` wait fix ran on the
M5StickS3 with the AT6668 at 115200. The poll returned a valid 56-byte MON-HW
payload and decoded `noise=0 agc=18 antenna=0 jam=0`; the raw payload places
`agcData0` at 12 (`12 00`), `antStatus` at 20 (`00`), and the jamming word at
24 (zero). The MON-HW field offsets are live-confirmed, but `antenna=0` is the
init value, so a qualified antenna and a disconnected-antenna comparison are
still owed to interpret the status bits.

The constellation check switched talkers `$GN -> $GP` with `gps send PCAS04,1`
and back to `$GN` with `PCAS04,7`. However, `gps config` reports the binary
`CFG-NAVX` (0x06 0x07) write as `NACKED` on this unit, so the working path is
the `$PCAS04` fallback. The binary offset write is host-tested but not
live-certified; a receiver or firmware that accepts `CFG-NAVX` is still needed
to confirm the structured write. The fallback is the supported path on this
AT6668.

## Remaining gate

Compare MON-HW antenna and jam bits with a known-good and a disconnected
antenna. A live unit that ACKs `CFG-NAVX` is still needed to certify the binary
offset write; until then `$PCAS04` is the verified constellation path.
