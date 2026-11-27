# 114b - Web flasher WiFi and settings provisioning

Implementation state (2026-11-26): the browser config step, its encoder and its
response parser are implemented in `web-installer/`, and the firmware receive
path now refuses an oversized line instead of clipping it. The hardware
acceptance list at the end is outstanding and is not claimed here. The firmware
parser and one-shot console command from
`plans/implemented/114-provision-parser.md` landed in PR #165 and remain the only
settings writer for this feature.

Status: implemented host-side. Browser serial timing, WiFi association and the
MQTT broker hand-off still need a device.

**Split delivery.** The browser config step and the serial handshake are browser
and hardware work. The existing parser remains the single validated settings
path; no second staged settings writer was added. The browser never writes a
setting itself and never claims success from a completed write.

## What landed

- `web-installer/provision.js` encodes one `ProvisionTLV` bundle from the form
  fields, builds the single console line `provision base64:<blob>`, and parses
  the command's documented result lines. It refuses an out-of-range field or an
  oversized bundle before the port is opened.
- `web-installer/index.html` carries a "WiFi and settings provisioning" panel
  that collects the WiFi SSID and passphrase, the companion password, the theme,
  TX power and, on MQTT builds, the broker URI, base topic and credentials. It
  opens the port, waits for the `furble> ` prompt, sends one command, and closes
  the port again. Success is reported only from the documented summary line with
  a decoded byte count that matches what was sent.
- `src/FurbleConsole.cpp` refuses a console line longer than 255 characters with
  `error: line longer than 255 bytes, command not run`. Before this, the line
  editor accepted 256 characters and `esp_console_run()` copied them with
  `strlcpy()` into a 256-byte buffer, so a longer `provision` line was silently
  clipped and a clipped base64 blob can still decode into a shorter valid
  bundle. That was the one firmware gap the browser transport needed closed.
- `tests/web-installer/provision-fixture.json` is one multi-field bundle that
  both codecs encode. `tests/host/provision_tlv_test.cpp` checks the production
  codec against it, `tests/web-installer/provision.test.cjs` checks the browser
  codec against it, and `tests/test_web_installer.py` runs the browser test and
  pins the mirrored limits, wire ids and board capability map to their firmware
  sources.
- `tests/host/console_commands_test.cpp` drives the `provision` command through
  the serial transport, not a direct dispatch, and covers an applied bundle plus
  oversized input, an over-long SSID, an over-long passphrase, a truncated frame
  and a repeated frame.

## Review round 1

The independent review is recorded in
[537-review-feedback.md](../537-review-feedback.md). Its blocking item was a
cross-PR interaction with plan 372's validator fixture, fixed in
`tests/test_web_installer.py`. The three domain gaps it found are fixed in both
halves:

- A passphrase of 1 to 7 characters is refused by
  `Settings::validNetworkString()` and by the browser encoder. Empty stays valid
  for an open network, and the 8 to 63 range covers WPA2-PSK and WPA3-SAE.
- The theme is validated through `Settings::validTheme()` on the provisioning
  path and on `settings set theme`, so the closed set has one definition.
- TX power is bounded to 0, 1 and 2 on the provisioning path instead of being
  stored and clamped to P3.
- The SSID and passphrase writes are read back because `Settings::save()` is
  `void`. A refused write logs at error level and fails the command with
  `storage failure` naming the wire id, and it is never counted as applied.

## Deviations from the design

- **The step requires the debug build.** The design names the PR27 console as
  the transport, and no release image contains that console. The panel therefore
  hides its controls until the "Debug build" box is ticked, and the docs say so.
  This is a real product limit of the chosen protocol, not an implementation
  shortcut: provisioning a release image needs either a console in release
  builds or a different transport.
- **The console line is 255 characters, so one bundle is about 178 bytes.** The
  design asked for a payload within the command limit with a clear overflow
  error. The limit turned out to be the line editor and `esp_console_run()`, so
  the firmware now refuses the line and the browser refuses the bundle. A
  typical SSID, passphrase, companion password, theme and TX power bundle fits;
  maximum-length fields in every slot do not. The browser also compares the
  decoded byte count the device reports with what it sent, so even a device that
  somehow applied a shorter prefix cannot be reported as success.
