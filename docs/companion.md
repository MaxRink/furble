# Companion camera management

The companion BLE service exposes camera management through the Cameras
characteristic:

`b57f4f63-087b-4740-b71d-8262cf26ebbc`

It supports write, indicate, and notify. Write requests are two bytes:

```
uint8 op        // 0 list, 1 connect, 2 disconnect, 3 select, 4 deselect
uint8 camera_id // 0xff means all; ignored by list and disconnect
```

List records and command responses carry this fixed header followed by the
camera name:

```
uint8 status     // 0 ok, 1 unknown ID, 2 rejected, 3 busy
uint8 camera_id
uint8 camera_type
uint8 flags      // bit 0 saved, bit 1 selected, bit 2 active target, bit 3 connected
uint8 progress   // 0 to 100
int8  rssi       // -128 when unknown
uint8 state      // 0 idle, 1 connecting, 2 connected, 3 reconnecting, 4 lost, 5 disconnecting
uint8 name_len
uint8 name[name_len]
```

The list ends with a record whose camera ID is `0xff`. Indications carry list
records and command responses. Notifications carry changed camera state.

Camera IDs are stable across catalog reordering and deletion. Selection is the
same in-memory Multi-Connect target set used by the on-device Cameras page. A
scan updates only transient scan results, so it does not hide saved cameras.
Connect and disconnect requests follow the device control request path. A
connect request is busy while scanning or another connect is in flight.
