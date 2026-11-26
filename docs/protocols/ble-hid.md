# BLE HID

furble does not implement HID over GATT. It is a BLE central that speaks each
camera vendor's own service, and no supported camera accepts a HID report. This
page records what exists so the next reader does not assume a HID stack is
present.

| Item | Value |
| :--- | :--- |
| HID client | none |
| HID report parsing | none |
| HID service in the companion peripheral | none. The only GATT services furble serves are the companion service and Device Information |
| Code that mentions HID | one constant |

## The only HID reference

`lib/furble/Scan.h:181` defines

```cpp
static constexpr uint16_t HID_GENERIC_REMOTE = 0x180;
```

Nothing reads it. A repository-wide search finds that single definition and no
use, so it has no effect on scanning, matching or connecting.

### Decision on the value

The value is wrong and the constant is dead, and both statements are true at
once. The page states both rather than choosing:

- **Wrong value.** 0x180 is not the Bluetooth assigned number for HID over GATT,
  which is 0x1812. The name says HID and the number says something else, so the
  constant cannot be used as written for the filter its name implies.
- **Intentionally unused.** No code path reads the constant, no scan filter, no
  matcher and no connection logic. Deleting it and correcting it to 0x1812 have
  exactly the same effect on the firmware today: none.

The fix is a code change and does not belong in this documentation pull request.
It is recorded as follow-up FU-01 in
[plan 379](../../plans/379-docs-gap-closure.md#follow-ups): either remove the
constant or correct it to 0x1812 and wire it into a filter, at
`lib/furble/Scan.h:181`, in the pull request that first wants a HID scan.

## What a HID implementation would need

For a future camera or a generic BLE remote that only speaks HID, furble would
need:

1. A scan filter on the HID over GATT service, using the assigned number rather
   than the current constant.
2. A GATT client for the HID service, the Report Map characteristic and the
   Report characteristic.
3. A report descriptor parser, because the shutter usage inside a vendor report
   map is not at a fixed bit offset.
4. A mapping from the HID usage to `Control::CMD_SHUTTER_PRESS` and release, and
   the same for focus.

None of that exists, and none of it is required by any currently supported
camera.

## Open rows

| Missing | Why the repository cannot close it | Document and section needed |
| :--- | :--- | :--- |
| HID over GATT service definition and report protocol | Not implemented, so there is nothing in the tree to cite | Bluetooth SIG HID over GATT Profile specification, service and report sections |
| HID usage tables for a camera shutter | No usage is referenced by the code | USB HID Usage Tables, Consumer Page, the camera and shutter usages |
| Whether the unused constant should be removed or corrected | That is a code decision, not a documentation one | Owner decision on `lib/furble/Scan.h:181`, recorded in a plan |
