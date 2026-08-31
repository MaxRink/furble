# 114 - One-shot provisioning parser and console apply

Status: implementation slice for the firmware side of provisioning. This PR
does not implement WiFi association, browser WebSerial, or the staged browser
transport described by the broader flasher design.

## Scope

The firmware accepts one complete provisioning blob through the existing USB
console command:

```
provision <hex-or-base64 blob>
```

The blob is a bounded sequence of tagged records. Each record contains a tag,
value type, byte length, and value. Setting records carry the stable settings
wire id as the first value byte. Unknown optional records are ignored, unknown
required records fail, and a malformed or duplicate record rejects the whole
blob without changing the output bundle.

The parser accepts WiFi, MQTT, and companion-password fields. WiFi and MQTT
remain deferred until their respective backends land. The companion-password
field is applied to the firmware's write-only wire-id 46 setting. No secret is
printed by the console.

## Apply behavior

All setting records are preflighted before the first NVS write. Validation
includes the stable schema, type, length, boolean values, GPS baud, GPS duty,
GPS assistance, text size, feedback output, display mode, button mode, and the
interval wire representation. A failed preflight leaves existing settings
unchanged.

After each successful write the console invokes the same live-setting reload
behavior used by the companion path for GPS, feedback, TX power, companion
enablement, and companion-password rotation. Settings that are restart-only
remain persisted and are reported as applied to storage, not claimed to be
live. A dedicated companion-password field and a wire-id 46 setting cannot
both appear in one bundle.

## Deliberate transport boundary

This PR intentionally lands the one-shot parser and apply seam first. It does
not add `provision begin`, chunked `provision tlv`, `provision commit`,
`provision status`, or `provision abort`. Those commands are a later transport
slice for the browser flasher and must be designed around the same validated
bundle path rather than creating a second settings writer.

WiFi credential persistence and MQTT application remain blocked on the WiFi
and MQTT implementation. Companion-password persistence uses the existing
write-only NVS setting and is applied by this firmware slice. Browser WebSerial
and real-device provisioning remain hardware or browser acceptance work.

## Verification

- Host codec tests cover round trips, malformed and truncated records,
  duplicate fields, unknown required and optional records, bounds, text
  decoding, and output immutability on failure.
- Host apply tests use the production Settings and NVS path to verify complete
  preflight atomicity, domain validation, persisted values, and the runtime
  reload callback contract.
- Full host ctest and the repository simulator and firmware CI remain required
  before merge.

No hardware gate is required for this parser-only slice. The real browser
transport, WiFi association, MQTT broker, and companion secret flow remain
explicit residual tests.
