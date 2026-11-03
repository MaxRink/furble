# 145 - Explicit connection context initialization

## Issue

The LVGL connection context assignment supplied only the first seven of its ten
aggregate fields. C++ safely value-initialized the remaining string, integer,
and boolean fields, but the compiler emitted `-Wmissing-field-initializers`.
The warning makes it harder to see a genuinely missing initialization when the
context grows, and positional defaults were not visible at the assignment.

## Design

The connection context assignment now supplies all ten fields, preserving the
existing values: a null widget and menu state, disconnected feedback, an empty
cached camera name, zero cached progress, and no established session. The
connection timer remains paused until the context and its widgets are ready.
This is a maintenance-only change. `doConnect()` continues to reset the cache
values for every new connection.

## Verification

- The affected M5StickS3 debug firmware build is run with the required
  `FURBLE_VERSION` and `FURBLE_TEST` environment variables.
- The host suite remains green because this change only affects the LVGL
  firmware translation unit.
- The diff is checked for whitespace and em-dashes.

## Hardware boundary

No hardware behavior changes. No hardware test is required for this warning
cleanup.
