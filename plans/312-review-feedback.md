# 312 - Review feedback for the MQTT queue fix

## Scope

This is the review-feedback record for #407 (`fix/mqtt-queue-psram`). Every
Copilot finding from the first review is fixed here, with the code and scenario
evidence.

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | `EVENT_QUEUE_BYTES` was 9,216, but four 2,348-byte `queued_event_t` items are 9,392 bytes. | Fixed. The budget is 9,392. The simulator, profile, scenarios, docs and host assertion use the exact value. | `include/FurbleMQTTMemory.h`, `tests/host/mqtt_test.cpp`, `sim/coverage/memory_budget.json` |
| 2 | The memory profile labelled the queue as PSRAM for every board, including Core2, which has no PSRAM. | Fixed. The profile selects `psram` only when the profile has PSRAM and `internal` otherwise. | `sim/memory_profile.cpp` |
| 3 | The internal queue fallback was reached after the plain start gate had already passed, so a board without PSRAM could admit a client whose 9,392-byte queue cannot fit. | Fixed. The start gate adds `EVENT_QUEUE_BYTES` to the internal free and largest floors when PSRAM is unavailable. The queue creation also rechecks internal free and largest before `xQueueCreate()`. | `src/FurbleMQTT.cpp`, `include/FurbleMQTTMemory.h`, host internal-fallback test |
| 4 | The README settings overview documented only the client floors, not the owner gate, PSRAM queue, internal fallback or connect-time camera snapshot. | Fixed. The README MQTT paragraph now documents the 18,432/12,288 owner gate, the TLS owner floor, the PSRAM queue with internal fallback, the connect-time camera snapshot and the discovery floor. | `README.md` |

## Validation

- Host `mqtt-owner` test includes the internal-fallback start gate case.
- Simulator memory profile and the MQTT memory scenarios pass.
- The hardware coverage and flash-readiness gates are rerun on the final commit.
