# PR27: USB serial command console

## Goal

Add an optional text console over the USB port that is already there, so a
developer or a test script on the host can drive furble without touching the
screen. Compile-time gated and off in every shipping build.

All line anchors below were read at commit `2b79ce8` on `master`.

## Motivation

Testing furble means poking at it by hand through a three button UI.

Checking whether a setting took effect means walking a menu tree. Firing the
shutter means holding the device. Reading the battery voltage, the GPS fix or
the connection state means finding the page that shows it. Every one of those is
a handful of button presses, and every change has to be re-tested the same way
on the next build. There is no way to script it.

The USB cable is already plugged in during development. A line oriented console
over it would let a developer read and set any setting, fire the shutter, query
status and dump diagnostics in one line each. More importantly, it makes the
rest of this plan testable. Every later PR in this series claims a battery or
timing improvement that has to be verified on hardware, and right now that
verification is a person with a stopwatch. With a console, it is a script on the
host that runs the same sequence every time.

## Draft issue

Open this before any code. Motivation only, no design.

> **No way to drive furble from the host for testing**
>
> Testing a change on hardware means walking the menu tree by hand with three
> buttons, once per build, which makes it slow to check anything and impossible
> to repeat exactly. The USB cable is already connected during development and
> already carries the log output, but there is no way to send anything back the
> other way. A simple text console on that port would let me read and set
> settings, fire the shutter, and query battery, GPS and connection state from a
> script on the host, which would make on-device regression testing possible at
> all. Would a developer-only console, compiled out of release builds, be
> welcome?

## Scope

In scope:

- An `esp_console` REPL, over USB-Serial/JTAG on the S3 and over UART0 on the
  ESP32 boards.
- A small command set covering settings, status, shutter, focus, scan and
  connect.
- A request queue so console commands reach the UI task safely.
- Compile-time gate. Not built into any of the five release environments.

Out of scope:

- Any change to the committed `sdkconfig.*` files. PR00 sets that rule and it
  holds here.
- Any UI change. The console adds no menu item, no page and no setting.
- A BLE or wifi control surface. That is PR50.
- Scripting or a host side test harness. This PR provides the interface. The
  harness follows.

## Files to change

- `src/CMakeLists.txt:12-14`, `idf_component_register`. It lists `PRIV_REQUIRES
  esp_pm` and `REQUIRES icons`. Because the requires are explicit, `console` has
  to be added to `PRIV_REQUIRES` or the include will not resolve.
- `src/main.cpp:21-40`, `app_main`. `Furble::Platform::init()` at line 27,
  `Furble::Settings::init()` at line 28, `Furble::Device::init(...)` at line 29,
  the control task created at line 32, and `vUITask(NULL)` at line 39, which
  never returns. Start the console after line 32 and before line 39.
- `src/main.cpp` and a new `src/FurbleConsole.cpp` plus
  `include/FurbleConsole.h`. Add the source to the `furble_sources` list at
  `src/CMakeLists.txt:1-10`.
- `platformio.ini:1-5`, the `[furble]` shared `build_flags`. The gate flag is
  added to the debug environments from PR00, not here.
- `src/FurbleUI.cpp:2123-2134`, `UI::task`. Line 2125 calls
  `Platform::getInstance().update()`, lines 2127-2129 take `UI::m_Mutex` around
  `lv_task_handler()`, line 2133 delays 5 ms. The console request queue is
  drained here.
- `src/FurbleUI.cpp:42`, `std::mutex UI::m_Mutex`. It is private and static. Any
  LVGL call from another task must be under it.
- `include/FurbleUI.h:251` and around, the `UI` class. Add the queue handle and
  the request enum.
- `include/FurbleControl.h:13-22`, `cmd_t`. `CMD_SHUTTER_PRESS`,
  `CMD_SHUTTER_RELEASE`, `CMD_FOCUS_PRESS`, `CMD_FOCUS_RELEASE`, `CMD_CONNECT`
  and `CMD_DISCONNECT` are the commands the console maps onto.
