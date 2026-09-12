# Android camera catalog

## Scope

The Android companion now consumes the camera characteristic defined by
`plans/51-app-feature-parity.md`. It discovers the optional characteristic,
subscribes to both indication and notification traffic, decodes bounded
8-byte-header records with strict UTF-8 validation, and gates the Cameras tab
on both the characteristic and capability feature bit 1.

The catalog is keyed by the firmware's stable `camera_id`, not by list
position. Refresh replaces the current snapshot at the list terminator while
live state records update the matching ID. Select, connect, and all-camera
disconnect requests remain authenticated and use the existing serialized GATT
operation queue. The tab is hidden for older firmware and for unauthenticated
sessions.

## Verification

Host protocol tests cover the frozen UUID, capability bit, request bytes,
record layout, signed RSSI, UTF-8 names, terminators, and malformed/truncated
records. The GATT contract test covers the write/indicate/notify property set
and authenticated operation requirement. Android build and device validation
are run by the integration owner.
