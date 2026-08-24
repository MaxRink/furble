# 114b - Web flasher WiFi and settings provisioning

Status: design only. The firmware parser and one-shot console command from
`plans/114-provision-parser.md` landed in PR #165. This document is the
follow-up browser transport and WiFi application design. It extends
`plans/33-wifi-hub.md` PR33b (WiFi settings) and the web installer at
`web-installer/`.

**Split delivery.** The browser config step and real serial WebSerial handshake
are Claude / hardware work. The existing parser remains the single validated
settings path. Do not add a second staged settings writer without a separate
design and tests.

## Motivation

A headless studio node (`plans/33`, `plans/42`) has no screen to type an SSID
on, and the maintainer's stated order puts network config on the console. But an
end user flashing over the web installer never opens a serial console. Today the
flasher writes a binary and stops; the user then has to attach a terminal and
type `wifi set ssid ...`. That is the friction that keeps board-only installs in
the hands of developers only.

Give the web installer a step that, right after flashing, sends one complete
provisioning blob through the existing `provision <hex-or-base64-blob>` console
command over the same serial link the flasher already holds open. The device
boots once, is provisioned, and joins the network with no terminal.

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

Out of scope:

- Improv serial. Rejected above.
- Any credential display or echo. The provisioning response reports a field as
  set, never its value, same rule as PR27/PR33b.
- Multiple WiFi networks. One network, as PR33b.

## Files to change

- `web-installer/index.html` and `web-installer/CLAUDE.md`: the post-flash config
  step and its documentation.
- `plans/114-provision-parser.md`: the firmware command and validation contract.

## Settings and defaults

No new settings of its own. It writes existing settings. The `wire_id` column is
additive and defaults to 0 (not on the wire) for every existing setting until
each is explicitly assigned an id, so behavior is unchanged until a field is
provisioned.

## Dependencies

- `plans/27-usb-console.md`: the console is the transport. Hard dependency,
  landed.
- `plans/114-provision-parser.md` / PR #165: the parser, field limits, and
  atomic apply path. Hard dependency, landed.
- `plans/33-wifi-hub.md` PR33b: provides `WIFI`, `WIFI_SSID`, `WIFI_PSK`. The
  WiFi part of provisioning is **blocked** on 33b. The generic TLV parser and
  settings-write path are **not** blocked and can land first, writing any
  existing setting.
- `plans/50-companion-app-design.md`: shares the `wire_id` table and TLV format.
  Coordinate so both use one encoder.
- `plans/116-companion-password.md`: the companion password is one of the fields
  the flasher sets, so 114 is the delivery mechanism for 116's secret.

## Risks

- **The parser contract must not drift.** The browser encoder must use the
  bounded one-shot command and the production field limits. It must reject an
  oversized payload before writing to the serial port.
- **Hex/base64 over a line console.** Keep the browser payload within the
  command limit and reject overflow with a clear error rather than truncating.
- **WebSerial reopen race.** ESP Web Tools holds the port during install; the
  config step must wait for it to release before reopening. This is a browser
  timing bug waiting to happen and is exactly what the residual browser test
  covers.
- **Plaintext creds in NVS.** Same trust level as PR33b and the BLE bonding
  keys. State it; do not add NVS encryption here.

## Codex self-verification (headless, no hardware)

PR #165 already provides `provision_tlv_test` and `provision_apply_test`. The
browser follow-up adds a small host test for its encoder and response handling:

- The browser emits the same bytes as the production codec for a multi-field
  bundle.
- An error response or a timeout is surfaced as a failed browser step.

Run it:

```
cmake -S tests/host -B build/host-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tests --parallel 2
ctest --test-dir build/host-tests -R 'provision-(tlv|apply)' --output-on-failure
```

Round-trip the console command in the sim: add a `provision` scenario driving
the one-shot command through the console shim and asserting the target settings
changed via the existing settings queries.

```
SDL_VIDEODRIVER=dummy sim/build/furble-sim \
  --script sim/scenarios/e2e/provision-roundtrip.txt
```

Exit 0 from both proves the firmware side headless. No radio, no browser.

## Residual (Claude / hardware) verification

- The real browser flow: flash a device via the actual ESP Web Tools page in a
  Chromium browser, complete the config form, and confirm the port reopen and
  the `provision` handshake land the settings on the device (read them back over
  the console).
- If PR33b has landed: provision real WiFi creds this way and confirm the device
  associates on the next boot with no terminal.
- Confirm the WebSerial reopen does not race the install on slow machines.
