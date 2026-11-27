# 372 - Web installer chip family

Status: implemented on `fix/web-installer-chip-family`. No hardware flash and no
real browser run; the evidence below is generated output, gate output and the
regression test. The last section lists what stays unverified.

Related: [68](68-flasher-debug-firmware.md) (the debug manifests
this also covers), [113](113-per-board-ota-slots.md) (the per-board
partition sizes behind the shared offsets), [352](352-m5gfx-board-pin.md) (the
other board-identity defect that shipped because a gate checked the wrong
thing). Docs: [web-installer.md](../../docs/web-installer.md). Rules:
`web-installer/CLAUDE.md`.

## The defect

`web-installer/manifest.tmpl` contained two `builds` entries, one for each chip
family, and `web-installer/generate-manifest.py` only substituted `PLATFORM`,
`VERSION` and `ASSET_BASE_URL`. Every manifest therefore advertised both
families over the same four binaries. Generated for an M5StickS3 image with
`PLATFORM=m5stick-s3`, the pre-fix template produced:

```json
"builds": [
  {
    "chipFamily": "ESP32",
    "parts": [
      { "path": "firmware/abc/furble-bootloader-m5stick-s3-v9.9.9.bin", "offset": 4096 },
      { "path": "firmware/abc/furble-partitions-m5stick-s3-v9.9.9.bin", "offset": 32768 },
      { "path": "firmware/abc/furble-ota_data_initial-m5stick-s3-v9.9.9.bin", "offset": 61440 },
      { "path": "firmware/abc/furble-m5stick-s3-v9.9.9.bin", "offset": 131072 }
    ]
  },
  {
    "chipFamily": "ESP32-S3",
    "parts": [
      { "path": "firmware/abc/furble-bootloader-m5stick-s3-v9.9.9.bin", "offset": 0 },
      { "path": "firmware/abc/furble-partitions-m5stick-s3-v9.9.9.bin", "offset": 32768 },
      { "path": "firmware/abc/furble-ota_data_initial-m5stick-s3-v9.9.9.bin", "offset": 61440 },
      { "path": "firmware/abc/furble-m5stick-s3-v9.9.9.bin", "offset": 131072 }
    ]
  }
]
```

The first entry is an ESP32 build that writes an ESP32-S3 bootloader to
`0x1000`. ESP Web Tools reads `builds` and flashes the entry whose `chipFamily`
matches the chip it detected. It never checks that the binaries belong to that
family. A user with an ESP32-S3 board who selected the ESP32 entry wrote the
bootloader to `0x1000` instead of `0x0`, and the board did not boot until it was
re-flashed over UART. The mirror case, a classic ESP32 board on the S3 entry,
has the same outcome at the other address.

## Why the existing gates were green

Two independent checks passed on the broken manifests, which is why this
shipped:

- `tools/verify_web_installer.py` did not merely tolerate two families, it
  required them. The old `validate` failed a manifest unless `builds` held
  exactly two entries, one per family, and the old `build_family_error` error
  text was `expected ESP32 and ESP32-S3 builds`. The correct one-family manifest
  was rejected by this gate: pointing the pre-fix validator at a fixed site
  exits 1 with `manifest_m5stack-core.json: expected ESP32 and ESP32-S3 builds`.
- The per-entry offset check was correct on both entries. Offset `0x1000` is
  right for `ESP32` and `0x0` is right for `ESP32-S3`, so a check that reads one
  entry at a time cannot see the defect. It is an error of inventory, not of
  arithmetic.

`tests/test_web_installer.py` covered the same shapes: every test built one
`builds` entry by hand and checked its offsets. Nothing asserted how many
entries a generated manifest declares, or which family a given board gets.

## The fix

### One family per manifest, derived from the environment

`web-installer/manifest.tmpl` now has one `builds` entry and no numbers in it.
All five substituted values come from the new
`tools/web_installer_layout.py`:

- `CHIP_FAMILIES` at `tools/web_installer_layout.py:31` maps an ESP-IDF target
  name to the ESP Web Tools `chipFamily` string.
- `BOOTLOADER_OFFSETS` at `tools/web_installer_layout.py:39` maps that string to
  the offset the ROM loads the second stage bootloader from.

Neither is a board list. The board to family mapping is read, not restated:
`chip_family_for_platform` at `tools/web_installer_layout.py:106` resolves the
`PLATFORM` name to the sdkconfig that environment builds with and reads
`CONFIG_IDF_TARGET` from it. That is the file the binaries were built against,
so the manifest cannot disagree with them.

The resolver picks the longest sdkconfig base that prefixes the environment, the
way `tools/stack_budget.py` already does for stack accounting. `m5stick-s3` and
`m5stick-s3-debug` both land on `sdkconfig.m5stick-s3`, and `m5stack-core2`
never matches `sdkconfig.m5stack-core`, because a base only extends on a hyphen.
Fragments such as `sdkconfig.m5stack-core-usb-debug` carry no
`CONFIG_IDF_TARGET`, so they are not bases.

