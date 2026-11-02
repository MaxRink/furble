# Camera BT API surface: what the cameras expose

This note summarises the Bluetooth API surface of every camera family Furble
registers, including services, characteristics, commands, and features that
Furble does not currently use. It is a map, not a certification. The
implementation-level detail is in the per-family research notes and the pairing
and state-machine references.

Evidence labels: `firmware-static` from a shipped image, `app-static` from an
official application, `hardware-capture` from a real link, `public-source` from
a vendor document or public implementation, `inference`, and `unknown`.

A service or command being present in an image does not prove that a camera
exposes it in a given mode. A successful ATT write does not prove a physical
capture.

## Surface summary

Counts are rows in the corresponding research inventory. They include standard
GAP, GATT, and Device Information entries where an artifact names them.

| Family | Services | Characteristics | Commands | Notifications | Features |
| --- | ---: | ---: | ---: | ---: | ---: |
| Fujifilm Basic and Secure | 13 | 69, or 70 on Secure v4.1 | 78 | 21 | 45 |
| Ricoh GR III, IIIx, IV, HDF | 9 | 76 | 117 | 40 | 56 |
| Canon EOS Smart and Remote | 9 | 36 | 71 | 13 | 55 |
| Nikon Remote and Smart | 5 | 32 | 47 | 11 | 74 |
| Sony | 9 | 46 | 55 | 19 | 60 |
| Panasonic LUMIX | 13 | 75 | 58 | 20 | 45 |
| DJI Osmo | 5 | 17 | 22 | 12 | 59 |

## Fujifilm

The audited Secure images carry the same 70-entry UUID table, 13 services, 69
characteristics, and 21 CCCDs. X-E5 1.12 adds one write-only characteristic,
`8B5ECF55-FC6B-40D0-B4C1-76F64E5453C7`. X100T 1.11 is Wi-Fi only.

Furble uses 14 vendor characteristics for registration, shutter, geotag, and
optional state subscriptions. The shutter characteristic accepts only `0`
release, `1` half press, and `2` full press; Furble now uses `1` for focus and
`0` for release. The unused surface includes camera and lens identity, power
control and remote boot, the 7-byte and 12-byte time packets, timezone, backup,
firmware update, file transfer, image resize and IPTC settings, and most of the
60-entry characteristic table. The Basic token service `91F1DE68` is present
through the v3.1 generation and absent from v4.0 and v4.1.

Open questions: the exact NOT1 `01 00` meaning, the camera-side registration
gate for each unused characteristic, and the per-characteristic ATT security.

## Ricoh

The GR III and GR IV daemons share six custom services. GR III exposes 52
Ricoh characteristic classes, GR IV exposes 65, and the GR IIIx 1.41 recording
contains 51. The full inventory is 76 rows after standard services.

Furble uses model identity, operation mode, GEO Tag, GPS Information, shutter,
drive mode, and capture status. The unused surface includes camera power and
battery, date and time, storage and transfer lists, power-off during transfer,
exposure settings, self-timer, WLAN control, paired-device name, firmware
update, and the multiplexed Camera and Shooting notifications.

The handler map confirms that Camera Power writes accept only `0` or `1`;
`2` is a read-only Sleep state and is refused on write. GEO Tag accepts only
`0` or `1`. The GPS parser reads the first 31 bytes and never reads the final
datum byte. GR III uses passkey display; GR IV uses numeric comparison.

Open questions: GR IV storage field semantics, the 130-byte Battery Level
payload, the Operation Mode list encoding, and the exact release-mode meaning
of AF versus no-AF.

## Canon

Camera Connect confirms the Smart `00010000` family. Furble uses the pairing
name, identity, mode, shutter, focus, and GPS characteristics. The unused
surface includes power and capability reads, Wi-Fi handover, livestream,
shoot-state and button-state notifications, error categories, movie, zoom,
remote button frames, GPS request and source selection, and the eight-byte GPS
control commands.

The Smart shutter `00 01` and `00 02` are combined AF plus release. AF only is
`00 03` and `00 04`; plain release is `00 05` and `00 06`. Furble now uses the
AF-only pair for focus.

The Remote `00050000` profile is public-source only. Camera Connect contains no
Remote UUID strings.

Open questions: whether the camera accepts Furble's Write Command control
writes, whether identity must wait for the pair result, whether the GPS request
ack and source select are required, and the full model gate for Bluetooth GPS.

## Nikon