- `src/FurbleControl.cpp:183-185`, `Control::sendCommand`. It is a single
  `xQueueSend` with a zero timeout, so it is safe to call from any task. This is
  the entry point for every camera action.
- `include/FurbleControl.h:124`, `getState()`, and `include/FurbleControl.h:99`,
  `getTargets()`. These back the `status` and `cameras` commands.
- `src/FurbleSettings.cpp:11-24`, `m_Setting`. The table already holds a display
  name, an NVS key and a namespace for every setting. `settings list` iterates
  it.
- `include/FurbleSettings.h:65-90`, the public `Settings` interface. `m_Setting`
  itself is private, so a public accessor is needed. See Implementation notes.
- `include/FurbleGPS.h`, `GPS::getInstance().get()` returns the `TinyGPSPlus`
  object used by the GPS Data page at `src/FurbleUI.cpp:1555-1589`. The `gps`
  command reads the same fields.
- `lib/furble/CameraList.h:28-62`, `load()`, `size()`, `get(n)` and
  `getSaveCount()`. These back `cameras list` and `connect <n>`.
- `lib/furble/Scan.h:23-54`, `Scan::getInstance()`, `start`, `stop`, `isActive`,
  `clear`. These back the `scan` commands.

## New settings

None.

The console is compile-time gated, so there is nothing to toggle at runtime and
nothing to store. This is deliberate. A runtime setting would mean a switch in
the Features menu, which is a UI element for a feature no user of a release
build can reach.

If upstream would rather have it available in release builds, the fallback is
one bool `CONSOLE` setting defaulting to `false`, following the pattern at
`src/FurbleSettings.cpp:209-215`, with a switch added next to the existing ones
at `src/FurbleUI.cpp:1616-1619`. Offer it, but lead with the compile-time gate.
It costs nothing in the release binary, which is the whole argument.

## Menu placement

None. The console adds nothing to the UI.

## Implementation notes

### Transport

ESP-IDF v5.4.2 is what this project builds against (`dependencies.lock`,
`version: 5.4.2`). Three REPL constructors exist, verified in
`components/console/esp_console.h` of the pinned toolchain:

```
esp_err_t esp_console_new_repl_uart(...);              // esp_console.h:393
esp_err_t esp_console_new_repl_usb_cdc(...);           // esp_console.h:416
esp_err_t esp_console_new_repl_usb_serial_jtag(...);   // esp_console.h:439
esp_err_t esp_console_start_repl(esp_console_repl_t *repl); // esp_console.h:450
```

Pick by board:

- M5StickS3, `FURBLE_M5STICKS3`. Use `esp_console_new_repl_usb_serial_jtag` with
  `ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT()` (`esp_console.h:131`). The
  S3 has the peripheral: `CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED=y`
  (`sdkconfig.m5stick-s3:27`) and `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`
  (`sdkconfig.m5stick-s3:1134`). The StickS3 USB-C port is that peripheral,
  which is why `pio device monitor` already works on it without a bridge chip.
- All four ESP32 boards. Use `esp_console_new_repl_uart` on UART0. They reach
  the host through a USB-UART bridge and the console is already on UART0
  (`CONFIG_ESP_CONSOLE_UART_NUM=0`, `sdkconfig.m5stick-c-plus:1262`).

Then `esp_console_register_help_command()` (`esp_console.h:332`), one
`esp_console_cmd_register()` (`esp_console.h:237`) per command, and
`esp_console_start_repl()`. The REPL runs in its own task, sized by
`esp_console_repl_config_t.task_stack_size` and `task_priority`. Give it a
priority below the control task, which runs at 4 (`src/main.cpp:32`), and below
the per camera target tasks, which run at 3 (`src/FurbleControl.cpp:257`). Use
2.

### Coexistence with log output

On the S3 the log already reaches the same USB port.
`CONFIG_ESP_CONSOLE_UART_DEFAULT=y` (`sdkconfig.m5stick-s3:1459`) selects UART0
as the primary console and `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`
(`sdkconfig.m5stick-s3:1465`) mirrors the output to USB-Serial/JTAG. The ESP-IDF
console documentation says to disable the secondary output when running an
interactive console on the same hardware interface, because a secondary console
is output only.

