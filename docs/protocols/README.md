# Protocol reference

One page per wire protocol furble speaks, written from the implementation. Each
page states the frames, fields, keys, topics or commands the code sends and
parses, the byte layout where the code defines one, the timing and error
handling around it, and the rows that only a vendor or standards document can
close.

| Protocol | Direction | Role in furble | Page |
| :--- | :--- | :--- | :--- |
| UBX | furble to a u-blox M10 receiver | configuration and fix readout | [UBX](gnss-ubx.md) |
| CASIC and `$PCAS` | furble to an AT6668 receiver | configuration, standby, ephemeris cache | [CASIC and PCAS](gnss-casic-pcas.md) |
| NMEA 0183 | receiver to furble | fix, satellites, DOP | [NMEA](gnss-nmea.md) |
| Camera BLE, Sony, Nikon, Canon and the other vendors | furble to the camera as a central | advertisement matching, pairing, shutter and focus | [Camera remotes](camera-remotes.md) |
| IR remote | furble to the camera as an emitter | Nikon, Sony and Canon shutter | [IR remote](ir-remote.md) |
| MQTT and Home Assistant discovery | furble as a client, a broker and Home Assistant in the middle | state, commands, discovery records | [MQTT](mqtt.md) |
| Companion GATT | a phone or a script to furble as a peripheral | settings, cameras, trigger, GPS, auth, OTA | [Companion GATT](companion-gatt.md) |
| BLE HID over GATT | furble as an observer only | a scan filter constant, no client | [BLE HID](ble-hid.md) |
| Console command set | a host to furble over USB serial | diagnostics and control | [Console commands](../console-commands.md) |
| WebUI REST API | a browser to furble over HTTPS | state and control | [WebUI](../webui.md#api) |

## What "documented" means here

A protocol is documented when every field the code writes or reads has a stated
meaning, every constant has a citation, and the error paths are described. A
protocol is not documented by being named in a feature list. Where the only
source for a field meaning would be a vendor document that is not in the tree,
the row is listed as open with the document and section it needs, and no value is
invented.

The gap register that tracks this work is
[plan 332](../../plans/332-docs-gap-closure.md).

## Related research

Camera vendor research is kept separately in
[docs/camera-research](../camera-research/README.md), which holds per-model
firmware evidence, public source comparisons and capture targets. This directory
holds the wire contract that the firmware implements, not the research trail.
