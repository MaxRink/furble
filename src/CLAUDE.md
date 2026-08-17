# src/ (app layer)

Application layer on top of lib/furble. Headers live in include/, sources here.

- `main.cpp` starts two contexts: a FreeRTOS control task
  (`Furble::Control::getInstance()`, priority 4) and the UI loop in the main
  task. Init order matters: Platform, Settings, Device before Control.
- `FurbleControl`: camera connection state machine. It owns the mutex from the
  root traps section. Keep critical sections short and delay-free.
- `FurbleSettings`: type-safe NVS settings via `Settings::load<KEY>()` /
  `Settings::save<KEY>()`, backed by lib/preferences. New settings need the
  enum entry, a `storage_type` specialization, and a default.
- `FurbleGPS`: TinyGPSPlus over UART2. Mind the UART clock source trap.
- `FurbleUI*`: LVGL UI. Respect the changed-check rule for periodic setters.
  The display off state machine lives here: `processInactivity` dims or sleeps
  the panel, sleep/wake pairs the APB lock with a 120 ms SLPIN/SLPOUT dwell,
  and a wake press is swallowed until every input source reports released.
- New source files must be added to `src/CMakeLists.txt` (alphabetical, before
  main.cpp). Component deps go in `idf_component_register` there.