Do not change the sdkconfig. PR00 establishes that the committed sdkconfig files
stay untouched so release builds do not shift. Two acceptable outcomes instead:

- Accept the interleaving. `linenoise` redraws the prompt after output, so a log
  line does not destroy the line being typed. This is a debug tool. Verify on
  device that it is actually usable, and if it is, say so and move on.
- Ship a `log <tag> <level>` command that calls `esp_log_level_set`, so the
  developer quiets the noise with `log * warn` as the first thing they type.

Do both. The second one costs five lines and makes the first one comfortable.

On the ESP32 boards, `esp_console_new_repl_uart` installs the UART driver on the
same UART0 that carries the log. That is exactly what the ESP-IDF console
example does, so it is a supported configuration. Confirm it on a StickC Plus
rather than assuming it.

### Light sleep

Automatic light sleep is on. `Platform::setSleep(true)` runs at
`src/FurblePlatform.cpp:14` and calls `esp_pm_configure` with
`light_sleep_enable` at lines 65-72. `CONFIG_PM_ENABLE=y`
(`sdkconfig.m5stick-s3:1359`).

On the S3 this is already handled and no new code is needed.
`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y` is set at `sdkconfig.m5stick-s3:1135`.
The ESP-IDF USB Serial/JTAG console documentation says that option makes the
chip detect an active USB connection and stay out of automatic light sleep, at
the cost of extra power. Since the console is only ever used with the cable
plugged in, and since the option is already enabled, console RX works today with
no pm lock. Verify by typing a command after the device has been idle for a
minute.

On the ESP32 boards over UART0 there is no such protection. Light sleep gates
the APB clock and UART RX drops characters. Take an `ESP_PM_APB_FREQ_MAX` pm
lock for as long as the console is enabled, using `esp_pm_lock_acquire`. Holding
a lock permanently defeats light sleep entirely, which would be unacceptable in
a release build and is fine in a debug-only build. Say that explicitly in the
code comment so nobody copies the pattern.

If PR06, the power module with counted pm locks, has landed, use that instead of
a raw lock. If it has not, a local lock is fine and PR06 can absorb it later.

### Marshalling onto the right task

Three destinations, three rules.

**Camera actions go through `Control::sendCommand`.** It is one `xQueueSend`
with a zero timeout (`src/FurbleControl.cpp:183-185`) and the control task
drains it (`src/FurbleControl.cpp:135-181`). Safe from the REPL task with no
locking. `shutter press`, `shutter release`, `focus press` and `focus release`
are direct one line mappings onto `CMD_SHUTTER_PRESS` and friends. Note the
control task only forwards those commands in `STATE_ACTIVE`
(`src/FurbleControl.cpp:159-178`), so the console should print the current state
rather than silently doing nothing when no camera is connected.

**Anything that touches LVGL goes through a request queue drained by the UI
task.** `UI::doConnect` and `UI::doDisconnect` (`src/FurbleUI.cpp:1229-1249` and
`1251-1263`) both call `lv_menu_set_page` and manipulate objects, so the REPL
task must not call them. Add a small FreeRTOS queue owned by `UI`, holding an
enum plus one integer argument, and drain it at the top of the `UI::task` loop
(`src/FurbleUI.cpp:2123-2134`), inside the same `m_Mutex` section that already
wraps `lv_task_handler()` at lines 2127-2129. That is roughly fifteen lines and
it is the same mechanism PR50, the companion app, will need. Do not take
`UI::m_Mutex` from the REPL task directly. It is private and static, and
reaching into it from another translation unit would be the wrong shape.

**Settings go through `Settings::save` directly.** It is NVS through
`Preferences` (`src/FurbleSettings.cpp:39-45`) and is safe from any task. But
saving is not the same as applying. Some settings are read on every use, for
example `MULTICONNECT` at `src/FurbleUI.cpp:1438` and `RECONNECT` at
`src/FurbleUI.cpp:1243`, and those take effect immediately. Others are cached in
the UI at construction, for example brightness at `src/FurbleUI.cpp:89-90`, and
those only take effect after a reboot. Do not try to fix that here. Have
`settings set` print `saved, applies on reboot` for the cached ones, and
document which is which in the command help.

