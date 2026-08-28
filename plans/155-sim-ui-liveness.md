# 155 - Sim UI liveness invariant and false-connected coverage

Plan numbers 152 and 153 are claimed by open PRs #247 and #246, and 154 may be
claimed by a parallel flappy-peer PR in flight at the same time as this one.
This plan takes 155 to stay clear of that race.

## Motivation

During the 2026-08-28 hardware bench session the furble Connected screen
stayed up while NEITHER camera had a live link. The sim could not have caught
this false-connected state: the fake control (`sim/FurbleControlSim.cpp`)
derives the UI-facing state and the per-camera link truth from the same
trivial code path, so they can never disagree, and no scenario asserted the
converse invariant that a UI presenting Connected implies the links are
actually up. Every existing guard (`false-connected-guard.txt`,
`reconnect-indicator.txt`) asserts the forward direction only, at scripted
checkpoints only.

## Change

Short-term, sim-side remediation. All changes live under `sim/`,
`sim/scenarios/e2e/`, and docs; no firmware source changes, so release
binaries are untouched.

### Continuous liveness invariant (sim/driver.cpp)

Every driver tick (including while a `wait` step holds the scenario), the
driver checks: does the UI present the Connected screen (the exact
`ui.connected` three-way check from `UI::simQueryState`, src/FurbleUI.cpp)
while fewer camera links are actually up (`Camera::isConnected` per target)
than the session has targets? A divergence outliving a grace period (default
3000 sim-ms, `seed liveness_grace_ms N` overrides) fails the run with
`LIVENESS INVARIANT FAILED`. The invariant is on by default in every scripted
scenario. `seed liveness_check false` opts a scenario out of the failure only:
detection keeps running and increments the `ui.liveness_violations` counter
query, so an opted-out scenario can still assert the invariant would have
fired.

### link_lies seed and link-lies-kill action

`seed link_lies true` arms `action link-lies-kill`, which disconnects every
connected fake camera link WITHOUT informing the fake control state machine:
`isConnected()` turns false, `control.connected` drops to 0, and the control
state stays `active` with no reconnect scheduled. This deliberately constructs
the divergence the fake control cannot otherwise express, because
`simDropActiveLink` always advances the state machine. The action is gated on
the seed so no scenario builds the divergence by accident.

### Scenarios (sim/scenarios/e2e/, the CI-globbed directory)

- `multi-connect-false-connected.txt`: connect two cameras, drop BOTH links at
  once with auto-reconnect on, and assert the UI leaves the Connected
  presentation within a bound (`ui.connected no`), shows the reconnect
  indication (`ui.reconnecting yes`, `ui.reconnect_count 2/2`), and recovers.
  The invariant is enforced throughout with `ui.liveness_violations` asserted
  to stay 0.
- `link-lies-invariant.txt`: connect, kill the link truth via
  `link-lies-kill`, and prove the divergence (control `active` and
  `ui.connected yes` with `control.connected 0`). The scenario opts out of
  enforcement with `seed liveness_check false` so CI stays green, shortens the
  grace to 500 ms, sits in the divergence, and asserts
  `assert_min ui.liveness_violations 1`: the invariant fired and would have
  failed an enforced run.

## Verification

- Sim built headless; full e2e suite (72 master scenarios plus the two new
  ones) green.
- Host suite (`tests/host`) unchanged and green.
- Mutation check of the invariant's teeth: with `checkLivenessInvariant()`
  disabled in `sim/driver.cpp`, `link-lies-invariant.txt` fails its
  `assert_min ui.liveness_violations 1`; re-enabled, it passes. No mutation
  left in the tree.

## Long-term remediation (future work)

This plan hardens the fake-control sim. The class of bug from the 2026-08-28
incident (production `Control`/NimBLE believing a dead link is alive) can only
be reproduced by simulating over the REAL `Control` state machine:

- Compile production `FurbleControl.cpp` into the sim against a MockNimBLE
  layer (the `tests/host` fake NimBLE is the starting point).
- Provide virtual BLE peers whose links can be wedged, timed out, or silently
  dropped at the transport layer, so `isConnected()` itself can lie exactly as
  the hardware did.
- Run the same e2e scenario suite and this liveness invariant over that stack.

See the sim gap analysis from the 2026-08-28 incident review: the fake control
cannot express UI/link divergence by construction, which is why the `link_lies`
seed exists as an interim, hand-built divergence. The real-Control sim is
tracked separately and is not attempted here.
