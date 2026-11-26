# Companion GATT

The companion service is furble's own BLE peripheral: a phone or a script
connects to furble, reads and writes settings, lists cameras, triggers the
shutter, streams a location and authenticates with a shared password.

| Item | Value |
| :--- | :--- |
| Status | shipped. Compiled behind the companion setting, default off. |
| Service logic | `src/FurbleCompanionService.cpp`, `include/FurbleCompanionService.h` |
| NimBLE transport | `src/FurbleCompanion.cpp`, `include/FurbleCompanion.h` |
| Wire version | 2 (`include/FurbleCompanionService.h:54`) |
| Pairing window | 60 s (`include/FurbleCompanionService.h:61`) |
| Maximum bonds | 15 (`include/FurbleCompanion.h:45`) |
| Design | [plan 50](../../plans/50-companion-app-design.md), [plan 51](../../plans/51-app-feature-parity.md) |
| App notes | [Companion](../companion.md) |

## Service and characteristics

Service `b57f4f5e-087b-4740-b71d-8262cf26ebbc`
(`include/FurbleCompanion.h:28`).

| Characteristic | UUID | Properties | Maximum value | Character id | Citation |
| :--- | :--- | :--- | ---: | ---: | :--- |
| Location | `b57f4f5f-087b-4740-b71d-8262cf26ebbc` | write, write without response, encrypted write | 42 | 0x02 | `include/FurbleCompanion.h:29`, `src/FurbleCompanion.cpp:386-390` |
| Status | `b57f4f60-087b-4740-b71d-8262cf26ebbc` | read, notify, encrypted read | 20 | 0x03 | `include/FurbleCompanion.h:30`, `src/FurbleCompanion.cpp:392-395` |
| Settings | `b57f4f61-087b-4740-b71d-8262cf26ebbc` | write, indicate, encrypted write | 512 | 0x04 | `include/FurbleCompanion.h:31`, `src/FurbleCompanion.cpp:397-400` |
| Trigger | `b57f4f62-087b-4740-b71d-8262cf26ebbc` | write, encrypted write | 4 | 0x05 | `include/FurbleCompanion.h:32`, `src/FurbleCompanion.cpp:408-411` |
| Cameras | `b57f4f63-087b-4740-b71d-8262cf26ebbc` | write, indicate, notify, encrypted write | 256 | 0x06 | `include/FurbleCompanion.h:33`, `src/FurbleCompanion.cpp:414-419` |
| Capability | `b57f4f64-087b-4740-b71d-8262cf26ebbc` | read, encrypted read | 6 | none, read only | `include/FurbleCompanion.h:38`, `src/FurbleCompanion.cpp:402-406` |
| Auth | `b57f4f6f-087b-4740-b71d-8262cf26ebbc` | write, indicate, encrypted write | 18 | 0x07 | `include/FurbleCompanion.h:35-37`, `src/FurbleCompanion.cpp:420-423` |
| OTA control | `b57f4f6d-087b-4740-b71d-8262cf26ebbc` | reserved, not created | n/a | 0x10 | `include/FurbleCompanion.h:39`, `src/FurbleCompanion.cpp:425-426` |
| OTA data | `b57f4f6e-087b-4740-b71d-8262cf26ebbc` | reserved, not created | n/a | 0x11 | `include/FurbleCompanion.h:40`, `src/FurbleCompanion.cpp:425-426` |

## Capability record

6 bytes, read once after connect (`include/FurbleCompanionService.h:149-153`).

| Offset | Field | Value |
| ---: | :--- | :--- |
| 0 | `version` | `CAPABILITY_VERSION` = 1 |
| 1 | `wire_version` | `WIRE_VERSION` = 2 |
| 2 | `features` | bit 0 settings v2, bit 1 cameras |

The size is pinned by a `static_assert` (`include/FurbleCompanionService.h:181`).

## Status record

20 bytes, notified at most once per second and only when a field changed
(`include/FurbleCompanionService.h:131-147`, `src/FurbleCompanionService.cpp:435-475`).

