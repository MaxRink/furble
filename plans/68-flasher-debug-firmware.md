# Web flasher debug firmware

## Motivation

The web flasher only offers the release firmware. Release builds omit the USB
serial console, so a user who hits a bug has no way to capture diagnostics
without a local PlatformIO toolchain. The debug envs already exist in
`platformio.ini` and enable `FURBLE_CONSOLE`, which adds the USB serial command
console and the Bluetooth diagnostic commands. This plan publishes those debug
builds through the same flasher, behind a single checkbox, so a bug reporter can
flash a diagnostic build in the browser.

This is PR-1 of a stacked pair. PR-2 (plan 69) adds a Web Serial panel that
drives the console it exposes.

## Design

### Release workflow

`.github/workflows/release.yml` gains a second matrix axis `variant` with the
values `""` and `-debug`. The build token becomes
`${{ matrix.platform }}${{ matrix.variant }}`, which is a valid PlatformIO env
for both the release and the `*-debug` envs. Ten build jobs run instead of five.

Every binary name, the build env, the rename source directory, and the manifest
filename use that combined token, so debug artifacts are
`furble[-part]-<board>-debug-<version>.bin` and `manifest_<board>-debug.json`
alongside the release ones. The `release` and `deploy` jobs already glob
`furble-*.bin` and `manifest*.json`, so they pick up the debug artifacts with no
further change.

### Manifest generation

`generate-manifest.py` and `manifest.tmpl` are unchanged. The debug variant is a
value of `PLATFORM`, not a new template. The workflow sets
`PLATFORM=<board>-debug`, and the `-debug` suffix flows straight into every
binary path in `manifest.tmpl`. Flash offsets and both chipFamily entries are
shared with the release path, because debug envs share the release `sdkconfig`
through `board_build.esp-idf.sdkconfig_path`. No offset table is duplicated.

### Flasher UI

`web-installer/index.html` gains one checkbox, "Debug build (serial console + BT
diagnostics)", placed under the board list, and a one line caption noting the
debug build is larger and enables the developer serial console. A single
`updateManifest()` helper reads the selected board radio and the checkbox and
sets `button.manifest = ./manifest_${board}${variant}.json`, where `variant` is
`-debug` when the checkbox is set and empty otherwise. Both the board radios and
the checkbox call the helper on change. The helper returns early when no board
is selected, so the install button stays hidden until a board is chosen, keeping
the existing reveal behavior.

## Files

- `.github/workflows/release.yml`: add the `variant` matrix axis and thread the
  combined platform+variant token through the build env vars, build, rename,
  manifest, and upload steps.
- `web-installer/index.html`: debug checkbox, caption, and the
  `updateManifest()` rewire.
- `web-installer/CLAUDE.md`: document the debug variant, the
  `manifest_<board>-debug.json` naming contract, and that debug builds carry
  `FURBLE_CONSOLE`.
- `plans/68-flasher-debug-firmware.md`, `plans/README.md`.

## Verification

- `python3 -c 'import yaml; yaml.safe_load(open(".github/workflows/release.yml"))'`
  parses.
- Run `generate-manifest.py` with `PLATFORM=m5stick-s3` and again with
  `PLATFORM=m5stick-s3-debug`. Both emit valid JSON through `python3 -m
  json.tool`. The debug manifest references `-debug` binary paths, and the
  offsets and chipFamily entries match the release manifest exactly.
- Trace the `index.html` JS: confirm the checkbox rewires `button.manifest` for
  every board and that the button stays hidden until a board is picked.
- Build one debug env to prove it compiles and produces the artifacts the
  manifest references:
  `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug` (retry once if
  the first dependency install hits the TinyGPSPlus fsmonitor quirk). Confirm
  `.pio/build/m5stick-s3-debug/firmware.bin` exists.
- Hardware verification: flash the m5stick-s3 debug build from the flasher, open
  the USB serial console, and confirm the console prompt responds.

## Implementation state

Implemented in this branch. The workflow, flasher UI, and docs are done.

## Verification results

- `yaml.safe_load` parses `release.yml` cleanly.
- `generate-manifest.py` was run for `PLATFORM=m5stick-s3` and
  `PLATFORM=m5stick-s3-debug`. Both passed `python3 -m json.tool`. The debug
  manifest references `furble[-part]-m5stick-s3-debug-<version>.bin`, and the
  offsets match the release manifest exactly: ESP32 4096/32768/61440/131072,
  ESP32-S3 0/32768/61440/131072.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug` succeeded in
  3m31s. The four artifacts the debug manifest references are present in
  `.pio/build/m5stick-s3-debug/`: `firmware.bin` (1290128 bytes),
  `bootloader.bin`, `partitions.bin`, `ota_data_initial.bin`.
- The diff was scanned for em-dashes and conflict markers; none present.
- Hardware verification on the M5StickS3 remains to be done before merge.
