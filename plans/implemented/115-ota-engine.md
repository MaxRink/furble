# 115 - Transport-independent OTA engine

## Goal

Land the deterministic OTA lifecycle before any network transport or user-facing
command depends on it. The engine must be directly host-testable and must reject
inconsistent transport progress without writing firmware itself.

This is the state-machine slice of `plans/34-ota-partitions.md`. PR #167 landed
the two-slot layouts first. HTTPS delivery, manifest lookup, precondition checks,
boot confirmation, rollback, console commands, MQTT triggers, and reboot policy
remain follow-up slices.

## Design

`OTA::Engine` owns the lifecycle states and a value-only snapshot. An injected
`OTA::Transport` owns all I/O. `step()` advances exactly one state or download
poll, so a later firmware task, simulator fake, or host test can drive the same
production state machine without delays or hidden threads.

Transport byte counts are absolute. They may reveal an initially unknown total,
but they may not regress, exceed the total, change a known total, or report
completion before all known bytes arrive. Every transport failure and invariant
violation calls the idempotent `abort()` seam.

The engine accepts an opaque non-empty source string. HTTPS-only policy belongs
to the HTTPS transport rather than this transport-independent layer.

## Verification

The host suite injects a recording fake and covers:

- clean progress through check, download, verify, apply, and done;
- busy rejection without disturbing the active update;
- explicit abort and checkpoint preservation;
- check, download, verify, and apply failures;
- invalid requests and invalid resume offsets;
- unknown totals revealed during download;
- regressing, oversized, incomplete, and total-changing progress;
- monotonic percentage reporting and retry from a checkpoint.

Firmware builds compile the same engine source. This slice has no hardware I/O,
console surface, network transport, or simulator UI, so no physical test or UI
scenario applies. The HTTPS follow-up must add a simulator transport and fault
scenarios before it adds hardware delivery, then pass an on-device update and
rollback test before merge.

## Implementation status

Implemented by PR #168. The original branch also exposed a blocking console
command and an `esp_https_ota` adapter. Review removed both because WiFi,
preconditions, manifest checks, boot confirmation, and an independently usable
abort path were not present. Keeping this PR as the injected engine preserves a
small, independently mergeable base for those tested follow-ups.