| Offset | Field | Meaning |
| ---: | :--- | :--- |
| 0 | `version` | packet version 1 |
| 1 | `battery_percent` | |
| 2 | `battery_mv` | U2 |
| 4 | `battery_ma` | I2 |
| 6 | `power_flags` | |
| 7 | `camera_total` | |
| 8 | `camera_connected` | |
| 9 | `control_state` | |
| 10 | `gps_source` | |
| 11 | `gps_satellites` | |
| 12 | `ivl_state` | |
| 13 | `ivl_remaining` | U2 |
| 15 | `uptime_s` | U4 |
| 19 | `reserved_tail` | |

## Location record

42 bytes, written by the companion, little endian, packed
(`include/FurbleCompanionService.h:112-130`).

| Offset | Field | Meaning |
| ---: | :--- | :--- |
| 0 | `version` | 0 is rejected |
| 1 | `flags` | bit 0 location valid, bit 1 time valid, bit 2 altitude valid |
| 2 | `satellites` | |
| 3 | `accuracy_m` | |
| 4 | `latitude` | double |
| 12 | `longitude` | double |
| 20 | `altitude` | double |
| 28 | `year` | U2 |
| 30 | `month`, `day`, `hour`, `minute`, `second`, `centisecond` | |
| 36 | `reserved` | |
| 37 | `age_ms` | U4 |
| 41 | `reserved_tail` | |

A write shorter than offset 37 plus 4 bytes is rejected as a short location write
(`src/FurbleCompanionService.cpp:311-314`). Unknown flag bits are rejected
(`src/FurbleCompanionService.cpp:318-320`).

The location characteristic stays outside the password gate on purpose: the apps
stream fixes as soon as the link is ready and only authenticate before settings
and trigger, so a password gate here would drop fixes with no error the app can
surface (`src/FurbleCompanionService.cpp:305-310`).

## Settings protocol

Writes are `{op, wire_id, length, value[length]}`. Op 0 is list, op 1 is get and
op 2 is set (`src/FurbleCompanionService.cpp:1027-1090`).

| Op | Request | Response |
| ---: | :--- | :--- |
| 0 | `{0, 0, 0}` | one indication per setting, then a terminator record with id 255 |
| 1 | `{1, id, 0}` | one record with the current value |
| 2 | `{2, id, len, value}` | one record with the new value, or a status |

A response record is `{status, id, type, length, value[length]}` and, for list
records only, one trailing flags byte
(`src/FurbleCompanionService.cpp:1000-1019`).

| Field | Values | Citation |
| :--- | :--- | :--- |
| `status` | `SETTING_OK` 0, `SETTING_UNKNOWN_ID` 1, `SETTING_BAD_LENGTH` 2, `SETTING_READ_ONLY` 3, `SETTING_REJECTED` 4 | `include/FurbleCompanionService.h:246-252` |
| `type` | `SETTING_BOOL` 0, `SETTING_U8` 1, `SETTING_U32` 2, `SETTING_STRING` 3, `SETTING_BLOB` 4 | `include/FurbleCompanionService.h:167-173` |
| `flags` | bit 0 needs restart, bit 1 dangerous | `include/FurbleCompanionService.h:236-237` |

A malformed length, an op other than 0 to 2, or a non-zero length on op 0 or 1
produces `SETTING_BAD_LENGTH` (`src/FurbleCompanionService.cpp:1082-1090`). An
unknown id produces `SETTING_UNKNOWN_ID`
(`src/FurbleCompanionService.cpp:1123-1127`).

### Secret settings

`WIFI_PSK` (wire id 53) and `MQTT_PASS` (wire id 59) are write-only over this
link. A get or a list answers the word `set` or `unset` in the setting's own
`SETTING_STRING` type, the same answer the console prints, instead of the stored
credential (`src/FurbleCompanionService.cpp:139-155`). A client needs no new wire
type to render it, and it has nothing secret to echo back into an editor. Writes
are unchanged, so a client that holds the companion password can still set both,
and `COMPANION_PASSWORD` keeps its existing shape: it never appears in a list and
a get is rejected.

Command responses and list records are indicated, and unsolicited state events
are notified. That split is deliberate and is the same one settings versus status
uses (`src/FurbleCompanion.cpp:397-419`).

## Cameras protocol

A request is exactly two bytes, `{op, camera_id}`
(`include/FurbleCompanionService.h:241`, `src/FurbleCompanionService.cpp:479-489`).

