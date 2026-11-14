# 321 review feedback

## Copilot review of PR 458

| # | Source | Finding | Resolution | Evidence |
| --- | --- | --- | --- | --- |
| C1 | Copilot, `lib/furble/protocol/GpsUbx.cpp` | `encodeNavSat` only enforced the one-byte satellite count, so 85 or more satellites produced a payload above `MAX_PAYLOAD` (1,028 bytes at 85) that `scan()` rejects. | Fixed. The receiver encoders (`encodeNavPvt`, `encodeNavSat`, `encodeMonVer`) frame through a file-local `frameReceived()` that returns an empty frame above `MAX_PAYLOAD`, so none of them can emit a frame the codec's own stream drops. `encodeMonVer` had the same gap above 32 extensions. `frame()` keeps its 65,535-byte transmit bound: `MAX_PAYLOAD` is a receive limit, and the scan boundary test frames `MAX_PAYLOAD + 1` on purpose. | `gps-ubx`: 84 satellites (1,016 bytes) encode to a frame `scan()` accepts, 85 are refused, and a 33-extension MON-VER is refused. |
| C2 | Copilot, `plans/321-ublox-m10-driver.md` | The Sources table named the u-blox documents without URLs, against `plans/95-engineering-lessons.md` (every protocol PR cites its source with a real URL). | Fixed. Each row carries its official `content.u-blox.com` URL. Each URL was fetched on 2026-11-14: all seven return the PDF, byte-identical (SHA-256) to the copy the offsets were read from, and the document numbers and revisions match the table. | `plans/321-ublox-m10-driver.md` Sources |

## CI on PR 458

| # | Source | Finding | Resolution | Evidence |
| --- | --- | --- | --- | --- |
| CI1 | Host sanitizers, Coverage floor, PlatformIO host harness | `gps_ubx_fuzz.cpp:90` failed to compile on Linux GCC and Clang. `std::mt19937::result_type` is `unsigned long` there, so the OR with `rng() & 0x0fffffff` widened the key and narrowed it back to `uint32_t` inside a brace initializer. macOS hid it because its `uint_fast32_t` is 32 bits. | Fixed. The masked draw is cast to `uint32_t` before the OR, so the key is `uint32_t` on every host. | `tests/host/fuzz/gps_ubx_fuzz.cpp` |
| CI2 | Simulator end-to-end, `sim-e2e` "Run the fuzz gate" | `FUZZ GATE FAIL`: `[host-gps_ubx] fuzz binary missing`. The gate runs `gps_ubx_fuzz`, but the workflow builds an explicit target list that did not name it. | Fixed. The build step names `gps_ubx_fuzz`, and so do the matching commands in `docs/sim.md` and `tests/fuzz/README.md`, which also lacked the other gate targets. A new unit test fails when any binary `tools/check_fuzz.py` runs is missing from the workflow list; it fails on the old workflow. | `.github/workflows/sim-e2e.yml`, `tests/test_check_fuzz.py` |