`render` at `web-installer/generate-manifest.py:33` is the one place that fills
the template. The CLI keeps the same three environment variables the workflows
already set. An environment that resolves to no sdkconfig, or a target with no
family, exits non-zero instead of publishing a manifest, so a misnamed matrix
entry fails the release job rather than shipping an image with no usable build.

### The gate now checks the family it expects

`tools/verify_web_installer.py` derives `EXPECTED_FLASH_OFFSETS` from the same
two tables and requires exactly one build per manifest whose family matches the
board in the file name. `build_family_error` at
`tools/verify_web_installer.py:46` is where the count is enforced, and
`validate` at `tools/verify_web_installer.py:115` also compares the declared
family with `expected_chip_family`, so a manifest named for an S3 board that
declares `ESP32` fails even if its offsets happen to be self-consistent.

### The page refuses a mismatched manifest

`web-installer/index.html` keeps the published URL contract: the install
button's `manifest` stays `./manifest_<board><variant>.json`. What changed is
that each board radio declares its family in a `data-chip-family` attribute,
and `verifyFamily` at `web-installer/index.html:151` fetches the selected
manifest and requires exactly one build with that family. The Connect button
stays disabled until the check passes and is disabled again on failure, with
the reason on screen. The check fails closed, including when the manifest cannot
be read at all.

This is defence in depth for the one case the generator cannot fix: a stale
manifest served from a cache or a browser still holding the old page. The page
cannot detect the chip before ESP Web Tools does; it can refuse the manifest
input, which is what it now does. The attributes are compared with the sdkconfig
derivation by the regression test, so the page cannot drift from the generator.

### What did not change

The manifest file names, the asset path shape, the `PLATFORM`, `VERSION`,
`ASSET_BASE_URL`, `REPOSITORY` and `RELEASE` variables, and both workflow steps
that generate the manifests. `pages.yml` and `release.yml` need no edit, so
`tools/check_ci_workflows.py` is unaffected.

## The regression test

`tests/test_web_installer.py:107` adds `WebInstallerChipFamilyTest`, which
renders real manifests through the generator and asserts, for every `builds`
entry, the family and the bootloader offset:

- `test_esp32_environment_declares_only_the_esp32_build` covers `m5stick-c` and
  `m5stack-core`: one build, `chipFamily` `ESP32`, bootloader `0x1000`.
- `test_s3_environment_declares_only_the_esp32_s3_build` covers `m5stick-s3`
  and `waveshare-s3-eth`: one build, `chipFamily` `ESP32-S3`, bootloader `0x0`.
- `test_every_published_manifest_declares_one_matching_family` covers all
  eleven published manifests, debug variants included.
- `test_two_chip_family_manifest_is_rejected` feeds the exact pre-fix
  two-entry output to the gate and to the validator.
- `test_pages_validation_accepts_the_generated_site` and
  `test_pages_validation_rejects_a_legacy_two_family_manifest` write a full
  deploy tree and run the real Pages step.
- `test_unknown_environment_fails_closed` and
  `test_cli_writes_the_manifest_the_page_requests` exercise the CLI.
- `test_page_labels_every_published_board_with_its_family` and
  `test_page_keeps_the_published_manifest_url_contract` hold the page to the
  same data and to the URL contract.

Against unmodified master sources this suite reports 3 failures and 21 errors.
The three assertion failures are the ones that describe the defect: the CLI
prints `chipFamily` `ESP32` for an S3 board, the page has no board to family
data, and an unknown `PLATFORM` exits 0. The most direct statement of the defect
is the count, taken from the pre-fix generator:

```
builds: 2
  ESP32    bootloader=0x1000 offsets=['0x1000', '0x8000', '0xf000', '0x20000'] MATCH=True
  ESP32-S3 bootloader=0x0    offsets=['0x0', '0x8000', '0xf000', '0x20000'] MATCH=True
expected exactly one build; declared 2
```

Every offset matched the family it was filed under. The defect was that there
were two families at all.

## Gates

Run on `fix/web-installer-chip-family` against `origin/master`:

- `python3 -m unittest tests.test_web_installer`: 18 tests, OK.
- `python3 -m unittest tests.test_check_ci_workflows`: 29 tests, OK. That
  suite runs the page's `updateManifest` in a Node `vm` with a stubbed
  `document`, which is why the manifest URL is still assigned synchronously and
  the family check runs afterwards.
- `python3 tools/pre_review.py --base origin/master`: see the PR.
- `python3 tools/check_doc_citations.py`: 0 errors.
- `python3 tools/check_ci_workflows.py`: no workflow changed.

## Not verified

- No board was flashed, in either direction. The claim that a bootloader at
  `0x1000` on an ESP32-S3 leaves it unbootable is the ROM load address, not a
  measurement here.
- The page's fetch-and-refuse path was not run in a browser. Only the manifest
  generation, the Pages gate and the page's synchronous manifest assignment are
  covered by tests.
- A cached pre-fix manifest in a browser that still shows the old page is
  refused by the check, but that behaviour is untested on a real deploy.