| Op | Name | Effect |
| ---: | :--- | :--- |
| 0 | list | one indication per camera, then a terminator with id 255 |
| 1 | connect | start a connection to that camera |
| 2 | disconnect | drop that camera |
| 3 | select | add to the multi-connect set, requires the multi-connect setting |
| 4 | deselect | remove from the set |

Camera id 0 means not assigned and 255 means all cameras
(`lib/furble/protocol/CameraListProtocol.h:29-31`). Status values are
`CAMERA_OK` 0, `CAMERA_UNKNOWN_ID` 1, `CAMERA_REJECTED` 2 and `CAMERA_BUSY` 3
(`include/FurbleCompanionService.h:87-92`).

A camera record is an 8-byte fixed head followed by `name_len` bytes of name
(`include/FurbleCompanionService.h:155-165`, `src/FurbleCompanionService.cpp:417-423`).

| Offset | Field | Meaning |
| ---: | :--- | :--- |
| 0 | `status` | the status enum |
| 1 | `camera_id` | |
| 2 | `cam_type` | the vendor type |
| 3 | `flags` | bit 0 saved, bit 1 selected, bit 2 target, bit 3 connected |
| 4 | `progress` | 0 to 100 |
| 5 | `rssi` | -128 means unknown |
| 6 | `state` | 0 idle, 1 connecting, 2 connected, 3 reconnecting, 4 lost, 5 disconnecting |
| 7 | `name_len` | at most 64 |
| 8 | `name` | UTF-8, not NUL terminated |

Names are capped at 64 bytes so one record fits one indication
(`include/FurbleCompanionService.h:243-246`). State notifications are rate
limited to one per second and only carry records whose state changed
(`include/FurbleCompanionService.h:244`, `src/FurbleCompanionService.cpp:435-475`).

## Trigger protocol

`{nonzero, op}` or, for op 4, `{nonzero, 4, hold_ms U2}`
(`src/FurbleCompanionService.cpp:1226-1290`).

| Op | Action |
| ---: | :--- |
| 0 | shutter release |
| 1 | shutter press, ignored while already held |
| 2 | focus press |
| 3 | focus release |
| 4 | press, then release after `hold_ms`, or immediately when 0 |

A trigger is only accepted on an active control session
(`src/FurbleCompanionService.cpp:1240-1243`).

## Authentication

A shared password, at most 63 bytes, HMAC-based challenge and response
(`include/FurbleCompanionAuth.h:14-18`).

| Step | Payload | Direction |
| :--- | :--- | :--- |
| Begin | `{0x01, 0x00}` | written by the client |
| Challenge | `{0x01, 0x00, nonce[16]}` | indicated to the client |
| Proof | `{0x01, 0x01, response[16]}` | written by the client |
| Result | `{0x01, 0x02, result}` | indicated to the client |

Nonce is 16 bytes, the HMAC is SHA-256 with a 32-byte digest, and the response is
the truncated 16-byte digest (`include/FurbleCompanionAuth.h:14-16`).

The four packets and their sizes are `AUTH_VERSION` 0x01, `AUTH_OP_BEGIN` 0x00,
`AUTH_OP_PROOF` 0x01 and `AUTH_OP_RESULT` 0x02, with a challenge of two plus
sixteen bytes, a proof of two plus sixteen and a result of three
(`include/FurbleCompanionService.h:62-68`). The exchange runs in
`handleAuth()` (`src/FurbleCompanionService.cpp:627-684`).

| Result value | Meaning |
| ---: | :--- |
| 0x01 | authenticated |
| 0x02 | rejected |
| 0x03 | dropped, and the transport disconnects |
| 0x04 | not required, no password is set |

Three failures drop the link (`include/FurbleCompanionAuth.h:18`).

An unknown version, unknown op or wrong length consumes the outstanding
challenge as a failed proof, so a malformed retry cannot reuse one nonce
(`src/FurbleCompanionService.cpp:630-644`).

## Advertising

| Item | Value | Citation |
| :--- | :--- | :--- |
| Fast interval | 160, which is 100 ms in 0.625 ms units | `include/FurbleCompanionAdvertising.h:14` |
| Slow interval | 1636, which is 1022.5 ms, Apple's recommended fallback step | `include/FurbleCompanionAdvertising.h:16` |
| Fast window | 60000 ms after enable, boot, disconnect or a physical press | `include/FurbleCompanionAdvertising.h:18` |
| Bonded-host reconnect | advertises until the bonded host returns, which can be hours | `include/FurbleCompanionAdvertising.h:21-28` |

