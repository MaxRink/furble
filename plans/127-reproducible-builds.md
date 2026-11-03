# 127 - Reproducible firmware builds

## Motivation

Release images should be independently rebuildable from a commit and a pinned
toolchain. Build directories, host paths, and wall-clock time must not alter
the firmware bytes.

## Implementation state

- All six release sdkconfigs enable `CONFIG_APP_REPRODUCIBLE_BUILD`.
- The PlatformIO pre-build script derives `SOURCE_DATE_EPOCH` from the commit
  timestamp and supplies deterministic UTC text to the About page.
- PlatformIO and firmware dependencies are recorded in
  `tools/reproducible-build.lock`.
- `tools/reproducible_build.py` builds two distinct absolute copies, compares
  the ELF and all flash images, and has a negative version-change test.
- Each comparison copy gets an isolated PlatformIO core with its own mutable
  ESP-IDF framework package. The shared-framework NimBLE patch is locked,
  idempotent, and rejects partial application.
- CI runs the comparison for every release board.

## Verification

Run the input check and one or all release-board comparisons:

```text
python3 tools/reproducible_build.py --check-inputs
python3 tools/reproducible_build.py --env m5stick-s3 --negative-version
```

The check requires the same source, target configuration, pinned tools, and
`SOURCE_DATE_EPOCH`. Signed and encrypted images are not covered.
