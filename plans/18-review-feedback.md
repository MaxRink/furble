# Plan 18 review feedback: GPS reader and standby settle fixes (#465)

Every finding on #465 is recorded here as fixed, already fixed, or a false
positive, with its evidence. The narrative is in `plans/18-gps-motion.md`,
sections "Fix: gps-motion-detector flake" and "Fix: gps-standby-wake settle
race".

| # | Source | Finding | Outcome | Evidence |
| --- | --- | --- | --- | --- |
| 1 | Pre-push subagent review | The first `gps.standby_settled` required the published state to be STANDBY. A pause in the second before a wake still opens the wake window; no burst comes, the cycle goes degraded and never returns to STANDBY, so the wait hangs for its whole bound. | Fixed before the first push. The query no longer requires STANDBY and is renamed `gps.duty_settled`. | A 20 offset pause sweep, four runs each, passed 80 of 80; 15 went degraded before the wait finished. |
| 2 | #465 review (Copilot), `src/FurbleGPS.cpp:1299` | "Not BURST" can still read yes early. `serviceCycle()` clears `m_CycleRequest` before `sendCommand()`, and if the same pass started the burst the published word still says STANDBY. | Fixed. A `FURBLE_SIM` scope marks the GPS task busy from before it reads the request until the command is written, and `GPS::update()` raises the request before it stores the pushed sequence. The query reads the fix sequence, pushed sequence, request and busy flag in that order and no cycle state. | With the post-pause wait cut to 1 ms, the old condition failed 21 of 128 and the query read `no` in exactly those 21. The new wait passed that probe 128 of 128, the sweep 80 of 80 and the real scenario 64 of 64. |
