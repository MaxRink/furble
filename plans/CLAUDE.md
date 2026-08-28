# plans/

Numbered improvement plans. One document per planned PR against upstream
gkoh/furble. `README.md` here is the index and dependency graph.

Note: this directory lives on the `plans` branch until the integration merge
lands on fork master. This file applies once it does.

- Each PR must update its own plans/NN doc before merge: implementation state,
  and any deviation from the written plan with the reason.
- Fork-only delivery changes record their implementation state in the related
  plan even when the upstream plan says the work has no PR.
- Docs state motivation first, then design. Keep the upstream maintainer's
  preferences in mind: issue first, fewer UI elements, defaults keep current
  behavior, everything new is configurable.
- Camera compatibility plans record shared protocol bytes, model-specific
  additions, and hardware validation status explicitly.
- Fujifilm Secure registration plans keep the camera's required peer profile
  through subscriptions and shutter discovery. Request Furble's bounded FAST
  profile only after discovery completes, then verify the controller applied
  it before declaring the camera active.
- Ricoh `OperationRequest {0x01, 0x01}` is capture with autofocus, not a
  focus-only or half-press command. Keep focus gestures as no-ops until a
  distinct operation is verified from protocol evidence and hardware.
- Numbering: 0x small improvements, 1x-2x phased features, 30+ framework work,
  50+ design documents, 90+ deferred ideas. Do not renumber existing docs.
- `00-hardware-experiments.md` records measured hardware facts (crystal, GPS
  backup rail, $PCAS support). Cite it instead of re-measuring.
- Charging auto-off policy work updates `13-auto-off-low-batt.md` and reserves
  wire id 42 for timezone. The charging opt-in uses wire id 43 only after an
  audit of current master and open relevant branches. Wire id 45 remains
  unavailable. Keep the default auto-off behavior unchanged on boards without
  charging telemetry, suppress auto-off while charging by default, and avoid
  NVS writes from policy ticks.
- Restart and BLE recovery plans must cover both clean shutdown and unclean
  reset paths, and must include an immediate reconnect acceptance test.
- Settings concurrency plans must distinguish ESP-IDF NVS thread safety from
  the mutable lifetime of each `Preferences` wrapper handle.
- OTA plans separate the initial USB-flashed bootloader and partition contract
  from later application updates. Rollback, validation, range resumption, and
  interrupted-write recovery each need explicit simulator and hardware gates.
- Build identity plans must preserve explicit release versions. Development
  versions include enough Git identity to tie a hardware result to source.
