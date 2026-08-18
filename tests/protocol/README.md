# Companion protocol corpus

This directory is a host-only Phase 1 conformance suite. It has no ESP-IDF,
NimBLE, Android, simulator, or emulator dependency.

The generator is a C++ program. It includes the real
'include/FurbleCompanion.h' through the small host stubs in 'stubs/', so the
packed location and status definitions remain the source of truth. It reads
the firmware settings table in 'src/FurbleSettings.cpp' and the firmware wire
type mapping in 'src/FurbleCompanion.cpp'. The generated binary files and
'golden/corpus.json' are checked in. Each metadata record carries the
characteristic kind, direction, expected result, and expected bytes.

The settings list records use the firmware order:

~~~
status, id, type, flags, length, value
~~~

Non-list responses use:

~~~
status, id, type, length, value
~~~

Trigger fixtures use the fixed four-byte packet from the Android protocol:
version, operation, hold_ms in little-endian order. Non-timed operations set
hold_ms to zero.

## Run locally

From the repository root:

~~~
make -C tests/protocol CXX=clang++ generate
make -C tests/protocol CXX=clang++ test
~~~

'generate' is the only command that rewrites the checked-in corpus. The test
binary validates the manifest, all binary lengths, struct field bytes, every
nonzero setting wire ID, TLV status coverage, trigger operations, and the
service UUID offsets in the C++ header and Kotlin source text.

Use 'make -C tests/protocol CXX=clang++ format-check' with clang-format 21.
