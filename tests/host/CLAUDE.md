# tests/host/ (host regression tests)

Host tests must exercise real production control flow and preserve diagnostic
output on failure. ThreadSanitizer wrappers are fail-closed: every sanitizer
warning and every non-zero child status fails, regardless of report names or
the sanitizer's conventional exit code. The wrapper's exact completion marker
is `control-connect-camera-race: PASS`; near matches do not count. Keep the
shell contract test runnable without a compiler so wrapper behavior remains
covered even when the TSAN binary is unavailable.

Provisioning tests cover Legend Off through the production apply path and NVS
store. Keep `Furble::Settings` qualified outside its namespace; run both the
ordinary and MQTT-enabled provisioning targets when changing this coverage.
