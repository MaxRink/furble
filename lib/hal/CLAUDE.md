# lib/hal/ (pure chip drivers)

Register-level drivers for power, gauge, IO expander and radio chips on boards
the board support library does not know. Design and board gap tables are in
`plans/322-hardware-abstraction.md`; the verification record is
`plans/353-hal-verification.md`.

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
- Bus pacing belongs to the driver. A chip with a bus-free time or a poll-rate
  rule gets its gap from `RegisterBus::delayUs()` at the call, cited, never
  from a fixed sleep in the adapter. The hook's default is a no-op for host
  fakes, so the firmware adapter of a board that carries such a chip must
  override it; plan 322's Pager gap list records that. A test double that waits
  on its own proves nothing about the driver, and the fake keeps waits in the
  same log as transfers so a case can assert the gap before each packet, not
  only that one was asked for somewhere.
- Enable what you decode. A status bit latches only when its enable bit is set
  (AXP2101 section 6.12.1), and some enables reset to 0 (section 6.13.2.41), so
  a driver that decodes an event must enable it too, or the field leaves the
  API. The test asserts that the enabled mask and the decoded bits agree. A
  latch also holds its interrupt line asserted until it is read and cleared
  (section 6.13.2.44), so a handler that returns without draining it re-fires,
  and a path that powers off without draining it leaves the next boot a phantom
  edge.
- Wait for a level, not for a clock. Where the datasheet bounds a transition,
  cite the number; where it does not (SX1262 BUSY after SetSleep, Table 8-2),
  poll inside a budget and state the total bound in the header. A single sample
  at a fixed delay reports a healthy part as broken.
- The fake bus honours each chip's addressing rule instead of walking up the
  register file: the AXP2101 reads registers linearly, a BQ27220 standard
  command stays inside its 16-bit word and the XL9555 wraps inside its 16-bit
  port pair. A transfer the part cannot serve is logged as a misuse and the
  case fails. Never widen the fake to make an access pass.
- Reuse first. A chip the pinned board support library already drives on an
  existing board keeps that driver there. Add a register here only for a need
  that library does not cover, and record the gap in plan 322.
- Never switch off a rail the board lists as protected, and never add VRTC to
  the AXP2101 rail list: the chip has no switch for it.
