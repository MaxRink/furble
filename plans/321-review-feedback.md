# 321 review feedback

## Copilot review of PR 458

| # | Source | Finding | Resolution | Evidence |
| --- | --- | --- | --- | --- |
| C1 | Copilot, `lib/furble/protocol/GpsUbx.cpp` | `encodeNavSat` only enforced the one-byte satellite count, so 85 or more satellites produced a payload above `MAX_PAYLOAD` (1,028 bytes at 85) that `scan()` rejects. | Fixed. The receiver encoders (`encodeNavPvt`, `encodeNavSat`, `encodeMonVer`) frame through a file-local `frameReceived()` that returns an empty frame above `MAX_PAYLOAD`, so none of them can emit a frame the codec's own stream drops. `encodeMonVer` had the same gap above 32 extensions. `frame()` keeps its 65,535-byte transmit bound: `MAX_PAYLOAD` is a receive limit, and the scan boundary test frames `MAX_PAYLOAD + 1` on purpose. | `gps-ubx`: 84 satellites (1,016 bytes) encode to a frame `scan()` accepts, 85 are refused, and a 33-extension MON-VER is refused. |
| C2 | Copilot, `plans/321-ublox-m10-driver.md` | The Sources table named the u-blox documents without URLs, against `plans/95-engineering-lessons.md` (every protocol PR cites its source with a real URL). | Fixed. Each row carries its official `content.u-blox.com` URL. Each URL was fetched on 2026-11-14: all seven return the PDF, byte-identical (SHA-256) to the copy the offsets were read from, and the document numbers and revisions match the table. | `plans/321-ublox-m10-driver.md` Sources |
