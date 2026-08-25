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