- **MQTT fields use the dedicated tags, not setting ids 57 to 60.** The firmware
  accepts both. Sending one form only keeps each setting written once.
- **The panel is manual, not automatic.** It does not send anything when ESP Web
  Tools reports the install finished; it only updates the status line. The user
  closes the installer dialog and clicks one button, which is also the retry
  path. Automatic sending would fight the port the installer holds, and that
  race cannot be verified without a device.
- **A blank passphrase is sent as an empty `WIFI_PSK` record.** The field is
  always sent with the SSID, so an open network clears a saved passphrase rather
  than silently keeping a stale one.

## Protocol choice: batch `provision` console command, not Improv serial

Two candidates were evaluated.

- **Improv Wi-Fi serial** (`improv-wifi`, supported by ESP Web Tools). A
  standard packet protocol over the same USB serial. Pro: ESP Web Tools has a
  built-in Improv UI, so the browser side is nearly free. Con: Improv only
  carries SSID and PSK. It has no slot for a furble settings blob (theme, TX
  power, MQTT URI, base topic, companion password). It also needs an Improv
  service state machine resident in the firmware even on battery builds.
- **A batch `provision` console command** over the existing PR27 console. One
  line: `provision <hex-or-base64-blob>`, where the blob uses the production
  tagged-record codec in `lib/furble/protocol/ProvisionTLV.h` and setting
  records carry the stable `wire_id`. Pro: one validated firmware path for the
  console and flasher; no second parser.
  Con: the browser has to speak the console line protocol over WebSerial itself
  rather than using the built-in Improv widget.

**Recommendation: the batch `provision` command.** furble already commits to the
console as the configuration surface (plan 33), already needs a stable settings
wire-id table for the companion app (plan 50), and a single TLV path is less
code and less attack surface than a second Improv state machine. Improv's
SSID-only payload cannot carry the settings blob the flasher wants to set, which
is the whole point. Keep Improv explicitly rejected in the PR body with this
reasoning, mirroring plan 33's "Considered and rejected: Improv over serial"
note, and revisit only if a non-console flashing surface ever needs it.

## Scope

In scope:

- A config step in `web-installer/index.html` that, after ESP Web Tools reports
  install complete, reopens the serial port, sends one bounded
  `provision <hex-or-base64-blob>` command, and shows the parser result.
- A browser-side encoder that uses the exact record format and field limits in
  `lib/furble/protocol/ProvisionTLV.h`. It must omit settings whose backend is
  not present yet, rather than claiming that they were applied.
- A serial response parser that treats a timeout, malformed response, or any
  field error as a failed provisioning step and never reports success solely
  because the write completed.
- Documentation of the browser flow, including how the user retries without
  erasing NVS.

The firmware apply path also accepts the dedicated MQTT URI, credentials, and
base-topic fields through the same checked settings validator and writer. Their
stable setting IDs are 57 through 60; the companion password remains owned by
the checked authentication writer.

Out of scope:

- Improv serial. Rejected above.
- Any credential display or echo. The provisioning response reports a field as
  set, never its value, same rule as PR27/PR33b.
- Multiple WiFi networks. One network, as PR33b.

## Files changed

- `web-installer/provision.js`, `web-installer/index.html` and
  `web-installer/CLAUDE.md`: the post-flash config step, its encoder, its
  response parser and its documentation.
- `src/FurbleConsole.cpp`: the fail-closed line limit that the browser bound
  depends on.
- `tests/host/provision_tlv_test.cpp`, `tests/host/console_commands_test.cpp`,
  `tests/host/CMakeLists.txt`: the shared fixture check and the receive path.
- `tests/web-installer/`, `tests/test_web_installer.py`,
  `tests/test_tooling_versions.py`: the browser codec and parser tests and the
  Pages asset wiring.
