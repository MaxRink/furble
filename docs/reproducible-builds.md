# Reproducible firmware builds

The six release firmware environments enable ESP-IDF's
`CONFIG_APP_REPRODUCIBLE_BUILD`. ESP-IDF then normalizes compiler paths,
component ordering, and application metadata. The About page uses the same
deterministic UTC timestamp instead of the compiler's `__DATE__` and `__TIME__`
macros.

`patches/reproducible.py` derives `SOURCE_DATE_EPOCH` from the checked-out
commit timestamp when it is not supplied. CI and local builds therefore use
the same timestamp for a commit. An explicit non-negative
`SOURCE_DATE_EPOCH` can be supplied when building an exported source archive.

PlatformIO, the Espressif platform and framework, and each registry or Git
dependency are pinned. The current firmware toolchain is Espressif32 6.13.0
with ESP-IDF 5.5.3 (`framework-espidf@3.50503.0`). The pins are recorded in
`tools/reproducible-build.lock`; the verification script rejects drift between
that file and `platformio.ini` or `requirements.txt`.

From a checkout with the pinned PlatformIO installation:

```text
python3 tools/reproducible_build.py --check-inputs
python3 tools/reproducible_build.py --env m5stick-s3 --negative-version
```

The script creates two clean copies in different absolute directories, gives
each copy an isolated PlatformIO core, builds the selected environment in each,
and compares the SHA-256 of the ELF, firmware, bootloader, partition table, and
initial OTA data image. The isolated core is important because the project has
a checked-in NimBLE patch and PlatformIO normally shares framework packages
between checkouts. The negative test changes `FURBLE_VERSION` and requires at
least one artifact to change. CI runs the same gate for every release board.

The shared flags fix GCC's random seed and the ESP-IDF pre-build patch maps both
logical and real checkout paths. Both are needed on macOS, where `/tmp`
resolves to `/private/tmp`, so optimizer decisions and ELF debug data cannot
retain checkout-specific differences.

The NimBLE pre-build patch is serialized per framework path, is idempotent, and
fails on a rejected hunk. It never accepts a partial patch or leaves a reject
file that could silently change the firmware. Its second-application,
concurrency, and rejected-hunk tests run as a CI step.

The guarantee is for identical source, pinned inputs, target configuration,
and build timestamp. Signed or encrypted images remain dependent on their
signing or encryption keys and are outside this unsigned-build gate. Files in
`.pio` are build-system state and are not release artifacts. A future change
that introduces a new generated asset or timestamp must extend the artifact
list and this gate before it is considered reproducible.