### Reaching the settings table

`Settings::m_Setting` is private (`include/FurbleSettings.h`, the private
section after line 84). `settings list` needs to walk it. Add one public
accessor:

```
static const std::unordered_map<type_t, setting_t> &getAll(void);
```

`Settings::get(type_t)` already exists and is public
(`src/FurbleSettings.cpp:26-28`), so this is the same idea one level up. The
alternative, a hand written table in the console file, would go stale the first
time a setting is added. Do not do that.

Load and save are templates specialised per storage type
(`include/FurbleSettings.h:70-84`, specialisations at 96-148). The console
cannot be generic over them, so `settings get` and `settings set` need a switch
on `type_t` that picks the right instantiation. That switch is the one place
that has to be updated when a setting is added, and the compiler will not warn
about it. Add a `default:` that prints "unsupported type" rather than silently
doing nothing.

### Command set

One command per line, output one fact per line as `key: value`, so a host script
can parse it with a split on the first colon. No tables, no boxes, no colour.

```
help
version
status                      state, connected count, uptime, free heap
battery                     percent, voltage, current
gps                         enabled, fix age, satellites, lat, lon, alt
settings list               one line per setting: key, type, value
settings get <key>
settings set <key> <value>
cameras list                saved cameras with index, name, type
cameras status              active targets with name, connected, rssi
connect <index>             saved camera index, from cameras list
connect                     with no index, connect the multi-connect selection
disconnect
shutter press | release
shutter hold <ms>
focus press | release
scan start | stop | list
log <tag> <level>
reboot
```

`cameras status` overlaps with the Cameras page from PR25 and should read the
same state through the same `Camera::isConnected()` and `Camera::getRSSI()`. If
PR25 has not landed, drop the rssi field rather than duplicating the accessor.

`battery` overlaps with PR02. Same rule: read whatever PR02 exposes, or drop the
command until PR02 lands.

Use `esp_console_cmd_t.func` for the simple commands and argtable for the ones
with arguments. Argtable comes with the console component, so it costs no extra
dependency.

### Security and scope

The console is a developer feature and the compile-time gate is what keeps it
honest. Nothing in a release build changes, so there is no attack surface to
argue about.

Two rules to hold anyway, because someone will eventually ask for a runtime
switch:

- `settings get` works only on keys in the `Settings::m_Setting` table. Never
  add a raw NVS read or dump command. BLE pairing keys and bonding data live in
  NVS in namespaces furble does not own, and a generic dump would print them.
- Nothing writes to NVS outside the `Settings` interface.

### Flash cost

`esp_console` pulls in `linenoise` and `argtable3`. The cost is real but it
lands only in the gated build, so no release environment grows by a byte. Prove
that rather than claiming it:

```
pio run -e m5stick-s3 -t size
pio run -e m5stick-s3-debug -t size
```

Report both numbers in the PR body. The project uses
`partitions_singleapp_large.csv` (`platformio.ini:11`), so there is headroom on
the S3 either way. Check the smallest target, `m5stick-c`, before enabling the
gate in its debug environment, and if it does not fit there, enable the console
on the S3 debug environment only and say so.

## Dependencies

PR00, the debug environments, is a soft dependency. The gate flag belongs in
those environments, so without PR00 this PR has to add an environment of its
own. Land PR00 first.

PR06, the power module, would supply the pm lock for the ESP32 boards. Not
required.

PR02 and PR25 supply the data behind the `battery` and `cameras status`
commands. Not required, drop those commands if they are not there.

PR50, the companion app, wants the same UI request queue. Building it here means
PR50 inherits it.

## Risks

- The UI request queue is new machinery in the hottest loop in the firmware
  (`src/FurbleUI.cpp:2123-2134`). A bug there affects every board and every
  user, gate or no gate, if the drain call is not itself gated. Compile the
  drain out entirely when the console is not built.
