# Companion camera management

The optional Companion BLE service exposes the saved camera list through the
Cameras characteristic. A companion can list saved cameras, select or deselect
them for multi-connect, connect to a selected camera, disconnect all targets,
and receive state updates.

## Stable camera IDs

Saved cameras use IDs 1 through 254. ID 0 is reserved for an unsaved camera and
`0xff` is the all-cameras marker. IDs are monotonic and are not reused after a
camera is deleted. This remains true after reboot, migration from an older
id-less index, and exhaustion of the available range. Once 254 has been used,
the firmware refuses another assignment rather than wrapping into 0 or `0xff`.

## Persistence and recovery

The camera index uses two CRC-checked journal generations. A new generation is
published by writing its payload, metadata, and commit marker in that order.
An interrupted write leaves the previous valid generation available. Migrated
IDs and the monotonic allocation floor are persisted before the migrated index
is published.

Deleting a camera first publishes an index that omits it. Its serialized camera
record is retained while either durable journal generation can still reference
it. Reclamation is attempted only after a later successful publication makes
both generations exclude the record. The bounded deferred-reclamation queue is
safe to retry after a power cut.

The Cameras characteristic requires an encrypted and authenticated Companion
link. The `0xff` marker applies to selection and all-camera operations; a
specific operation using an unknown ID is rejected.
