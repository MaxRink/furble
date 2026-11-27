# web-installer/

ESP Web Tools flashing page. Manifests are generated at release time, not
committed.

- `generate-manifest.py` renders `manifest.tmpl` from the `PLATFORM` and
  `VERSION` env vars, one manifest per board env. Asset paths use the
  `ASSET_BASE_URL` env var. Without it, they target a MaxRink/furble release.
- A manifest declares exactly one `chipFamily`, the family the environment
  actually targets. `generate-manifest.py` reads it from the `CONFIG_IDF_TARGET`
  of the sdkconfig that environment builds with, through
  `tools/web_installer_layout.py`. Never add a second `builds` entry: ESP Web
  Tools matches the entry by the chip it detects, so a second entry lets a user
  flash one board's binaries at the other family's bootloader offset. Plan 372
  records that defect and the board it bricked.
- Flash offsets differ by chip: bootloader at 0x1000 on ESP32, 0x0 on ESP32-S3.
  Shared offsets: partition table at 0x8000, otadata at 0xf000, app at 0x20000.
  All four come from `tools/web_installer_layout.py`, which
  `tools/verify_web_installer.py` and `tests/test_web_installer.py` also use.
- `index.html` carries a `data-chip-family` attribute on every board radio. The
  page fetches the selected manifest, refuses it unless it declares exactly that
  one family, and only then enables the Connect button. It fails closed: a
  manifest it cannot read keeps the button disabled, and so does a radio with no
  or an empty attribute, which is refused before anything is fetched. Every
  request carries the generation of the selection that started it, so a late
  answer to an older board is dropped rather than re-enabling Connect. Keep the
  attribute in step with the sdkconfig target; the regression test compares
  them, and `tests/web-installer/manifest.test.cjs` drives the refusals in Node.
- Firmware binary names follow `furble[-part]-$PLATFORM-$VERSION.bin`. Any
  rename must match the release workflow in `.github/workflows/`.
- `.github/workflows/pages.yml` builds the six release environments on a tag
  and publishes the page with the generated manifests and binaries.
- Pages is tag-only. Each deployment stores firmware below a commit-specific
  `firmware/<sha>/` path so a manifest cannot silently reuse an older build.
- The Pages deploy validates all manifests and referenced binaries before
  publishing. Keep that check green when changing artifact names or paths.

## M5StickS3 PMIC preflight

- `pmic-preflight.js` runs the same `flash prepare` handshake as
  `tools/flash_prepare.py` through Web Serial before the ESP Web Tools dialog
  opens. It requires exact acknowledgements for readiness, a disabled PMIC
  watchdog, and unlocked long-press download recovery.
- The preflight closes its port before replaying the installer activation. ESP
  Web Tools then requests the same port again for chip detection and flashing.
  This second permission selection is intentional because two Web Serial
  readers cannot own the port at once.
- The page applies this gate only to M5StickS3. Other boards do not have the
  M5PM1 hazard. If a StickS3 release or wedged image cannot answer, the page
  refuses to flash and displays the true-power-loss recovery steps.
- Keep the browser protocol and the terminal helper behaviorally equivalent.
  Update `tests/test_tooling_versions.py` when either acknowledgement changes.

## WiFi and settings provisioning

- `provision.js` is the browser half of
  `plans/implemented/114-flasher-provisioning.md`. The `provisionPanel` block in
  `index.html` collects the WiFi SSID and passphrase, the companion password,
  the theme, TX power and, on MQTT builds, the broker URI, base topic and
  credentials. It encodes one `ProvisionTLV` blob and sends
  `provision base64:<blob>` over Web Serial. There is no second settings writer:
  the firmware `provision` console command stays the one validated path, and the
  page reports success only from that command's result lines.
- The panel needs the debug build. `FURBLE_CONSOLE` is what provides the
  console, and no release image contains it, so the panel hides its controls
  until the "Debug build" box is ticked instead of pretending to provision.
- Field limits, the tag numbers and the record order mirror
  `lib/furble/protocol/ProvisionTLV.h`. `MAX_COMMAND_CHARS = 255` mirrors
  `src/FurbleConsole.cpp`: `esp_console_run()` copies the line into a 256-byte
  buffer with `strlcpy()`, and the console refuses a longer line rather than
  clipping it. `buildCommand()` rejects an oversized or invalid bundle before
  the port is opened, and the error names the limit.
- A blank passphrase is sent as an empty `WIFI_PSK` record, which clears a saved
  passphrase for an open network. Anything between 1 and 7 characters is
  refused: WPA2-PSK and WPA3-SAE both need 8 to 63 characters, and
  `Settings::validNetworkString()` applies the same rule on the device. A blank
  optional field is not sent at all.
- The theme select offers exactly the closed set
  `Settings::validTheme()` accepts, `Dark`, `Default` and `Mono Furble`, and the
  encoder refuses anything else. TX power is 0, 1 or 2 (P3, P6, P9); an
  out-of-range value is refused instead of being stored and clamped to P3.
  `tests/test_web_installer.py` pins all three sets to the firmware source.
- MQTT settings are omitted on `m5stick-c`, `m5stick-c-plus` and
  `m5stack-core`, which do not compile the MQTT backend. `BOARD_HAS_MQTT` is
  checked against `platformio.ini` by `tests/test_web_installer.py`, so a new
  MQTT profile or a new board row has to update it.
- The response parser fails closed. A missing `furble> ` prompt, a timeout, an
  `error:` or `failed:` line, a decoded byte count that differs from the sent
  blob, a deferred field, or a summary that never arrives is a failed step. The
  console echo of the command is dropped, so no credential is ever rendered.
  The firmware verifies the SSID and passphrase writes with a read-back, so a
  refused NVS write becomes a `storage failure` error line instead of an
  applied field.
- The panel prefers a port already granted to the page and retries `port.open()`
  for about two seconds, because ESP Web Tools holds the port until its dialog
  closes. That reopen timing has not been verified on hardware; see the residual
  list in the plan.
- `tests/web-installer/provision-fixture.json` is one bundle both codecs encode.
  `tests/host/provision_tlv_test.cpp` checks the production codec against it and
  `tests/web-installer/provision.test.cjs` checks the browser codec against it.
  Run the browser test with `node tests/web-installer/provision.test.cjs`, or
  through `python3 -m unittest tests.test_web_installer`, which also pins the
  mirrored limits and wire ids to their firmware sources.
- The Pages deploy copies `provision.js` next to `index.html`, and
  `tools/verify_web_installer.py` fails when a local page script is missing from
  the site directory.

## Debug variant

- Each publishable board ships a debug build from the matching PlatformIO
  `*-debug` env. Debug builds carry `FURBLE_CONSOLE`, which adds the USB serial
  command console and Bluetooth diagnostic commands. M5Stack Core is excluded:
  its `m5stack-core-usb-debug` fallback is single-factory and developer-only.
- The debug variant is a value of `PLATFORM`, not a separate template. The
  release workflow sets `PLATFORM=<board>-debug`, and the `-debug` suffix flows
  straight into every binary path in `manifest.tmpl`. The chip family and flash
  offsets are shared with the release path, because debug envs share the release
  sdkconfig and its `CONFIG_IDF_TARGET`. `generate-manifest.py` needs no variant
  logic: the suffix is not part of any sdkconfig file name, and the longest
  matching base board wins.
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
