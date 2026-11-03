# 296 - carry companion accuracy into the GPS fix

## Motivation

Both companion apps already put their horizontal accuracy on the wire:
`accuracy_m` in `companion_fix_t`, with 255 meaning unknown. The firmware
parser dropped the field, so `external_fix_t.accuracy_valid` stayed false and
the MQTT `accuracy` field never populated. Any future "do not tag with a poor
phone fix" gate needs this value.

## Implementation state

`CompanionService` now copies the byte into `fix.accuracy_m` and sets
`accuracy_valid` unless the value is 255. `GPS::setExternalFix()` already
validates finite non-negative accuracy, so a malformed direct caller is still
rejected.

The value now reaches:
- the external fix snapshot used by the GPS paths;
- the MQTT geolocation payload's existing `accuracy` field;
- a future quality gate without another wire change.

## Validation

- `companion-gatt`: the location write asserts `accuracy_valid == true` and
  `accuracy_m == 4`, and a second write with `255` proves the unknown sentinel
  clears `accuracy_valid`.
- Full host CTest: 140/140 after the rebase onto the static-analysis gate.
- Static analysis: PASS.
- Simulator build passes; the change is in the shared companion parser.

## Remaining gate

The threshold for rejecting a poor phone fix is deliberately not chosen here.
It needs outdoor phone data and belongs with the GPS quality gate.
