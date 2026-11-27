# Web installer

The browser flasher at <https://maxrink.github.io/furble/>. It is a static page
plus one ESP Web Tools manifest per board, published by
`.github/workflows/pages.yml` on a `v*` tag. Nothing here is built into the
firmware; the manifests are generated at release time and are not committed.

## Why one chip family per manifest

ESP Web Tools reads `builds` and flashes the entry whose `chipFamily` matches
the chip it detected on the connected board. It does not check that the entry's
binaries belong to that family; it trusts the manifest.

Until plan 372 every generated manifest carried both entries: an `ESP32` entry
with the bootloader at `0x1000` and an `ESP32-S3` entry with it at `0x0`, both
pointing at the same single-family binaries. A manifest for an S3 board
therefore also offered a classic ESP32 image. A user who picked the entry for
the other family wrote a bootloader to the wrong offset, and the board did not
boot again until it was re-flashed over UART. The two entries were internally
consistent, so the offset checks of the Pages gate passed and the defect
shipped.

A manifest now declares exactly one `chipFamily`, and the family comes from the
board the PlatformIO environment targets. The generated file looks like this
(`web-installer/manifest.tmpl`):

```json
{
  "name": "furble",
  "version": "v0.9.0",
  "new_install_prompt_erase": true,
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "firmware/<sha>/furble-bootloader-m5stick-s3-v0.9.0.bin", "offset": 0 },
        { "path": "firmware/<sha>/furble-partitions-m5stick-s3-v0.9.0.bin", "offset": 32768 },
        { "path": "firmware/<sha>/furble-ota_data_initial-m5stick-s3-v0.9.0.bin", "offset": 61440 },
        { "path": "firmware/<sha>/furble-m5stick-s3-v0.9.0.bin", "offset": 131072 }
      ]
    }
  ]
}
```

The four offsets are fixed per family:

| Part | ESP32 | ESP32-S3 |
| :--- | :--- | :--- |
| `bootloader.bin` | `0x1000` (4096) | `0x0` (0) |
| `partitions.bin` | `0x8000` (32768) | `0x8000` (32768) |
| `ota_data_initial.bin` | `0xF000` (61440) | `0xF000` (61440) |
| application | `0x20000` (131072) | `0x20000` (131072) |

## Where the family comes from

`web-installer/generate-manifest.py:33` renders the template for one `PLATFORM`,
a PlatformIO environment name such as `m5stick-s3` or `m5stick-s3-debug`. It
does not own a board list. `chip_family_for_platform` at
`tools/web_installer_layout.py:106` resolves the environment to the sdkconfig
that environment builds with, and reads `CONFIG_IDF_TARGET` from it. That file
is what produced the binaries, so the manifest cannot disagree with them. An
environment that matches no sdkconfig, or a target with no mapping, fails the
job instead of publishing a manifest with no usable build.

Two tables in `tools/web_installer_layout.py` are the only hand-written data:

- `CHIP_FAMILIES` at `tools/web_installer_layout.py:31` maps an ESP-IDF target
  name to the ESP Web Tools `chipFamily` string.
- `BOOTLOADER_OFFSETS` at `tools/web_installer_layout.py:39` maps that string
  to the offset the ROM loads the second stage bootloader from.

Adding a board on a new chip means adding both entries after checking the
ESP-IDF load address for that family, plus an sdkconfig for the new environment.
Adding a board on a supported chip needs no change here: the `board` line in
`platformio.ini` and a `sdkconfig.<board>` with the right `CONFIG_IDF_TARGET`
are enough.

## The page

`web-installer/index.html` keeps the published URL contract: the install
button's `manifest` property is `./manifest_<board><variant>.json`, where
`<variant>` is `-debug` when the debug checkbox is set and empty otherwise. The
manifest file names and paths did not change with plan 372, so an already
published site keeps working.

Each board radio carries a `data-chip-family` attribute
(`web-installer/index.html:51`), which is the family that board's manifest must
declare. `updateManifest` at `web-installer/index.html:213` sets the manifest
URL, then `verifyFamily` at `web-installer/index.html:160` fetches it and
requires exactly one build with that family. The Connect button stays disabled
until the check passes. A manifest that declares a different family, or more
than one, is refused with the reason on screen. The check fails closed: a
manifest that cannot be read also keeps the button disabled, because an
unverified manifest is the shape that bricks a board.

Two more refusals keep the button disabled. A radio with no `data-chip-family`,
or an empty one, is refused before anything is fetched, because the page cannot
check an image it cannot name; a new board that forgets the attribute therefore
loses the click instead of skipping the check. And every request carries the
generation of the selection that started it, so a manifest answer that arrives
after a later board was picked is dropped: a stale success cannot re-enable
Connect for a board that failed, and a stale failure cannot disable a board that
passed.

The page still cannot detect the chip before it flashes; ESP Web Tools does that
after the port is granted. The refusal covers the manifest, which is the input
the generator controls. The `data-chip-family` attributes and the sdkconfig
derivation are compared by `tests/test_web_installer.py:100`, so the two cannot
drift apart silently. The refusals themselves are executed rather than grepped:
`tests/web-installer/manifest.test.cjs` runs this page's own script in Node
against a fake DOM and a fake fetch, and `tests/test_web_installer.py` runs that
file with the tooling suite.

## Publishing

`pages.yml` builds six release environments and their `-debug` variants on a
tag, except the developer-only M5Stack Core debug profile. Each job generates
its own manifest with `PLATFORM` set to the environment name and
`ASSET_BASE_URL` set to `firmware/<sha>`, uploads it, and the deploy job merges
the artifacts, copies `index.html` and `pmic-preflight.js` into the site root,
and runs `python tools/verify_web_installer.py site`.

That gate (`validate` at `tools/verify_web_installer.py:115`) rejects:

- a missing or unexpected manifest name, against the eleven published ones;
- a manifest that does not declare exactly one supported chip family, which is
  the check `build_family_error` at `tools/verify_web_installer.py:39` makes;
- a `chipFamily` that does not match the board its file name names;
- offsets that differ from that family's layout;
- an asset path that is not local, unsafe, missing, empty, or named for another
  version;
- a firmware asset no manifest references.

The manifest is also a release artifact: `release.yml` attaches the same
generated `manifest_*.json` files to the GitHub release, so a release and the
Pages deploy cannot disagree about a board's family.

## Related

- [Supported hardware](supported-hardware.md) has the board list and status.
- [Getting started](wiki/Getting-Started.md) is the user-facing flashing guide.
- `web-installer/CLAUDE.md` holds the rules for changing the page.
- `plans/implemented/372-web-installer-chip-family.md` records the defect and this fix.
