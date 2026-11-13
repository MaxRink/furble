# 316: docs capture stops on a shadowed companion-reject action

Implementation state (2026-11-13): merged as #419. The wiki copy still needs a manual push.

## Motivation

`sim/scripts/docs-capture.sh` stopped on master. The companion pairing box
from the `companion-pairing` capture stayed open over the connect captures.
`connecting`, `connected` and `remote` showed the "Pair device" box. The camera
prompt could not open under it, so `camera-pair-reject` returned
`VALID_NO_EFFECT` and the script exited 1.

The report blamed the non-rig build. That was a red herring. The docs capture
is the only script that sends `action companion-reject` to the UI, and it
always builds with `FURBLE_SIM_RIG=0`. A rig build behaves the same.

## Root cause

PR #399 (`fa499564`) added a hardware-model action named `companion-reject`
to `sim/hardware_model.cpp`. It models the companion refusing an unbonded
central and backs `e2e/hw-ble-auth-gatt-reidentify.txt`. The driver offers
every action to `hwAction()` before the UI dispatch. So the scenario line
`action companion-reject` incremented `companion.rejected`, logged
"companion rejected an unbonded central" and reported `APPLIED`. The UI's own
`companion-reject`, which clicks the pairing dialog's Reject button, never ran.
The injected pairing stayed pending and the pairing timer kept the dialog open.

It is a simulator seam collision, not a product path. No firmware source is
involved, and companion pairing is not enabled by default. The fuzzer was not
affected because it calls `UI::simScenarioAction` directly.

A scan of every `hwAction()` name against the strings `UI::simScenarioAction`
compares found `companion-reject` is the only collision.

## Fix

- Rename the hardware-model action to `companion-reject-central`. Add it to
  both action allowlists in `sim/scenario_action.cpp` and to `docs/sim.md`.
  `e2e/hw-ble-auth-gatt-reidentify.txt` uses the new name and keeps every
  assertion.
- `action companion-reject` again reaches the dialog's Reject handler.
- `e2e/companion-pairing-input.txt` gains a reject leg that asserts the modal
  closes and focus returns to the page. The same sequence on the pre-fix
  binary left `ui.modal open` after the reject.
- `sim/scripts/docs-screenshots.txt` asserts `ui.modal closed` after the
  reject, so the capture fails at the cause rather than three captures later.

The branch starts from PR #415, which adds the `heap_count.h` include the
non-rig `sim/CompanionRigTransport.cpp` branch needs to compile.

## Regenerated captures

With the fix, `connecting`, `remote` and `camera-pairing` are byte-identical
to the committed images. They only differed in the stopped run because the
pairing box covered them. These captures did change:

| Images | Change | Cause |
| --- | --- | --- |
| `diag-device-info`, `textsize/{small,normal}/device-info`, `scroll/{default,dark}/about-{2,3}` | Heap reads 175056 B on Device info and 189520 B, min 187040 B on About, not 204800 B | #412 and #413 seed each board with its code-derived boot heap and charge device allocations |
| `textsize/large/{connected,device-info,settings}` | The header title is no longer cut at the top edge | The PR #415 header row fix |
| `connected` | One pixel in the Bulb icon is one antialiasing shade lighter | Not isolated. It reproduced byte-identically in two runs on this branch |

The wiki copies of `connected` and `diag-device-info` are updated in
`docs/wiki/img/`. The wiki needs a push. `docs/ui-walkthrough.md` embeds
these files by path and quotes no heap value, so its text is unchanged.

## Implementation state

Implemented. `docs-capture.sh` completes on all three boards and themes.