## Provisioning TLV

The console `provision` command and the app share one blob format. A batch is a
sequence of records (`lib/furble/protocol/ProvisionTLV.h:12-23`). The wire
format allows a 0 to 63 byte `WIFI_PSK`; the apply step then enforces the WPA2
and WPA3-SAE rule, so an empty value (open network) or 8 to 63 bytes is
accepted and 1 to 7 bytes is refused before any write.

| Offset | Size | Field |
| ---: | ---: | :--- |
| 0 | 1 | tag, low seven bits; bit 7 means required |
| 1 | 1 | value type, one of the five setting types |
| 2 | 1 | length |
| 3 | n | value |

| Tag | Name | Value layout |
| ---: | :--- | :--- |
| 0x01 | `WIFI_SSID` | UTF-8, at most 32 bytes |
| 0x02 | `WIFI_PSK` | empty (open network) or 8 to 63 bytes |
| 0x03 | `SETTING` | first byte is the stable wire id, then the value |
| 0x04 | `COMPANION_PASSWORD` | at most 63 bytes, wire id 47 |
| 0x05 | `MQTT_URI` | at most 255 bytes |
| 0x06 | `MQTT_USERNAME` | at most 255 bytes |
| 0x07 | `MQTT_PASSWORD` | at most 255 bytes |
| 0x08 | `MQTT_BASE_TOPIC` | at most 255 bytes |

Limits: 4096 bytes total, 64 records, 63 bytes per string
(`lib/furble/protocol/ProvisionTLV.h:25-33`). Error codes are `NONE`,
`NULL_INPUT`, `TRUNCATED`, `MALFORMED`, `UNKNOWN_REQUIRED_FIELD`,
`UNKNOWN_SETTING_ID`, `DUPLICATE_FIELD`, `BAD_TYPE`, `BAD_LENGTH`, `OVER_LENGTH`
and `INVALID_TEXT` (`lib/furble/protocol/ProvisionTLV.h:56-68`).

The console accepts hex or base64, with an explicit `hex:` or `base64:` prefix
allowed. Without a prefix an even all-hex string is read as hex and anything else
as padded or unpadded standard base64
(`lib/furble/protocol/ProvisionTLV.h:104-112`,
`docs/console-commands.md#settings`).

The web installer builds the same batches in `web-installer/provision.js` and
sends one `provision base64:` line over Web Serial. The console line limit of 255
characters caps one bundle at about 178 bytes, and both ends refuse the overflow
rather than clipping it. The browser and firmware codecs are pinned to one
shared fixture, `tests/web-installer/provision-fixture.json`.

## Camera list blob

The saved camera index is a versioned binary blob, not a TLV
(`lib/furble/protocol/CameraListProtocol.h:9-31`).

| Item | Value |
| :--- | :--- |
| v2 header | `0xFF 'C' 'L' 0x02` |
| v1 record | 16-byte name, then a U4 type, 20 bytes |
| v2 record | the v1 layout plus a 1-byte camera id, 21 bytes |
| Id 0 | not yet assigned |
| Id 255 | reserved on the companion wire to mean all cameras |
| Rotating address vendor | type 8, Fujifilm Secure, the only vendor whose advertised address changes between pairings |

A v1 blob is a bare array of records whose first byte opens an uppercase hex
address key, so `0xFF` can never start one, which is why the marker makes the
migration unambiguous (`lib/furble/protocol/CameraListProtocol.h:17-24`).

## Open rows

| Missing | Why the repository cannot close it | Document and section needed |
| :--- | :--- | :--- |
| Bluetooth Core requirements for the encrypted-write properties | The code sets them through NimBLE | Bluetooth Core Specification, GATT characteristic properties and security requirements |
| Companion app behaviour per platform | The apps are in `companion/`, and their platform notes are separate | Android and Apple platform BLE documentation for the connection interval and MTU the apps request |
| OTA characteristic protocol | OTA over the companion is reserved and not created | Plan for OTA over the companion, plus the app side of the transfer |
| Settings catalog per wire id | Ids live in `SETTING_SCHEMAS` and the reservation table, not in this page | [Settings and controls](../settings-and-controls.md) and `include/CLAUDE.md` wire id table |