- Log and prompt interleaving may turn out to be unusable in practice on the S3,
  because the log goes to the same port. If it is, the honest fix is a local,
  uncommitted sdkconfig change documented in the README, not a committed one.
- `esp_console_new_repl_uart` on a UART that already carries the log is standard
  but has to be confirmed on real ESP32 hardware, not assumed from the S3
  result.
- Holding a pm lock forever on the ESP32 boards makes any power measurement
  taken with the console enabled meaningless. Say so loudly in the README, or
  every later PR in this series will measure the wrong thing.
- A console that can connect and fire the shutter can also leave a camera with
  the shutter held if a script dies between `shutter press` and `shutter
  release`. `shutter hold <ms>` exists so scripts do not have to do the pairing
  themselves. Prefer it in documentation and examples.
- Scope creep. The command list above is already long. Anything beyond it
  belongs in a follow-up.

## Verification

Build matrix, all five release environments must be unchanged:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Confirm the five release binaries are byte identical to master apart from the
version string. Then build the gated variants.

On device, M5StickS3 over USB:

1. Flash the gated build. `pio device monitor -e m5stick-s3-debug`. Confirm the
   `esp>` prompt appears.
2. `help`. Confirm every command is listed.
3. `log * warn`. Confirm the log quiets down and the prompt is usable.
4. `version`, `status`, `battery`, `gps`. Confirm each prints and that the
   values match what the corresponding UI page shows.
5. `settings list`. Confirm every setting in `src/FurbleSettings.cpp:11-24`
   appears with its current value.
6. `settings set gps true`, then walk to the GPS menu on the device. Confirm the
   switch reflects the new value on the next visit, or that the command said it
   needs a reboot.
7. `cameras list`, then `connect 0`. Confirm the device connects to the Fujifilm
   camera and the screen lands on the Connected page.
8. `shutter press`, `shutter release`. Confirm the camera fires.
9. `shutter hold 200` twenty times in a loop from a host script. Confirm twenty
   frames and no missed or stuck shutter.
10. `disconnect`. Confirm the device returns to the main menu with a working
    back button.
11. `scan start`, wait, `scan list`, `scan stop`. Confirm the results match what
    the Scan page finds.
12. Leave the device idle for two minutes, then type a command. Confirm it is
    received. This is the light sleep check on the S3.
13. Use the device by hand through the UI while the console is connected.
    Confirm the UI is not slower and nothing is dropped.
14. Run every command with wrong or missing arguments. Confirm each prints an
    error and none crash.
15. `reboot`. Confirm a clean restart and that the prompt returns.

On a StickC Plus over USB, repeat steps 1 to 8 and 12. Step 12 is the one that
matters there, because it is the UART pm lock path rather than the S3 path.

Battery drain: not applicable to release builds, which do not contain the
console. For gated builds, measure and record idle current with the console
enabled versus a release build, so later power measurements are not taken with
the pm lock held by mistake.

Camera coverage: Fujifilm on hardware, FauxNY for the paths that do not need a
real camera. The console sends the same `Control::cmd_t` values the UI already
sends, so no vendor specific code is touched. State this in the PR body.

## References

- ESP-IDF v5.4, Console. `esp_console_new_repl_uart`,
  `esp_console_new_repl_usb_serial_jtag`, `esp_console_repl_config_t`,
  `esp_console_cmd_register`, and the note that a secondary serial console is
  output only and should be disabled for an interactive console:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/console.html
- ESP-IDF v5.4, USB Serial/JTAG Controller Console.
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` versus
  `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG`, and
  `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` for light sleep with USB connected:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/usb-serial-jtag-console.html
- ESP-IDF v5.4, Power Management. Automatic light sleep and
  `esp_pm_lock_acquire`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/power_management.html
- ESP-IDF v5.4, Logging library, `esp_log_level_set`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/log.html
- PlatformIO, ESP-IDF framework and sdkconfig handling:
  https://docs.platformio.org/en/latest/frameworks/espidf.html
- PlatformIO, device monitor:
  https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html
