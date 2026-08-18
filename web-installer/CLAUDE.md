# web-installer/

ESP Web Tools flashing page. Manifests are generated at release time, not
committed.

- `generate-manifest.py` renders `manifest.tmpl` from the `PLATFORM` and
  `VERSION` env vars, one manifest per board env.
- Flash offsets differ by chip: bootloader at 0x1000 on ESP32, 0x0 on
  ESP32-S3. Shared offsets: partition table at 0x8000, otadata at 0xf000,
  app at 0x20000. Keep both `builds` entries in sync when changing
  partitions.
- Firmware binary names follow `furble[-part]-$PLATFORM-$VERSION.bin`. Any
  rename must match the release workflow in `.github/workflows/`.

## Debug variant

- Each board also ships a debug build from the matching PlatformIO `*-debug`
  env. Debug builds carry `FURBLE_CONSOLE`, which adds the USB serial command
  console and the Bluetooth diagnostic commands.
- The debug variant is a value of `PLATFORM`, not a separate template. The
  release workflow sets `PLATFORM=<board>-debug`, and the `-debug` suffix flows
  straight into every binary path in `manifest.tmpl`. Flash offsets and
  chipFamily entries are shared with the release path, because debug envs share
  the release `sdkconfig`. `generate-manifest.py` needs no variant logic.
- Naming contract: the debug manifest is `manifest_<board>-debug.json` and its
  binaries are `furble[-part]-<board>-debug-$VERSION.bin`. These sit next to
  the release `manifest_<board>.json` in the deploy root and `deploy/firmware`.
- `index.html` has a single "Debug build" checkbox. The install button manifest
  is `./manifest_${board}${variant}.json`, where `variant` is `-debug` when the
  checkbox is set and empty otherwise. The manifest is recomputed on both board
  and checkbox change.