- `tools/verify_web_installer.py`, `.github/workflows/pages.yml`: ship
  `provision.js` and fail the deploy when a page script is missing.
- `docs/console-commands.md`, `docs/wiki/Console-Commands.md`, `README.md`,
  `docs/wiki/Getting-Started.md`, `docs/protocols/companion-gatt.md`,
  `docs/settings-and-controls.md`: the line limit, the browser step and the
  retry without erasing NVS.
- `plans/implemented/114-provision-parser.md`: the firmware command and
  validation contract.

## Settings and defaults

No new settings of its own. It writes existing settings. The `wire_id` column is
additive and defaults to 0 (not on the wire) for every existing setting until
each is explicitly assigned an id, so behavior is unchanged until a field is
provisioned. The browser writes theme (3) and TX power (4) as setting records
and uses the dedicated tags for WiFi (0x01, 0x02), the companion password
(0x04) and MQTT (0x05 to 0x08).

## Dependencies

- `plans/implemented/27-usb-console.md`: the console is the transport. Hard
  dependency, landed.
- `plans/implemented/114-provision-parser.md` / PR #165: the parser, field
  limits, and atomic apply path. Hard dependency, landed.
- `plans/33-wifi-hub.md` PR33b: provides `WIFI`, `WIFI_SSID`, `WIFI_PSK`. Merged
  as #53 with wire ids 51 to 53, so the WiFi part of provisioning is no longer
  blocked.
- `plans/50-companion-app-design.md`: shares the `wire_id` table and TLV format.
  One encoder per side, one shared fixture.
- `plans/implemented/116-companion-password.md`: the companion password is one of
  the fields the flasher sets, so 114 is the delivery mechanism for 116's secret.

## Risks

- **The parser contract must not drift.** The browser encoder uses the bounded
  one-shot command and the production field limits, and both codecs are pinned
  to one committed fixture. It rejects an oversized payload before writing to
  the serial port.
- **Hex/base64 over a line console.** The browser payload stays within the
  255-character command limit and overflow is a clear error, never a truncation.
  The firmware refuses an oversized line for the same reason.
- **WebSerial reopen race.** ESP Web Tools holds the port during install; the
  config step reopens it after the dialog closes and retries `port.open()` for
  about two seconds. The real timing is unverified without a device.
- **Plaintext creds in NVS.** Same trust level as PR33b and the BLE bonding
  keys. The console echoes the typed command line on the serial port, so a
  credential in the blob is visible to whoever holds the cable, exactly as
  `wifi set psk` already is. No NVS encryption is added here.

## Verification (host, no hardware)

```
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests --parallel 2
ctest --test-dir build/host-tests -R 'provision-(tlv|apply)|console-commands' --output-on-failure
python3 -m unittest tests.test_web_installer tests.test_tooling_versions
node tests/web-installer/provision.test.cjs
```

The first command chain covers the codec, the atomic apply path, the shared
fixture and the receive-path rejections. The second and third cover the browser
encoder, the response parser, the fake-port session, and the mirrored limits,
wire ids and board capability map. `tests/host/gesture_detector_test.cpp` is
assert-based, so configure the host build without `CMAKE_BUILD_TYPE=Release`;
`-DCMAKE_BUILD_TYPE=Release` defines `NDEBUG` and that file fails on unused
variables.

No radio and no browser are needed. The whole browser half runs under Node.

## Residual (hardware) verification

- The real browser flow: flash a device through the actual ESP Web Tools page in
  a Chromium browser, complete the config form, and confirm the port reopen and
  the `provision` handshake land the settings on the device (read them back over
  the console).
- Confirm where the port becomes available after the install dialog closes, and
  that the two-second `port.open()` retry covers slow machines.
- Provision real WiFi credentials this way and confirm the device associates on
  the next boot with no terminal.
- Provision an MQTT URI, base topic and credentials on an 8 MB board and confirm
  the broker connection and Home Assistant discovery.
- Confirm the companion password set this way authenticates, and that the panel
  renders no credential value.