The B600 firmware contains the Remote `0x2080` to `0x2087` set and the Smart
`0x2000` to `0x200b` set. Furble uses the Remote handshake and shutter. The
unused surface includes Remote pair and status indications, the Smart
four-stage auth, client name, time, GPS, control point `2021`, feature bitmap,
and the Classic SPP and PTP continuation.

Remote shutter `0x2083` has a 2-byte gate; `{02 02}` is a short press,
`{02 03}` is a long press, and `{02 00}` is a no-op. `0x2084` is a six-byte
key/status indication, not a pairing result; pairing is `0x2087`. Smart crypto
is confirmed, including the key, eight salts, Blowfish CBC-MAC, and 17-byte
frames. `{01, 00}` on `2008` is a time request, not an auth acknowledgement.

Open questions: the exact Remote shutter bytes, the `2084` status payload, and
the Z6 III GATT inventory.

## Sony

Both official apps implement CC00, DD00, and EE00; Creators' App adds BB00.
Furble requires the FF00 remote-commander service and writes FF01 for shutter
and focus. It writes location with DD11 and conditionally enables it through
DD30 and DD31 on the first GPS update. DD01 (location status) and DD21
(payload selector) are unused: the driver always sends the 95-byte form and
does not read the selected length. The unused surface includes the CC00 status
and settings services, DD01 and DD21, EE01 pairing and EE03 state, EE04 camera
info, BB00 DiRxTx, FF02 status notifications, PTP/IP still capture, movie,
zoom, power, image transfer, and the location release sequence.

The location payload is 91 or 95 bytes, selected by `DD21` byte 4 bit 1. The
app writes timezone and DST into bytes 91 to 94, subscribes `DD01`, locks with
`DD30` and `DD31`, and unlocks on stop. Furble always sends 95 bytes with zero
offsets. It raises the DD30 allow flag and the DD31 enable flag when location
first turns on, but it has no release path: neither stop nor disconnect clears
them, and the app's unlock sequence is not implemented. FF02 emits `02 3f`,
`02 2d`, `02 a0`, and `02 d5`; Furble does not subscribe. EE01 pairing is confirmed but not sent by Furble.
The four FF01 opcodes remain unproven by the firmware.

Open questions: the FF01 opcode bytes, the exact FF02 action model, the SMP
method, and the ZV-1F BLE stack, whose payload is opaque.

## Panasonic LUMIX

The MEI0 generation uses the session service, a per-install identity seed, and
plain writes. The APS generation adds a key-read and XOR-encoded identity. The
full inventory is 13 services and 75 characteristic entries, including the
G9II handle-only dialect. The three UPD images expose container metadata and
plaintext partition digests, but the code partitions are AES-CTR-class
ciphertext. No LUMIX BLE byte is firmware-static; the app remains the primary
evidence.

Furble uses the MEI0 identity, control shutter, GPS, and clock. The session
Confirm `01`, Start `02`, and Finish `03` writes are sent on connect and
disconnect; they are never focus commands. The GPS stop packet `0x56` is sent on
disconnect and focus press and release are no-ops. The unused surface includes
movie, readiness notifications, APS identity, Wi-Fi start, image transfer, live
view, settings, and firmware update. Furble now uses a signed GPS altitude and
the app's clock offset and DST order. The APS path and the readiness gates
remain unimplemented.

Open questions: which bodies use APS, the exact wake state machine, and the
per-body MTU and write type.

## DJI Osmo

The R SDK uses FFF0, with FFF4 notifications and FFF5 writes. Mimo also knows
FFF3 and a separate DM OTA service. The full R SDK command set is 22 rows.

Furble uses the connection request and approval, record control, status
subscription, and status push. The unused surface includes version query, key
report, restart, GPS push, power mode, mode switch, new status push `1D06`, and
the WKP wake advertisement.

The device ID has two official encodings: the table and a checksum-valid frame
imply `0xFF330000`, while one demo source file uses `0x33FF0000`. Furble
accepts both the demo and table device-ID encodings.

Open questions: the real camera-reported device ID, whether the camera requires
SMP bonding, and the exact wake behavior.

## What this means for implementation

The safe additions are the ones backed by both a camera-side artifact and a
clear response path: the Canon AF-only pair, the Ricoh GEO Tag and datum
correction, and the DJI dual device-ID acceptance. The capture-gated additions
are documented in the per-family research notes and the two diagram references.
No unused surface should be promoted to supported hardware without a capture.
