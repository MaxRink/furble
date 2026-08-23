# 114 - Web flasher WiFi and settings provisioning

Status: design only. Firmware side is a console/serial provisioning path.
Browser side is an ESP Web Tools config step. Extends
`plans/33-wifi-hub.md` PR33b (WiFi settings) and the web installer at
`web-installer/`.

**Split delivery.** The firmware provisioning parser and console command are
**Codex-implementable** and host-testable now against the existing `Settings`
table. The WiFi credential fields are **blocked on plan 33b** landing the
`WIFI_SSID`/`WIFI_PSK` settings. The browser config step and real serial Improv
handshake are **Claude / hardware** (a real browser over WebSerial).

## Motivation

A headless studio node (`plans/33`, `plans/42`) has no screen to type an SSID
on, and the maintainer's stated order puts network config on the console. But an
end user flashing over the web installer never opens a serial console. Today the
flasher writes a binary and stops; the user then has to attach a terminal and
type `wifi set ssid ...`. That is the friction that keeps board-only installs in
the hands of developers only.

Give the web installer a step that, right after flashing, pushes WiFi
credentials and a settings blob into NVS over the same serial link the flasher
already holds open. The device boots once, is provisioned, and joins the network
with no terminal.

## Protocol choice: batch `provision` console command, not Improv serial

Two candidates were evaluated.

- **Improv Wi-Fi serial** (`improv-wifi`, supported by ESP Web Tools). A
  standard packet protocol over the same USB serial. Pro: ESP Web Tools has a
  built-in Improv UI, so the browser side is nearly free. Con: Improv only
  carries SSID and PSK. It has no slot for a furble settings blob (theme, TX
  power, MQTT URI, base topic, companion password). It also needs an Improv
  service state machine resident in the firmware even on battery builds.
- **A batch `provision` console command** over the existing PR27 console. One
  line: `provision <base64-blob>`, where the blob is a length-prefixed TLV of
  `wire_id -> value` pairs reusing the companion settings TLV wire ids from
  `plans/50-companion-app-design.md` section 3.5. Pro: one code path for phone,
  console and flasher; carries WiFi creds AND any settings; no second parser.
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

- A `provision` console command group in the PR27 command table:
  - `provision begin` clears a staging buffer.
  - `provision tlv <base64>` appends one or more TLV records to staging.
  - `provision commit` validates every record, writes them through the same
    `Settings` path the menu uses, and prints one status line per field.
  - `provision status` prints how many fields are staged and the last result.
  - `provision abort` drops the staging buffer.
- The TLV format is exactly `plans/50` section 3.5's `id/type/len/value`, keyed
  on the stable `wire_id`, so the phone app, the console and the flasher share
  one encoder and one validator.
- A settings allow-list for provisioning: only fields with a non-zero `wire_id`
  and not marked device-local (e.g. `TOUCH_CALIBRATION`) are writable.
- Web side: a config step in `web-installer/index.html` that, after ESP Web
  Tools reports install complete, reopens the serial port, sends
  `provision begin`, one `provision tlv` per field from a small form (SSID, PSK,
  optional MQTT URI/user/base topic, optional companion password), then
  `provision commit`, and shows the per-field results.

Out of scope:

- Improv serial. Rejected above.
- Any credential display or echo. `provision commit` prints `wifi_psk: set`,
  never the value, same rule as PR27/PR33b.
- Multiple WiFi networks. One network, as PR33b.

## Files to change

- New `src/FurbleProvision.cpp` / `include/FurbleProvision.h`: the staging
  buffer, TLV decode, allow-list check, and the write-through call. Add to
  `furble_sources` and to `sim/build.sh` + `sim/CMakeLists.txt` source lists.
- The PR27 console command table: register the `provision` group.
- `include/FurbleSettings.h` / `src/FurbleSettings.cpp`: the `wire_id` column
  (shared with `plans/50`; if 50 has not added it, this PR adds it). WiFi
  credential wire ids depend on PR33b's `WIFI_SSID`/`WIFI_PSK` existing.
- `web-installer/index.html` and `web-installer/CLAUDE.md`: the post-flash config
  step and its documentation.

## Settings and defaults

No new settings of its own. It writes existing settings. The `wire_id` column is
additive and defaults to 0 (not on the wire) for every existing setting until
each is explicitly assigned an id, so behavior is unchanged until a field is
provisioned.

## Dependencies

- `plans/27-usb-console.md`: the console is the transport. Hard dependency,
  landed.
- `plans/33-wifi-hub.md` PR33b: provides `WIFI`, `WIFI_SSID`, `WIFI_PSK`. The
  WiFi part of provisioning is **blocked** on 33b. The generic TLV parser and
  settings-write path are **not** blocked and can land first, writing any
  existing setting.
- `plans/50-companion-app-design.md`: shares the `wire_id` table and TLV format.
  Coordinate so both use one encoder.
- `plans/116-companion-password.md`: the companion password is one of the fields
  the flasher sets, so 114 is the delivery mechanism for 116's secret.

## Risks

- **A bad TLV must never half-write.** `provision commit` validates every record
  (known id, correct type, length in range) before writing any, and writes
  atomically field by field with a per-field result. Test the boundary lengths
  (0, 1, 63, 64 for strings) exactly as PR33b tests its `std::string`
  specialisation.
- **Base64 over a line console.** Long blobs split across `provision tlv` lines.
  Cap each line and the total staging buffer; reject overflow with a clear error
  rather than truncating.
- **WebSerial reopen race.** ESP Web Tools holds the port during install; the
  config step must wait for it to release before reopening. This is a browser
  timing bug waiting to happen and is exactly what the residual browser test
  covers.
- **Plaintext creds in NVS.** Same trust level as PR33b and the BLE bonding
  keys. State it; do not add NVS encryption here.

## Codex self-verification (headless, no hardware)

The parser, validator and settings write-through are pure host logic. Add a host
test under `tests/host` and register it in `tests/camera` ctest, mirroring the
existing `settings_table_test`.

New test `provision_tlv_test` asserts:

- A valid multi-field TLV blob commits and every targeted `Settings` key reads
  back the written value.
- An unknown `wire_id` yields status "unknown id" and writes nothing.
- A wrong-length string field (64 bytes for a 63-cap key) is rejected, and no
  partial write lands.
- A device-local field (`wire_id == 0`, e.g. touch calibration) is refused.
- `provision commit` on an empty buffer is a no-op with a clear status.

Run it:

```
cmake -S tests/camera -B build/camera-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-tests --parallel 2
ctest --test-dir build/camera-tests -R provision-tlv --output-on-failure
```

Round-trip the console command in the sim: add a `provision` scenario driving
the staged commands through the console shim and asserting the target settings
changed via existing `ui.*`/settings queries.

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
