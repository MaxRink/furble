# BLE debug status (bench image)

`m5stick-s3-debug-ble` adds a read-only, unencrypted BLE status service for
bench diagnosis. It is not part of the production companion contract, and no
release image contains it.

The service is useful when the USB CDC endpoint is dead or the host has a stale
GATT handle map for the encrypted companion service. It answers without a
companion password and without the companion authentication exchange.

## Enable it

Build and flash the bench environment:

```sh
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug-ble -t upload
```

The image is gated on `FURBLE_BLE_DEBUG`. Enable Companion mode so the shared
NimBLE server exists. The status service is created during GATT construction and
the snapshot is refreshed from the companion service task, never while
`CompanionGatt` holds its own lock.

## Service and characteristic

- service `b57f4f70-087b-4740-b71d-8262cf26ebbc`
- status `b57f4f72-087b-4740-b71d-8262cf26ebbc` (read)

The status text reports:

- firmware version, reset reason and its numeric value;
- uptime in milliseconds and the FreeRTOS task count;
- internal and PSRAM free and largest blocks;
- bond count and live BLE connection count;
- companion enabled, connected, encrypted, authenticated, bond, pairing-window
  and advertising state.

The snapshot is a point-in-time read. It is not a live log stream, and it does
not expose or modify the encrypted companion link.

## Read it

Install `bleak` on the host, then query the device:

```sh
python3 -m pip install bleak
tools/ble_debug.py --device furble-38a70 --status
tools/ble_debug.py --device furble-38a70 --status --interval 5
```

`--device` accepts an advertised name substring or a BLE address. `--interval`
repeats the read, which is useful while enabling MQTT or watching heap during a
connection.

## Recovering a stale host GATT cache

The bench service changes the GATT layout. A host that cached the old handle
map can keep writing to handles that no longer exist. The firmware does not send
a Service Changed indication during authentication because CoreBluetooth then
rediscovers services while the auth exchange is in flight and can drop the auth
write or the first status notification.

Use `companion reidentify` on the debug console instead. It disconnects the
current companion link, drops only the companion bond, applies and persists a
new random-static LE address under `companion_bleid`, and reopens the 60 second
pairing window. The address is restored before advertising on later boots, so
the host keeps the bond it creates after the rotation. Camera bonds are
untouched. The host then sees a new peripheral identity and builds a fresh GATT
cache. See [Console Commands](Console-Commands) for the command details and
[Companion camera management](../companion.md) for the full companion
contract.

## Related pages

- [Console Commands](Console-Commands)
- [WebUI](WebUI)
- [Supported Hardware](Supported-Hardware)
