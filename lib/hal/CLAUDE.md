# lib/hal/ (pure chip drivers)

Register-level drivers for power, gauge, IO expander and radio chips on boards
the board support library does not know. Design and board gap tables are in
`plans/322-hardware-abstraction.md`.

- Pure C++17. No vendor SDK, RTOS, board support or UI header, and no token
  `tools/check_portability_inventory.py` forbids, comments included. The
  directory is a portable contract root; list every file in
  `tools/portable_core_manifest.txt`.
- All I/O goes through `HalBus.h` (`RegisterBus`) or a driver's own small
  `Port` class. Firmware supplies the adapter; `tests/host/hal_drivers_test.cpp`
  supplies the fakes. A new driver gets cases there and a CMake source entry.
- Cite the datasheet title, revision and section next to every register,
  bit and command. The test states expected values from the datasheet, never
  from the driver constants. Do not commit datasheet PDFs.
- Reuse first. A chip the pinned board support library already drives on an
  existing board keeps that driver there. Add a register here only for a need
  that library does not cover, and record the gap in plan 322.
- Never switch off a rail the board lists as protected, and never add VRTC to
  the AXP2101 rail list: the chip has no switch for it.
