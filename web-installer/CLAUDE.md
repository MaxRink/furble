# web-installer/

ESP Web Tools flashing page. Manifests are generated at release time, not
committed.

- `generate-manifest.py` renders `manifest.tmpl` from the `PLATFORM` and
  `VERSION` env vars, one manifest per board env. Asset paths use the
  `ASSET_BASE_URL` env var. Without it, they target a MaxRink/furble release.
- Flash offsets differ by chip: bootloader at 0x1000 on ESP32, 0x0 on
  ESP32-S3. Shared offsets: partition table at 0x8000, otadata at 0xf000,
  app at 0x20000. Keep both `builds` entries in sync when changing
  partitions.
- Firmware binary names follow `furble[-part]-$PLATFORM-$VERSION.bin`. Any
  rename must match the release workflow in `.github/workflows/`.
- `.github/workflows/pages.yml` builds the five release environments on a tag
  and publishes the page with the generated manifests and binaries.

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

## Capture BT debug dump panel

- `index.html` carries a collapsed "Capture BT debug dump" panel below the
  install button, plus its own `<script id="bt-dump-script">` block. It drives
  the debug firmware console over the Web Serial API so a reporter can attach a
  Bluetooth diagnostic transcript to a camera bug report without a terminal.
- It needs a debug build flashed first. The debug build carries `FURBLE_CONSOLE`
  and the `bt` command family (journal, scan, explore). A release build has no
  console, so the panel reports "flash a debug build first" and disconnects.
- esp-web-tools and a `navigator.serial` console cannot hold the same port at
  once. The flow is sequential: flash with the install button, close its dialog,
  then click Connect in this panel. The panel never blocks flashing.
- Transport is USB CDC at 115200 8N1, the same port the S3 flasher uses. The
  script reads with a `TextDecoderStream`-style loop, splits the stream on
  newlines, and detects the `furble> ` prompt as the unterminated tail of the
  buffer. The device echo of each sent command is suppressed once so the
  transcript shows each command a single time behind a `> ` marker.
- Command completion: journal commands finish when the prompt returns. `bt scan`
  and `bt explore` return the prompt immediately then stream asynchronously, so
  they finish on their own end marker instead: `bt.scan: done` (or a
  `bt.scan: refused` line) and `explore.read: end` (or `bt.explore: refused` /
  `explore.connect_failed`). Every command has a timeout that is logged to the
  transcript and swallowed, so one silent command cannot hang the capture.
- Buttons: Connect / Disconnect, Start recording (`bt journal clear` then
  `bt journal on`), Stop and dump journal (`bt journal dump` then
  `bt journal off`), Run scan (`bt scan <seconds>`), Run explore
  (`bt explore <addr>` then `bt explore stop`), plus Download dump (a
  `furble-bt-dump-<timestamp>.txt` blob), Copy and Clear.
- Web Serial is Chromium only (Chrome, Edge) and needs a secure context, which
  the deployed HTTPS page satisfies. The panel feature-detects `navigator.serial`
  and shows a one line note on unsupported browsers instead of the controls.
- The command surface is owned by fork PR #76 (`feat/64-bt-debug`). If it renames
  a `bt` command or an end marker, update `endConditionFor` and the button
  handlers in the script to match.
