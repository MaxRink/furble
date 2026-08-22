# Plan 113: connection-state sim coverage

## Motivation

A bench session found four connection-state bugs that reached hardware even
though the sim runs the real Control state machine and every UI page. The sim
just did not assert them:

1. the reconnect indicator was missing on the full-screen Remote shutter page
   (fixed in PR #154);
2. the status-bar Bluetooth icon does not go red on a disconnect;
3. the battery indicator shifts position across connection and GPS states;
4. a crash when the camera is power-cycled during the connect handshake
   (mid-connect peer reset), owned by PR #155 (fix/connect-crash-mid-drop).

This plan closes the sim-coverage gaps that let this class escape. It is tooling
and regression coverage only, no product fixes. Some new assertions fail today,
exposing the gaps the product PRs fix; they are marked WILL_FAIL (xassert) so
they flip to passing once the fixes land.

## What landed

### 1. Per-page connection-state sweep

`sim/scenarios/e2e/connstate-page-sweep.txt` visits every page reachable during
a connected session, drops the link on each, and asserts the drop is surfaced
there and clears on recovery. New `FURBLE_SIM` query seams in `src/FurbleUI.cpp`:

- `ui.link_alert`: a drop is surfaced on the current page (status-row reconnect
  icon, or a full-screen page's own banner). PASS-now on every page; this is the
  assertion that would have caught the blank shutter page.
- `ui.page_banner`: a full-screen page carries its own banner (`none` off the
  remote pages, `yes`/`no` on them).
- `action page <shutter|bulb|cameras|remote_timer|remote_gps>` reaches the
  connected sub-pages, and the `ui.page` query now names them.

Per-page coverage matrix (during a mid-session drop):

| page                     | surface                | link_alert | page_banner |
|--------------------------|------------------------|------------|-------------|
| connected menu (root)    | status-row icon        | yes        | none        |
| Remote shutter (F/S)     | own red banner (#154)  | yes        | yes         |
| Remote bulb (F/S)        | status-row icon only   | yes        | no WILL_FAIL|
| Cameras                  | status-row icon        | yes        | none        |
| Intervalometer           | status-row icon        | yes        | none        |
| GPS Data                 | status-row icon        | yes        | none        |

The Bulb page is as full-screen as the shutter page but has no dedicated banner,
so `ui.page_banner yes` there is WILL_FAIL until a bulb-page reconnect banner
lands (the shutter equivalent is PR #154).

### 2. Status-bar layout-stability and BT-colour matrix

`sim/scenarios/e2e/statusbar-stability.txt` runs across the disconnected /
connected / reconnecting / GPS matrix on all three panel widths. New query seams:

- `ui.bt_icon`: `hidden` / `plain` / `red` state of the status-row reconnect
  icon.
- `ui.battery_x` and `ui.battery_drift`: the battery icon x, and its shift from
  the anchor captured on the first read.

WILL_FAIL (xassert) lines:

- `ui.bt_icon red` during a reconnect: the icon appears but is not recoloured red
  in the status row (only the shutter-page banner is). WILL_FAIL on every panel
  until the red-status-BT fix.
- `ui.battery_drift 0` in a state whose icon set differs from the baseline: on
  the 80x160 panel the battery slides a couple of pixels right when the reconnect
  or GPS icon appears (anchor 52 to 54, and to 80 with both). WILL_FAIL on
  80x160; on 135x240 and 320x240 there is enough slack that it does not move, so
  those XPASS. Battery-anchor fix.

Same-icon-set states keep hard `assert ui.battery_drift 0` on every panel, so a
regression that moved the battery without an icon change turns red.

### 3. Connect-handshake fault fuzzing

`tests/host/fuzz/control_fuzz.cpp` gains `opHandshakePhaseDrop`, which injects a
peer reset or a rejected write at a randomised point across the connect handshake
(the pairing token exchange, then the identifier write), not only after a link is
established. It uses the existing `dropLinkOnWrite` / `failWrite` peer levers, so
it is additive and touches no shared MockNimBLE or FujifilmVirtualCamera symbol.
A deterministic `--repro handshake-phase-drop` guard is wired into CTest.

Coordination with PR #155 (fix/connect-crash-mid-drop): that branch adds the
deeper mid-connect hook (`FujifilmVirtualCamera::dropLinkDuringConnect` and
`NimBLEClient::mockDropLinkSelfDelete`) that frees a self-deleting client inline
while `_connect()` is still running, which is the exact mid-connect
use-after-free crash. This branch does not duplicate that symbol. A guarded
`opMidConnectSelfDeleteDrop` stub (`FURBLE_FUZZ_HAS_DROP_DURING_CONNECT`) is ready
to consume it and be added to the ops table once #155 merges.

## New harness surface

- `xassert <key> <value>` scenario verb: an expected-fail assert that prints
  `XFAIL (WILL_FAIL)` on a mismatch and keeps the run green, so a documented gap
  does not break CI. It prints `XPASS` once the value matches, prompting the fix
  PR to promote the line to a plain `assert`.

## Verification

- Sim built for all three panels (80x160, 135x240, 320x240). Both new scenarios
  run clean (rc 0) on each; the full e2e suite (29 scenarios) passes on 135x240.
- Host ctests build under ASan+UBSan; `control-fuzz-repro-handshake-phase-drop`
  and the seeded fuzz runs pass.
- clang-format 21 clean, no em-dashes.
- New scenarios wired into the sim-e2e job on all three panels; the fuzz repro is
  picked up by the host_camera ctest job.

## WILL_FAIL summary (flip green when the product PR lands)

| assertion                          | panels        | pending fix                       |
|------------------------------------|---------------|-----------------------------------|
| `ui.page_banner yes` on Bulb       | all           | bulb-page reconnect banner        |
| `ui.bt_icon red` on reconnect      | all           | red status-row BT icon (PR #156)  |
| `ui.battery_drift 0` off-baseline  | 80x160 only   | battery top-right anchor (PR #156)|

PR #156 (feat/reconnect-ui-polish) carries the red status-row BT icon and the
top-right battery anchor, so those two flip green when it lands; the bulb-page
banner is a still-open follow-up. The mid-connect crash class is exercised by the
handshake-phase fuzzing here and fully reproduced by PR #155's own host/ASan
regression test.
