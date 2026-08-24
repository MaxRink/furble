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
- Numbering: 0x small improvements, 1x-2x phased features, 30+ framework work,
  50+ design documents, 90+ deferred ideas. Do not renumber existing docs.
- `00-hardware-experiments.md` records measured hardware facts (crystal, GPS
  backup rail, $PCAS support). Cite it instead of re-measuring.
- Restart and BLE recovery plans must cover both clean shutdown and unclean
  reset paths, and must include an immediate reconnect acceptance test.
- Settings concurrency plans must distinguish ESP-IDF NVS thread safety from
  the mutable lifetime of each `Preferences` wrapper handle.
