# Companion camera management

The optional Companion BLE service exposes saved cameras through the Cameras
characteristic. It supports listing, selecting and deselecting cameras for
multi-connect, connecting to a selected camera, disconnecting all targets, and
state notifications.

Saved camera IDs are stable values 1 through 254. `0` means unsaved and `0xff`
means all cameras. IDs are monotonic and are not reused after deletion, reboot,
or migration from an older id-less index. Assignment stops at exhaustion rather
than wrapping into a reserved value.

The firmware keeps two CRC-checked index generations. It publishes the new
generation before deleting an entry, and retains the serialized camera record
until both durable generations no longer reference it. Deferred reclamation is
bounded and retryable after a power cut. The Cameras characteristic requires an
encrypted and authenticated Companion link.

New records use a thirteen-character uppercase key containing the exact
48-bit BLE address and address type, so public and random identities cannot
alias within the ESP-NVS fifteen-character key limit. Older address-only keys
remain readable as a legacy representation and are not used for new saves.
Index payloads are bounded before allocation at 254 records: 5,334 bytes in
the current layout and 5,080 bytes in the legacy layout. This is the storage
bound derived from the protocol's 1-254 camera-ID range.
