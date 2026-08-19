# 84 Broaden host unit test coverage

## Motivation

The host test harness in `tests/host/` compiles real firmware sources and runs
them under a mock NimBLE stack, but most of its assertions flow through the
Fujifilm end to end camera path. Several pure logic modules had no direct host
coverage, so a regression in their byte construction or their storage
invariants would only surface on hardware. This plan adds standalone unit
suites for the cheapest, highest value pure logic targets. Every new suite is
picked up automatically by the existing `host_camera` CI job because it runs
`cmake -S tests/host` then `ctest`; no workflow YAML change is needed.

## Coverage audit

Modules were sorted by how much hardware or ESP-IDF shimming a host test needs.

Pure logic, cheap to test (added here):

- `lib/furble/protocol/CameraListProtocol.cpp` had no direct host test. It is
  self contained byte construction for the saved camera index: `addressKey`,
  `encodeIndex`, `decodeIndex`, `upsertIndex`.
- `lib/furble/protocol/FujifilmProtocol.cpp` was compiled into the camera
  harness but only its happy path ran. The parse guards, the service flag
  matching, and the geotag byte packing had no negative or boundary coverage.
- `src/FurbleSettings.cpp` settings table. The protocol conformance suite
  already checks wire ids against the golden corpus, but the NVS key and
  namespace length limits were unchecked. ESP-IDF caps both at fifteen
  characters and silently truncates longer names, which could collide two
  keys. This invariant is verified by parsing the table from source, the same
  approach the protocol suite already uses for wire ids.

Hard to shim, deferred (noted, not added):

- `src/FurbleGPS.cpp`. The NMEA `checksum` and the `$PCAS` command builder are
  pure, but they are private static members of a class that pulls in
  M5Unified, TinyGPS++, lvgl, FurbleControl, and FurblePower. Reaching them on
  the host needs either a production refactor to extract the pure helpers or a
  large shim stack. Deferred to keep this change test only.
- `src/FurbleSettings.cpp` load and save paths. Compiling the translation unit
  needs `esp_bt.h`, `nvs_flash.h`, and a real NVS backed `Preferences`
  implementation. That is a substantial mock, deferred.
- `lib/furble/CameraList.cpp`. `save`, `load`, and the round trip need NVS
  backed `Preferences` plus every vendor class. Its index codec is already
  covered here through `CameraListProtocol`.
- Sony, Nikon, Canon, and Ricoh encoders. Unlike Fujifilm, these vendors have
  not extracted a pure protocol module. Their byte construction is inline in
  NimBLE coupled classes, so a unit test needs the mock BLE stack and, for a
  clean encoder test, a production refactor. Deferred.

## What was added

New standalone suites in `tests/host/`, each with its own CTest target appended
to `tests/host/CMakeLists.txt`:

- `camera_list_protocol_test.cpp` (`camera-list-protocol`): address key
  formatting and width, encode and decode round trips, the little endian type
  layout, empty input, upsert insert versus replace, and rejection of a length
  that is not a whole record and of a null pointer with a nonzero length.
- `fujifilm_protocol_test.cpp` (`fujifilm-protocol`): company id acceptance and
  rejection, basic and secure advertisement parsing with wrong type and wrong
  length negatives, service flag matching combinations, configuration and
  geotag notification boundaries, all three shutter frames, and geotag packing
  including a negative longitude two's complement, altitude truncation, the
  reserved zero bytes, and every date field offset.
- `settings_table_test.cpp` (`settings-table`): parses `src/FurbleSettings.cpp`
  and asserts every NVS key and namespace is within the fifteen character
  limit, keys are non-empty, exposed wire ids are unique, and the table has at
  least twenty rows.

The first two suites link their production `.cpp` directly because those
modules have no BLE or NVS dependency. The settings suite reads the source as
text, so it needs no furble sources on its link line. None of the fenced
lifecycle files or the mock BLE stack were touched.

## Verification

- Built with the PlatformIO bundled cmake under `-Wall -Wextra -Werror`. All
  five host tests pass, including the two pre-existing suites.
- Teeth spot checks, each reverted after:
  - CameraListProtocol: swapping a type byte shift from `>> 8` to `>> 16` fails
    `camera-list-protocol`.
  - FujifilmProtocol: changing the shutter press parameter fails
    `fujifilm-protocol`.
  - Settings table: lengthening an NVS key to sixteen characters fails
    `settings-table`.
- clang-format 21 clean on all new files.

## Remaining work

GPS pure helpers, the settings load and save paths, CameraList persistence, and
the non Fujifilm vendor encoders remain uncovered on the host. Each needs
either a production refactor to extract pure helpers or a heavier ESP-IDF and
NVS mock, and is out of scope for this test only change.
