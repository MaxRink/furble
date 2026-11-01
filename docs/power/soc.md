# SoC, OS, task, lock, logging, and console power opportunities

Status: research note. No firmware, configuration, or test change is made by this
document.

Tree: `power/research`, code read at commit
`4a38238f1a0ce8c730e89b8bf537c16a648b4cc9` (2026-11-01). The power research
docs were later committed as `b2734c07` with no source change, so the code
references remain valid.

## Scope and evidence rules

This note covers power opportunities that sit below the product feature layer:
the SoC clock and sleep configuration, the FreeRTOS task and wake structure, the
`esp_pm` lock users, logging, and the serial console. It does not re-audit camera
protocols, GPS sentence handling, or display rendering except where those
directly set a CPU or sleep state.

Evidence labels follow `docs/power/README.md`: every row is `measured`,
`modeled`, `inferred`, or `unknown`. The parenthetical detail says what kind of
evidence it is.

| Label | Meaning |
|---|---|
| `measured` | A hardware measurement on this project's boards, recorded in `plans/00-hardware-experiments.md`. Quoted here, not repeated. |
| `modeled` | Produced by the plan 63 simulator energy model and recorded in plan 98. Not a hardware measurement. |
| `inferred` | Code reading, a datasheet, a Kconfig value, or arithmetic derived from those. The parenthetical names the kind. |
| `unknown` | No current, latency, or energy measurement exists for this item. |

Hard rules applied in this note:

- No hardware current measurement was made for this document.
- No simulator build or scenario run was made for this document. The plan 98
  model numbers are quoted as `modeled`, not reproduced.
- USB current is not a battery current claim.
- A USB-attached StickS3 does not enter automatic light sleep because
  `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y` acquires `ESP_PM_NO_LIGHT_SLEEP`
  while the port is connected (`sdkconfig.m5stick-s3:1821`, IDF
  `esp_driver_usb_serial_jtag/Kconfig:14-27`). Every USB-attached number is an
  active-current number.
- A developer console build is not a power measurement build. The ESP32 console
  holds `ESP_PM_APB_FREQ_MAX` for its lifetime
  (`src/FurbleConsole.cpp:2833-2844`) and the S3 console uses the USB Serial/JTAG
  path.

## The current power architecture

### Boot and frequency

`Platform::getInstance()` calls `Power::init()` and then
`setCPUMaxFreq(CPU_MAX_FREQ_DEFAULT_MHZ)`, which is 160 MHz, before `M5.begin()`
(`src/FurblePlatform.cpp:35-45`, `include/FurblePlatform.h:21-24`). `app_main`
applies the stored `CPU_FREQ` afterwards (`src/main.cpp:337-339`). A device
stored at 80 MHz therefore boots at 160 MHz for the interval between
`Platform::init()` and the stored-value apply.

`Power::configure()` always passes `min_freq_mhz = 40` and
`light_sleep_enable = true` (`src/FurblePower.cpp:37-44`,
`include/FurblePower.h:61`). The maximum is one of 80, 160, or 240 MHz
(`include/FurblePlatform.h:21`, `src/FurbleSettings.cpp:681-683`), default 160.

`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=80` and `board_build.f_cpu = 80000000L`
(`sdkconfig.m5stick-s3:2189`, `platformio.ini:10`) describe the pre-app state.
`CONFIG_PM_DFS_INIT_AUTO=y` (`sdkconfig.m5stick-s3:2111`) applies that at startup,
then the app overrides it.

### Automatic light sleep

Automatic light sleep is enabled on every board by `esp_pm_configure()` with
`light_sleep_enable = true`. It only takes effect where
`CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`. That is true on the five release battery
configs and the headless S3 config
(`sdkconfig.m5stick-s3:2491`, `sdkconfig.m5stick-c:2056`,
`sdkconfig.esp32-s3-headless:2495`), but false on the Waveshare S3 Ethernet
config (`sdkconfig.waveshare-s3-eth:2490`). The Waveshare image therefore asks
for light sleep and cannot enter it.

`CONFIG_PM_SLP_IRAM_OPT=y` (`sdkconfig.m5stick-s3:2114`) and
`CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP=y` (`sdkconfig.m5stick-s3:2118`) are
already set. IDF states those are worth about 310 us of extra sleep per light
sleep and about 650 uA on the S3 respectively (IDF `esp_pm/Kconfig:62-70`,
`esp_pm/Kconfig:122-135`). `CONFIG_PM_RTOS_IDLE_OPT` is unset on every board
(`sdkconfig.m5stick-s3:2115`), so about 20 us per light sleep is left on the
table (IDF `esp_pm/Kconfig:72-79`).

### Locks held by furble

| Lock | Owner | When held | Code |
|---|---|---|---|
| `NO_LIGHT_SLEEP` | `control` | Entire connection when `SLEEP_CONN` is off | `src/FurbleControl.cpp:1118-1134` |
| `NO_LIGHT_SLEEP` | `gps` | Predicted NMEA burst windows, acquisition, resync, probe | `src/FurbleGPS.cpp:485-501` |
| `NO_LIGHT_SLEEP` | `charging` | While the battery reports charging | `src/FurbleUI.cpp:9906-9914` |
| `APB_FREQ_MAX` | `display` | Panel on or dimmed, released only when the panel sleeps | `src/FurbleUI.cpp:371-374`, `1316-1318`, `1333-1335` |
| `CPU_FREQ_MAX` | none | Never acquired by application code | `include/FurblePower.h:22`, `src/FurblePower.cpp:16-31` |

A held `ESP_PM_APB_FREQ_MAX` lock maps to `PM_MODE_APB_MAX`, and
`ESP_PM_NO_LIGHT_SLEEP` maps to `PM_MODE_APB_MIN`. `get_lowest_allowed_mode()`
returns `PM_MODE_LIGHT_SLEEP` only when the mode mask is below both of those
(IDF `esp_pm/pm_impl.c:228-241`, `555-568`). In plain terms: the display APB
lock blocks automatic light sleep for as long as the panel is on, and also
prevents DFS below the APB maximum.

### Peripheral clock gates

| Peripheral | Clock and lock state | Current gate | Power consequence |
|---|---|---|---|
| LEDC backlight, StickS3 and Core | APB by default | Display holds `APB_FREQ_MAX` while the panel is on | Blocks light sleep and DFS below 80 MHz. A different LEDC source is LOCK-03. |
| PMIC-dimmed backlight, StickC, StickC Plus, Core2 | AXP192 LDO2 or DCDC3 voltage | Display still holds `APB_FREQ_MAX` | The lock is not needed for the backlight on these boards. LOCK-03. |
| GPS UART | `UART_SCLK_XTAL` on S3, `UART_SCLK_REF_TICK` on ESP32 | No APB dependency | Correct. Do not change. |
| Console UART | APB, 115200 | Always on in release | Logs only. Cost is output and CPU, not a clock gate. |
| USB Serial/JTAG | USJ clock, enabled by the secondary console | `USJ_NO_AUTO_LS_ON_CONNECTION` while a host is attached | Blocks light sleep when USB is connected. CON-01. |
| RMT IR | APB, held by the IDF RMT driver | `rmt_enable` to `rmt_disable` bracket | Correct. No idle hold. |
| I2S speaker | APB, held by the IDF I2S driver | Channel enabled only while a tone plays | Correct. The amplifier is also closed between patterns. |
| I2C PMIC, touch, IMU | APB-derived divider, recomputed per transaction | Transactions as needed | The PM1 button read is about 200 transactions per second. OS-07. |
| SPI display | APB, held by the IDF SPI master during a transaction | Per flush transaction | Correct for the IDF driver. M5GFX's exact path is not verified here. |
| SD card | SPI, only while mounted | Mount only when GPX logging is enabled | No idle cost when logging is off. |
| SPI flash | APB and SPI | Always available | Flash power-down is rejected on S3 (PSRAM) and only worth evaluating after the loop is lengthened. SOC-10. |
| PSRAM, S3 | SPI | Enabled at boot | Retention current in light sleep is not in the model. SOC-11. |
| WiFi and Bluetooth | Modem and radio clocks | BT modem sleep is on; WiFi is off by default | The BT configuration is correct. WiFi off costs nothing at runtime. SOC-13 is WiFi-on only. |
| ADC | Not used directly for battery | Battery comes from the PMIC | No gate to change. |

### Wake budget

Approximate periodic wake sources per second, read from the code:

| Source | Period | Owner | Count per second | Citation |
|---|---|---|---|---|
| UI loop | 5 ms | main task, priority 1 | 200 | `src/FurbleUI.cpp:10177-10218` |
| PMIC button read | inside UI loop | main task, I2C | 200 | `src/FurblePlatform.cpp:747-758` |
| Control queue | 50 ms | control task, priority 4 | 20 | `src/FurbleControl.cpp:379-386` |
| Target queue | 50 ms per camera | target task, priority 3 | 20 per camera, up to 180 at 9 cameras | `src/FurbleControl.cpp:100-107`, `977` |
| GPS UART queue | 100 ms or event | GPS task, priority 3 | 10 plus UART events | `src/FurbleGPS.cpp:209-260`, `525-592` |
| Companion service | 1000 ms | companion task, priority 2 | 1 when enabled | `src/FurbleCompanion.cpp:424-456` |
| Inactivity policy | 1000 ms | LVGL timer | 1 | `src/FurbleUI.cpp:413-427` |
| Battery sample | 5000 ms | LVGL timer | 0.2 | `src/FurbleUI.cpp:575` |
| Icon refresh | 250 ms | LVGL timer | 4 while the panel is on, 0 while off | `src/FurbleUI.cpp:582-585`, `1314` |
| Connect liveness | 500 ms | LVGL timer | 2 while active, paused when idle | `src/FurbleUI.cpp:5216-5217`, `748-749` |
| Console read | 100 ms | console task, priority 2 | 10, console builds only | `src/FurbleConsole.cpp:68-71`, `2848-2853` |
| M5PM1 watchdog feed | 1000 ms | main task | 1 | `src/FurblePlatform.cpp:198-212` |
| Task watchdog | 5 s | ESP-IDF | 0.2 | `sdkconfig.m5stick-s3:2283-2287` |

The UI loop is the binding deadline whenever the display is off and no camera is
connected. With a 5 ms loop the chip can enter automatic light sleep roughly 200
times per second, each for only a few milliseconds. The plan 158 review already
says the simulator charges no time for that UI work (`plans/158:168-173`). This
is the central correction this note adds: a 5 ms loop does not produce a 0.25 mA
idle device unless the sleep entry and exit cost is near zero, and IDF says it
is not.

### Sleep entry and exit cost, from IDF constants

For the S3, IDF defines `DEFAULT_SLEEP_OUT_OVERHEAD_US = 382` with
`PM_SLP_IRAM_OPT=y`, and the light sleep path recalibrates the RTC clock on
every sleep when `CONFIG_PM_LIGHTSLEEP_RTC_OSC_CAL_INTERVAL=1`. The S3 does not
define `SOC_PMU_SUPPORTED`, so only the slow clock calibration runs: 10 slow
clock cycles at roughly 140 to 150 kHz, about 67 to 74 us. Use about 70 us as
the working figure. That gives about 452 us of overhead per sleep before entry
cost and loop overhead (IDF `esp_hw_support/sleep_modes.c:152-174`, `835-862`;
the fast clock block is guarded by `SOC_PMU_SUPPORTED`). Entry cost is not
included and is not measured here.

Arithmetic for the current screen-off idle case, using 200 sleeps per second as an
upper bound because loop work makes the real rate lower:

- 200 sleeps per second times (382 us wake-out + 70 us calibration) is about
  90 ms per second, or 9 percent of wall time.
- At the S3 active 80 MHz figure of 40.2 mA (datasheet Table 5-9), that is about
  3.6 mA average, before adding the 0.24 mA light sleep floor.
- At a 25 ms UI loop, 40 sleeps per second gives about 0.73 mA of the same
  overhead.
- At a 100 ms loop, 10 sleeps per second gives about 0.18 mA.

These numbers are `arithmetic`, not measurements. They assume the whole
overhead is CPU active time at 80 MHz, which is the conservative reading of the
Kconfig wording. The real value must be measured. The conclusion that survives
without measurement is directional: the plan 98 screen-off value of 0.25 mA
cannot be used to rank a 5 ms loop against a 25 ms loop, because it assigns zero
cost to 200 wake transitions per second.

## SOC: clocks, sleep modes, and peripheral gates

### SOC-01 Lower the DFS minimum below 40 MHz

- **Change:** Set `min_freq_mhz` to 20 or 10 MHz on the S3, and to the valid
  lower XTAL dividers on the ESP32 boards, instead of `CPU_MIN_FREQ_MHZ = 40`.
- **Current behavior:** `Power::configure()` always passes 40 MHz
  (`src/FurblePower.cpp:40`, `include/FurblePower.h:61`). The maximum is 80, 160,
  or 240.
- **Evidence:** inferred (code-read, datasheet) for the active current rows. The
  S3 accepts any frequency at or below the 40 MHz XTAL that divides exactly, so
  40, 20, 13, and 10 MHz are valid (IDF
  `esp_hw_support/port/esp32s3/rtc_clk.c:257-267`). The plan 98 audit called
  this out but did not measure it (`plans/98:169-172`).
- **Estimated saving:** Small and unquantified. The screen-on case is pinned at
  80 MHz or above by the display APB lock, so this only affects active
  screen-off work such as BLE host processing and GPS parsing. A fraction of a
  milliamp at best.
- **Risk:** Medium. Lower clocks stretch active time, and the BLE controller wake
  latency and camera link stability under a lower CPU floor are unverified.
- **Effort:** Small code change, medium validation.
- **Required test:** Build all boards. Bench A/B at 40, 20, and 10 MHz with the
  screen off, GPS on, and one camera connected. Run a 30 minute connected soak
  per vendor available. Confirm no missed notifications and no reconnect
  increase. Add a host test that the configured minimum is accepted by the
  validation helper.
- **Citation:** `include/FurblePower.h:61`, `src/FurblePower.cpp:37-44`, IDF
  `rtc_clk.c:257-267`, `plans/98:169-172`.

### SOC-02 Cap the CPU maximum at 80 MHz, or in the battery profile

- **Change:** Make the default maximum 80 MHz, or force 80 MHz from the Battery
  Saver profile, instead of the 160 MHz default.
- **Current behavior:** Default 160 MHz (`include/FurblePlatform.h:24`,
  `src/FurbleSettings.cpp:681-683`). DFS raises to 160 under UI and other CPU
  load.
- **Evidence:** inferred (code-read, datasheet). S3 active current is 40.2 mA at
  80 MHz and 56.9 mA at 160 MHz (datasheet Table 5-9). The plan 98 audit says
  the 160 default costs nothing at idle because DFS drops, but it does not
  measure the screen-on active share (`plans/98:166-172`).
- **Estimated saving:** Unquantified. If frame time doubles at 80 MHz, energy
  per frame can be similar. The only defensible statement is that the peak is
  lower.
- **Risk:** Medium. UI responsiveness and BLE connection setup timing are the
  documented risks (`plans/01:132-140`).
- **Effort:** Small code or profile change, medium validation.
- **Required test:** Measure menu idle, connected idle, and a fixed UI animation
  at 80 and 160 MHz. Record frame time, shutter round trip, and analyzer
  current. Reject if the frame time or shutter p95 regresses more than the
  agreed budget.
- **Citation:** `include/FurblePlatform.h:21-24`, `plans/01:132-140`,
  `plans/98:166-172`.

### SOC-03 Apply the stored CPU frequency before the first configure

- **Change:** Read `Settings::CPU_FREQ` in `Platform::getInstance()` and pass it
  to `setCPUMaxFreq()` instead of `CPU_MAX_FREQ_DEFAULT_MHZ`. `app_main` already
  calls `Settings::init()` before `Platform::init()` (`src/main.cpp:314-315`),
  so the value is available.
- **Current behavior:** `Platform` configures 160 MHz before `M5.begin()`, and
  `app_main` applies the stored value later (`src/FurblePlatform.cpp:35-45`,
  `src/main.cpp:337-339`). A stored 80 MHz device runs the boot splash, IR,
  feedback, SD, and netif init at 160 MHz.
- **Evidence:** inferred (code-read).
- **Estimated saving:** Boot window only. Tens of milliseconds at up to 56.9 mA
  instead of 40.2 mA on the S3. Negligible per day, but free and removes a
  boot-time inconsistency.
- **Risk:** Low. Init order already permits it. The only risk is a stale plan 01
  assumption that `Settings` cannot be read at this point (`plans/01:102-107`);
  that assumption is no longer true in this tree.
- **Effort:** Small.
- **Required test:** Build all boards. Boot with stored 80, 160, and 240 and
  confirm the first `CPU maximum frequency` log line already reports the stored
  value. Run the `settings-table` and `settings-nvs-roundtrip` host tests.
- **Citation:** `src/main.cpp:312-339`, `src/FurblePlatform.cpp:31-45`,
  `plans/01:102-107`.

Board file consistency note: `boards/waveshare-esp32-s3-eth.json` declares
`"f_cpu": "240000000L"`, `platformio.ini:10` overrides
`board_build.f_cpu = 80000000L` for every environment, and the runtime default
is 160 MHz. The runtime value is the one that sets power, but the three values
should be reconciled so the board file is not read as the shipped frequency.
This is a documentation and build consistency item, not a separate power lever.

### SOC-04 Enable `CONFIG_PM_RTOS_IDLE_OPT`

- **Change:** Set `CONFIG_PM_RTOS_IDLE_OPT=y` in all sdkconfigs.
- **Current behavior:** Unset on every board (`sdkconfig.m5stick-s3:2115` and the
  matching lines in the other configs).
- **Evidence:** inferred (kconfig). IDF says it puts about 180 bytes of RTOS idle code in
  IRAM and lets the chip sleep about 20 us longer per light sleep at 160 MHz
  (IDF `esp_pm/Kconfig:72-79`).
- **Estimated saving:** `arithmetic`: at 200 sleeps per second, 20 us is 4 ms
  per second, about 0.16 mA at 40.2 mA active. Lower when the loop is lengthened.
  Small but certain in direction.
- **Risk:** Very low. It costs about 180 bytes of IRAM and changes no policy.
- **Effort:** Small config change.
- **Required test:** Build all boards. Bench A/B with an analyzer and with the
  sleep counters from a `CONFIG_PM_PROFILING` build. Reject if the IRAM budget
  or the build size gate fails.
- **Citation:** IDF `esp_pm/Kconfig:72-79`, `sdkconfig.m5stick-s3:2114-2118`.

### SOC-05 Enable `CONFIG_PM_SLP_DISABLE_GPIO`

- **Change:** Set `CONFIG_PM_SLP_DISABLE_GPIO=y` and selectively exempt the pins
  that must wake the chip or hold a level.
- **Current behavior:** Unset (`sdkconfig.m5stick-s3:2116`). The code relies on
  pads holding through sleep: the feedback LED (`src/FurbleFeedback.cpp:14-20`),
  the motion wake GPIO (`src/FurblePlatform.cpp:447-476`), the PMIC button and
  IRQ lines, and the GPS UART.
- **Evidence:** inferred (kconfig). IDF says disabling all GPIO in sleep reduces current
  by about 200 to 300 uA (IDF `esp_pm/Kconfig:81-92`). That is comparable to
  the entire S3 light sleep floor, which is why it is attractive and why it is
  dangerous.
- **Estimated saving:** 0.2 to 0.3 mA in sleep per the Kconfig text, if the wake
  sources survive.
- **Risk:** High. Disabling pads can break button wake, PMIC IRQ wake, IMU motion
  wake, GPS UART reception, and the LED level. The Kconfig text requires
  `gpio_sleep_sel_dis()` on pins used as wake sources, and the ESP32 path needs
  `GPIO_ESP32_SUPPORT_SWITCH_SLP_PULL` first.
- **Effort:** Medium to high. Per-pin audit and a wake source matrix per board.
- **Required test:** For each board, verify every wake source: power button,
  side button, touch on Core2, IMU motion, PMIC IRQ, GPS NMEA, and display wake.
  Run a 24 hour screen-off soak and check for lost NMEA and spurious wakes.
  Measure with an analyzer at the battery boundary.
- **Citation:** IDF `esp_pm/Kconfig:81-92`, `src/FurbleFeedback.cpp:14-20`,
  `src/FurblePlatform.cpp:447-476`, `src/FurbleGPS.cpp:159-183`.

### SOC-06 Raise `CONFIG_PM_LIGHTSLEEP_RTC_OSC_CAL_INTERVAL`

- **Change:** Raise the calibration interval from 1 to 8, 16, or 32 sleeps.
- **Current behavior:** 1 (`sdkconfig.m5stick-s3:2117`), so the RTC slow and fast
  clocks are recalibrated on every automatic light sleep.
- **Evidence:** inferred (kconfig, IDF source). The Kconfig text says decreasing the
  interval increases active time, and the S3 calibration path runs the slow
  clock calibration every sleep when the interval divides the counter (IDF
  `esp_hw_support/sleep_modes.c:846-862`). The slow calibration is 10 slow clock
  cycles at roughly 140 to 150 kHz, about 67 to 74 us (IDF
  `sleep_modes.c:158-160`). The fast clock calibration in the same function is
  guarded by `SOC_PMU_SUPPORTED`, which the S3 does not define.
- **Estimated saving:** `arithmetic`: about 70 us per sleep. At 200 sleeps per
  second that is about 14 ms per second, or 0.56 mA at 40.2 mA active. At an
  interval of 8 it falls to about 0.07 mA. This overlaps the wake-out overhead in
  the summary calculation and must not be added to it twice.
- **Risk:** Medium-low. The RTC slow clock is the internal RC oscillator because
  no 32 kHz crystal is fitted (`sdkconfig.m5stick-s3:1976-1979`,
  `plans/00:186`). Longer intervals allow more RC drift, which affects light
  sleep wake time, not `esp_timer` timekeeping. It could affect `vTaskDelay`
  accuracy and any timing that assumes a precise wake.
- **Effort:** Small config change, medium measurement.
- **Required test:** With an analyzer, sweep 1, 8, 16, and 32 on the S3 screen-off
  idle case. Separately record wake lateness for 1 s, 5 s, and 45 s deadlines
  over an hour. Run the BLE connected soak and the GPS burst soak. Reject any
  setting that produces late wakes beyond the scheduled deadline margin.
- **Citation:** `sdkconfig.m5stick-s3:2117`, IDF `esp_pm/Kconfig:108-120`, IDF
  `sleep_modes.c:158-160`, `835-862`, `plans/00:186`.

### SOC-07 Lower `CONFIG_FREERTOS_HZ`

- **Change:** Evaluate 100 or 250 Hz instead of 1000 Hz.
- **Current behavior:** 1000 Hz (`sdkconfig.m5stick-s3:2466`,
  `sdkconfig.m5stick-c:2031`). The tick timer runs while the chip is awake; the
  tickless idle path stops it in sleep.
- **Evidence:** inferred (kconfig, code-read). The UI loop uses `vTaskDelay(5 ms)`,
  which is 5 ticks at 1000 Hz and 1 tick at 200 Hz. Many queue waits use
  millisecond values converted with `pdMS_TO_TICKS`.
- **Estimated saving:** Small. A 1 kHz tick interrupt is on the order of a few
  microseconds of CPU per millisecond, so roughly 0.1 to 0.3 mA while active.
  Under the display APB lock the CPU is active, so this is a screen-on and
  boot-time item, not an idle-floor item.
- **Risk:** Medium. Timer resolution, `pdMS_TO_TICKS` rounding, BLE host timer
  granularity, and the UI loop period all change. NimBLE host timers are
  tick-based.
- **Effort:** Medium. Config change plus a timing regression pass.
- **Required test:** Build all boards. Run the full host suite and the simulator
  scenario suite. Bench shutter latency, BLE supervision, UI frame time, and
  current at 100, 250, and 1000 Hz. If the delta is below the analyzer noise,
  close the item as rejected.
- **Citation:** `sdkconfig.m5stick-s3:2466`, IDF `freertos/Kconfig:33-40`,
  `319-348`.

### SOC-08 Tune `CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP`

- **Change:** Sweep the threshold instead of leaving it at 3 ticks.
- **Current behavior:** 3 (`sdkconfig.m5stick-s3:2492`). With a 5 ms loop and
  about 1 ms of UI work, the expected idle time is about 4 ms, which is just
  above the 3 ms threshold.
- **Evidence:** inferred (kconfig). IDF says FreeRTOS enters light sleep only when no task
  needs to run for this many ticks, and suggests raising it if sleep rejections
  are high (IDF `freertos/Kconfig:336-348`).
- **Estimated saving:** Direction is ambiguous. Raising the threshold skips
  shallow sleeps and stays in WAITI, which is about 29.1 mA at 80 MHz on the S3
  (datasheet Table 5-10) versus 0.24 mA in light sleep but with a 382 us wake
  cost. The optimum depends on the measured wake cost. No default change should
  be made from the current evidence.
- **Risk:** Medium. It changes sleep residency and therefore the thing being
  optimized.
- **Effort:** Small config sweep, medium measurement.
- **Required test:** Sweep 2, 3, 5, and 10 with the analyzer on the screen-off
  disconnected case and with a connected camera. Record sleep count, wake
  latency, and current. Do this after OS-01, because a longer loop changes the
  expected idle time.
- **Citation:** `sdkconfig.m5stick-s3:2492`, IDF `freertos/Kconfig:336-348`,
  IDF `sleep_modes.c:152-174`.

### SOC-09 Enable tickless idle on the Waveshare config

- **Change:** Set `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` in
  `sdkconfig.waveshare-s3-eth`, or stop claiming light sleep is enabled there.
- **Current behavior:** `CONFIG_PM_ENABLE=y` and `light_sleep_enable=true` are
  configured, but `CONFIG_FREERTOS_USE_TICKLESS_IDLE` is not set
  (`sdkconfig.waveshare-s3-eth:2108-2114`, `2490`). Automatic light sleep cannot
  happen.
- **Evidence:** inferred (code-read, kconfig). The Kconfig text says automatic light
  sleep support is disabled when tickless idle is disabled (IDF
  `freertos/Kconfig:319-334`).
- **Estimated saving:** No battery saving on a PoE or USB powered board. The
  value is configuration honesty and a working sleep path if the board runs from
  an alternative supply.
- **Risk:** Low to medium. Ethernet and the W5500 SPI path must survive light
  sleep. The board is not a battery target.
- **Effort:** Small config change.
- **Required test:** Build the env. Run the Ethernet link, DHCP, MQTT, and OTA
  soak with light sleep enabled. Check `pm` residency on the console.
- **Citation:** `sdkconfig.waveshare-s3-eth:2108-2114`, `2490`, IDF
  `freertos/Kconfig:319-334`.

### SOC-10 Flash power-down on the ESP32 boards

- **Change:** Evaluate `CONFIG_ESP_SLEEP_POWER_DOWN_FLASH=y` (the renamed
  `CONFIG_ESP_SYSTEM_PD_FLASH`) on the four ESP32 boards.
- **Current behavior:** Unset everywhere
  (`sdkconfig.m5stick-c:3346` and the matching lines). The S3 cannot enable it
  because PSRAM is configured (IDF `esp_hw_support/Kconfig:80-96`,
  `sdkconfig.m5stick-s3` SPIRAM block).
- **Evidence:** inferred (kconfig). IDF says it powers down flash during light sleep but
  costs more wake time, and requires no SPIRAM.
- **Estimated saving:** Unquantified. Flash standby current during light sleep,
  minus the 700 us flash-ready wait per sleep (`sleep_modes.c:152-157`,
  `1341-1358`). With a 5 ms loop the wake cost is likely larger than the flash
  saving, so this only makes sense after the loop is lengthened.
- **Risk:** Medium. Flash power-up timing, NVS, OTA, and crash recovery.
- **Effort:** Small config, medium validation on four boards.
- **Required test:** On each ESP32 board, cold boot, NVS round trip, OTA, and a
  30 minute disconnected light sleep soak. Measure at the battery boundary with
  an analyzer. Reject if wake time or flash errors increase.
- **Citation:** IDF `esp_hw_support/Kconfig:80-96`, `sleep_modes.c:1341-1358`,
  `sdkconfig.m5stick-c:3346`.

### SOC-11 PSRAM retention during light sleep

- **Change:** No concrete change is recommended. Establish whether the S3 PSRAM
  stays in self-refresh during automatic light sleep and what it costs.
- **Current behavior:** PSRAM is enabled on the S3
  (`sdkconfig.m5stick-s3` SPIRAM block). The light sleep datasheet figure of
  0.24 mA is a chip figure and does not describe the board PSRAM. The plan 63
  model has no PSRAM term (`tools/power-model/README.md:231-236`).
- **Evidence:** inferred (code-read, datasheet). The gap is real, the cost is
  `unknown`.
- **Estimated saving:** Unknown. Cutting PSRAM power would lose its contents, so
  any change requires moving all live allocations out of PSRAM first. That is a
  large, separate project.
- **Risk:** High if changed. Data loss and cache faults during sleep.
- **Effort:** Large, memory placement audit.
- **Required test:** Capability and allocation audit, then a light sleep soak
  with PSRAM power-down and a full functional pass.
- **Citation:** `sdkconfig.m5stick-s3` SPIRAM block,
  `tools/power-model/README.md:231-236`.

### SOC-12 Deep sleep between intervalometer frames

- **Change:** Implement plan 19: deep sleep between frames with a timed wake.
- **Current behavior:** No `esp_deep_sleep_start()` call exists in the tree. A
  timelapse keeps the device connected at the connected-idle current between
  frames.
- **Evidence:** inferred (code-read), modeled (plan 98). Plan 98 puts connected idle at
  84.5 mA and deep sleep at 7 uA for the S3, with the M5PM1 timer as the wake
  source (`plans/98:290-297`). This is a feature, not a tuning change.
- **Estimated saving:** Large in the timelapse scenario, close to the ratio
  between 84.5 mA and 7 uA plus PM1 overhead. No hardware measurement exists for
  the wake cycle.
- **Risk:** High. The plan 26 watchdog conflict is unresolved: the PM1 watchdog
  keeps counting in deep sleep and will reset the device unless disarmed or
  replaced by the PM1 timer (`plans/26:284-291`). Camera reconnect, GPS state,
  and display restore all become part of the wake path.
- **Effort:** Large.
- **Required test:** 100 wake cycles, corrupt state, missed frame, USB recovery,
  PM1 watchdog armed and disarmed, and an analyzer run across a full timelapse.
- **Citation:** `plans/19-interval-deep-sleep.md` (not implemented),
  `plans/98:290-297`,
  `plans/26:284-291`.

### SOC-13 WiFi sleep IRAM option

- **Change:** Evaluate `CONFIG_ESP_WIFI_SLP_IRAM_OPT=y` on battery boards that
  can run WiFi and MQTT.
- **Current behavior:** Unset on the S3, Core2, headless, and Waveshare configs
  (`sdkconfig.m5stick-s3:2357`, `sdkconfig.m5stack-core2:1925`). WiFi is off by
  default (`src/FurbleSettings.cpp:694-697`), so this only matters when the user
  enables the hub path.
- **Evidence:** inferred (kconfig). IDF says the Wi-Fi power-save average current is
  reduced when this option is enabled, at a cost of about 7.3 KB of IRAM when
  `ESP_WIFI_IRAM_OPT` is already on (IDF `esp_wifi/Kconfig:343-356`).
- **Estimated saving:** Unquantified. Only applies while WiFi is associated and
  power save is active.
- **Risk:** Low to medium. IRAM budget and coexistence behavior.
- **Effort:** Small config, measurement.
- **Required test:** Build all WiFi-capable boards. With an AP and MQTT broker,
  measure associated-idle current and publish latency with the option on and
  off. Run the coexistence soak from plan 96 batch 7.
- **Citation:** IDF `esp_wifi/Kconfig:343-356`, `sdkconfig.m5stick-s3:2348-2357`,
  `plans/96:461-473`.

## OS and tasks

### OS-01 Lengthen the UI loop when the display is off

- **Change:** Keep the 5 ms loop while the panel is on or animating, and use 20
  to 50 ms while the panel is off and no modal or connect is in flight. Apply
  the same change to the headless `vUITask()` loop.
- **Current behavior:** `UI::task()` calls `vTaskDelay(5 ms)` unconditionally
  (`src/FurbleUI.cpp:10217`). The headless loop does the same
  (`src/main.cpp:286-297`). Only the icon timer is paused when the panel sleeps
  (`src/FurbleUI.cpp:1314`); the inactivity, battery, GPS, control, target, and
  PMIC reads keep their cadence.
- **Evidence:** inferred (code-read, arithmetic). The 5 ms loop is the binding light
  sleep deadline in the screen-off state. The wake cost constants are cited in
  the summary above. The simulator does not charge this work
  (`plans/158:168-173`), so its 0.25 mA screen-off figure is not evidence
  against this change.
- **Estimated saving:** `arithmetic`: reducing from 200 to 40 sleeps per second
  removes about 2.9 mA of the 3.6 mA wake overhead estimated above; reducing to
  10 per second removes about 3.4 mA. The true value is `unknown` and depends on
  the real entry cost, which IDF does not state as a single constant. This is the
  largest screen-off opportunity that does not touch radio policy.
- **Risk:** Medium-low. Button and touch polling becomes 20 to 50 ms, blind
  remote shutter latency grows by up to that period, and the UI request queue
  and `Scan::processPendingCallbacks()` are serviced less often. The PM1
  watchdog feed stays at 1 s, so the 45 s timeout is unaffected. LVGL timers at
  250 ms and above are unaffected.
- **Effort:** Small to medium. One delay calculation plus a state check.
- **Required test:** Simulator scenarios for `screen-off-30s`,
  `blind-remote-shutter`, `connected-idle-30s`, and a modal open with the panel
  off. Host tests for the UI request queue. Hardware: measure input-to-shutter
  latency at 5, 20, and 50 ms with a camera, confirm double-click and long-press
  still work, and run the analyzer on the screen-off state.
- **Citation:** `src/FurbleUI.cpp:10177-10218`, `src/main.cpp:286-297`,
  `plans/158:168-173`, IDF `sleep_modes.c:152-174`.

### OS-02 Lengthen the control task queue timeout

- **Change:** Raise the 50 ms `xQueueReceive` timeout when the state machine is
  idle, for example to 250 ms.
- **Current behavior:** 50 ms (`src/FurbleControl.cpp:379-386`), which is 20
  wakes per second used for `reapZombieTargets()`, `sampleAdaptivePower()`, and
  liveness checks. Commands themselves wake the queue immediately.
- **Evidence:** inferred (code-read). `sampleAdaptivePower()` is rate limited to 5 s
  (`src/FurbleControl.cpp:22`, `1149`), and the zombie reclaim deadline is a
  timestamp, so the tick is a service cadence, not a correctness deadline.
- **Estimated saving:** Low. A task wake and context switch is on the order of
  tens of microseconds, so 20 to 4 wakes per second is well below 0.1 mA. The
  value is fewer scheduler events inside the 5 ms UI cadence, not a changed
  sleep count.
- **Risk:** Low. Zombie reclaim and liveness observation become up to 250 ms
  coarser. A connect command still wakes the queue immediately.
- **Effort:** Small.
- **Required test:** `control-disconnect`, `control-teardown-wedge`,
  `control-zombie-cancel`, `control-interleave`, and the simulator connect and
  reconnect scenarios. Confirm the reclaim deadline still fires within its
  documented bound.
- **Citation:** `src/FurbleControl.cpp:22`, `379-386`, `1149`,
  `src/FurbleControl.h:88`, `98`.

### OS-03 Lengthen the per-target queue timeout

- **Change:** Raise the 50 ms target `xQueueReceive` timeout to 250 ms, or move
  `maybeSetIdle()` and `updateConnStats()` to an explicit timer.
- **Current behavior:** 20 wakes per second per connected camera
  (`src/FurbleControl.cpp:100-107`, `116-198`). At the nine-camera maximum that
  is 180 wakes per second. The timeout is deliberately reused as the idle
  profile timer and statistics sampler.
- **Evidence:** inferred (code-read). The idle profile threshold is 10 s
  (`lib/furble/Camera.h:578`) and the statistics interval is 1 s
  (`lib/furble/Camera.h:580`), so 250 ms satisfies both.
- **Estimated saving:** Low to moderate in the nine-camera case. At 180 wakes per
  second, tens of microseconds each is a few milliseconds per second, about 0.1
  to 0.4 mA at 40.2 mA. In the single-camera case it is below noise.
- **Risk:** Low. Idle profile application can be up to 250 ms later, and the
  first statistics sample can be up to 250 ms later. Shutter commands still wake
  the queue immediately.
- **Effort:** Small.
- **Required test:** `control-interleave`, `connect-mid-drop`, the multiconnect
  simulator scenario, and a hardware shutter latency run with 1 and 9 cameras.
  Confirm `CONN_SAVER` still reaches the idle interval after 10 s.
- **Citation:** `src/FurbleControl.cpp:100-198`, `lib/furble/Camera.h:578-580`.

### OS-04 Block the GPS task indefinitely when GPS is disabled

- **Change:** Replace the 100 ms `vTaskDelay` in the disabled branch with a
  blocking queue receive that the existing `wakeGPSService()` event can cancel.
- **Current behavior:** `GPS::task()` releases the lock and delays 100 ms when
  `m_Enabled` is false (`src/FurbleGPS.cpp:212-220`). The task is created when
  the UI constructs `GPS::getInstance()` even if GPS is disabled, so this runs
  at 10 wakes per second for the life of the device.
- **Evidence:** inferred (code-read). `enable()` and `disable()` already set `m_Enabled`
  false and call `wakeGPSService()` before taking the service mutex
  (`src/FurbleGPS.cpp:319-324`, `1942-1946`), so the wake path exists.
- **Estimated saving:** 10 wakes per second removed. With the UI loop at 5 ms
  this does not change the sleep count, but it removes scheduler work and one
  task from the idle wake set. Below 0.1 mA, and free.
- **Risk:** Low. A lost wake event would leave GPS disabled until the next
  settings change, but the queue is also woken by UART events and the disable
  path sends the event before parking.
- **Effort:** Small.
- **Required test:** `gps-power-cycle`, `gps-power-profiler`, `gps-hold`, and the
  simulator GPS standby and settings scenarios. Toggle GPS on and off 100 times
  and confirm the task always resumes.
- **Citation:** `src/FurbleGPS.cpp:209-260`, `319-324`, `1942-1946`.

### OS-05 Reduce the GPS `cycleWait` base cadence

- **Change:** When a burst prediction exists, wait until the next deadline
  instead of polling every 100 ms. Keep the floor only for states that need it.
- **Current behavior:** `cycleWait()` starts at 100 ms and takes the minimum of
  that and the next state deadline (`src/FurbleGPS.cpp:525-592`). In `WAITING`
  and `STANDBY` with a prediction seconds away, the task still wakes 10 times
  per second.
- **Evidence:** inferred (code-read). The function already computes the exact deadlines,
  and UART data wakes the queue independently of the timeout.
- **Estimated saving:** 10 to 8 fewer wakes per second in the GPS-on state. Again
  below the 5 ms UI cadence, so no sleep count change until OS-01 lands. After
  OS-01 it becomes relevant.
- **Risk:** Medium-low. Configuration command timeouts and ACK polling run on
  this cadence; a 500 ms wait can delay a `$PCAS` retry by up to 500 ms. Fix
  freshness is unaffected because the deadline is still honored.
- **Effort:** Small to medium. State machine review.
- **Required test:** `gps-power-cycle`, `gps-power-profiler`, `gps-casic`, the
  `gps-standby-wake` and `gps-duty-standby-cycle` scenarios, and a hardware NMEA
  checksum-failure run with GPS duty enabled. Confirm configuration commands
  still complete and fix age stays under 30 s.
- **Citation:** `src/FurbleGPS.cpp:525-592`, `include/FurbleGPS.h:353-354`.

### OS-06 Companion service 1 s poll

- **Change:** Leave as is, or move to an event-driven wait if the power team
  wants the last wake removed.
- **Current behavior:** 1 wake per second while companion is enabled, with two
  notification calls (`src/FurbleCompanion.cpp:424-456`).
- **Evidence:** inferred (code-read).
- **Estimated saving:** Below the analyzer noise in every scenario that has a
  display or a 5 ms UI loop. `unknown` and not worth a dedicated change.
- **Risk:** Low.
- **Effort:** Small.
- **Required test:** `companion-gatt` and the companion simulator scenarios.
- **Citation:** `src/FurbleCompanion.cpp:424-456`.

### OS-07 Rate-limit the M5PM1 button read

- **Change:** Poll the PMIC button state at 20 to 50 ms instead of every UI loop
  iteration.
- **Current behavior:** `Platform::update()` is called at the top of every UI
  loop, and on the StickS3 it performs `m_M5PM1.btnGetState()` over I2C each
  time (`src/FurblePlatform.cpp:747-758`). That is about 200 I2C transactions
  per second, and it also defeats the PM1 I2C idle sleep that
  `M5PM1_REG_I2C_CFG.SLP_TO` provides (`plans/26:210-216`).
- **Evidence:** inferred (code-read, arithmetic). A 2-byte I2C read at 400 kHz is
  roughly 50 to 100 us including overhead, so 200 per second is 10 to 20 ms per
  second of bus and CPU time, about 0.4 to 0.8 mA at 40.2 mA active. The PM1
  quiescent difference is `unknown`; the published L1 figure is 52 uA
  (`tools/power-model/board-currents.yaml:387-390`), but that is a state figure,
  not the delta caused by polling.
- **Risk:** Medium-low. Button press latency and double-click detection must be
  revalidated. The PM1 watchdog feed stays at 1 s.
- **Effort:** Small.
- **Required test:** Button click, double click, long press, and PMIC recovery
  tests (`pmic-recovery`), plus a hardware click-latency run. Measure the
  StickS3 screen-off current with 5, 20, and 50 ms polling.
- **Citation:** `src/FurblePlatform.cpp:747-758`, `plans/26:210-216`,
  `tools/power-model/board-currents.yaml:387-390`.

### OS-08 Lengthen the M5PM1 watchdog feed period

- **Change:** Feed every 5 s instead of every 1 s, keeping the 45 s timeout.
- **Current behavior:** One `wdtFeed()` I2C write per second
  (`src/FurblePlatform.cpp:198-212`, `include/FurbleWatchdog.h:12-17`).
- **Evidence:** inferred (code-read, kconfig). The shared static assertion requires
  the timeout to be at least three feed periods, and 45 s allows 15 s
  (`include/FurbleWatchdog.h:16-17`). The PM1 I2C idle sleep timeout is not
  documented in the tree; plan 26 asks for the `SLP_TO` read and it is still
  owed (`plans/26:210-216`, `531-539`).
- **Estimated saving:** Tens of microamps at most, bounded by the PM1 sleep
  state. `unknown`.
- **Risk:** Low. The reset detection latency is unchanged because the timeout is
  unchanged; only the feed cadence changes.
- **Effort:** Small.
- **Required test:** `sim-watchdog`, the `watchdog-stall` scenario, and a
  hardware watchdog expiry run. Read `SLP_TO` before and after.
- **Citation:** `src/FurblePlatform.cpp:198-212`, `include/FurbleWatchdog.h:12-17`,
  `plans/26:210-216`, `531-539`.

### OS-09 Put the IMU into a lower power configuration

- **Change:** When the IMU setting is on, configure the BMI270 for accel-only low
  power or suspend the gyro when no diagnostics page needs it.
- **Current behavior:** `M5Unified` enables the IMU during `M5.begin()`
  (`src/FurblePlatform.cpp:38-45`). The tree's own comment says M5Unified writes
  `PWR_CONF = 0x00` and never enables advanced power save
  (`src/FurbleIMUMotionBMI270.cpp:60-70`). The gyro is only read on the
  diagnostics page (`src/FurbleUI.cpp:9063-9108`); the level and motion paths
  use the accelerometer.
- **Evidence:** inferred (code-read, datasheet). BMI270 normal accel plus gyro is
  685 uA, accel-only low power is 4 to 10 uA, suspend is 3.5 uA
  (`tools/power-model/board-currents.yaml:407-427`).
- **Estimated saving:** Up to about 0.67 mA while the IMU setting is on. The
  default is off (`src/FurbleSettings.cpp:643-664`), so this affects only users
  who enabled the IMU.
- **Risk:** Medium. M5Unified owns the sensor initialization, and the feature
  engine and wake path must keep working. The diagnostics page would need to
  re-enable the gyro or show it unavailable.
- **Effort:** Medium. Direct register writes plus vendor interaction checks.
- **Required test:** `imu-motion-encoding`, the gesture and level simulator
  scenarios, the diagnostics page, and a hardware IMU current measurement with
  the IMU setting on.
- **Citation:** `src/FurbleIMUMotionBMI270.cpp:56-70`, `src/FurbleUI.cpp:9063-9108`,
  `tools/power-model/board-currents.yaml:407-427`.

## Locks

### LOCK-01 `SLEEP_CONN` default and the connection lock

- **Change:** Decide whether `SLEEP_CONN` should default on for the StickS3, or
  whether Battery Saver remains the only way to get it. The mechanism already
  exists.
- **Current behavior:** `SLEEP_CONN` defaults false (`src/FurbleSettings.cpp:643-664`).
  `Control::setState()` acquires `NO_LIGHT_SLEEP` on the transition to
  `STATE_ACTIVE` when `Settings::sleepConnEffective()` is false, and releases it
  on the way out (`src/FurbleControl.cpp:1118-1134`). Battery Saver forces it on
  on the StickS3 (`include/FurbleBatterySaver.h:22-39`).
- **Evidence:** inferred (code-read), modeled (plan 98), measured (plans/00, different conditions). Plan 98
  models connected with the screen off at 43.6 mA with the lock held and 3.61 mA
  with it released (`plans/98:138-150`). The 3.3 mA connected idle floor in
  `plans/00` is `measured`, but under different link and display
  conditions, so it does not validate the model.
- **Estimated saving:** About 40 mA in the connected and screen-off state, by
  the model. This is the single largest battery-life number in the audit.
- **Risk:** Medium to high per vendor. Modem sleep plus automatic light sleep
  adds wake latency; a camera with a short supervision timeout can drop. Only
  Fujifilm hardware is available for soak. The GPS burst lock is StickS3 only,
  which is why the setting is gated there (`plans/77:118-125`).
- **Effort:** Policy change is one line; the soak is the work.
- **Required test:** The plan 111 protocol: 10 minute states 3 and 4 with an
  external analyzer, three repeats, plus a 30 minute connected soak per vendor.
  Measure first-shutter latency after a 30 s idle gap. Reject on any reconnect
  increase or missed notification.
- **Citation:** `src/FurbleControl.cpp:1118-1134`, `src/FurbleSettings.cpp:787-797`,
  `include/FurbleBatterySaver.h:22-39`, `plans/98:138-150`, `plans/111:29-86`.

### LOCK-02 GPS burst lock window

- **Change:** Rework the GPS duty policy so the lock is held only for the real
  burst and the task sleeps until the next deadline.
- **Current behavior:** The S3 holds `NO_LIGHT_SLEEP` during acquisition, each
  predicted burst, resync, and the degraded probe, and releases it in `WAITING`,
  `STANDBY`, `RAIL_OFF`, and `DEGRADED` (`src/FurbleGPS.cpp:485-501`,
  `525-592`). The default `GPS_DUTY` is 0 and the default `GPS_POWER` is always
  on (`src/FurbleSettings.cpp:591-605`). The window starts 50 ms before the
  predicted burst and closes after a 75 ms UART gap.
- **Evidence:** inferred (code-read), modeled (plan 98). Plan 98 models GPS duty at
  21.5 mA against 23.0 mA always-on because the wake dwell eats most of the
  interval (`plans/98:269-288`). Plan 15 records the 5 s `MIN_WAKE_WAIT_MS` and
  the 15 s duty cap (`plans/15:129-169`).
- **Estimated saving:** Up to about 19 mA of the 23 mA GPS budget if the duty
  policy really ducks, by the model. The lock change alone is not the win; the
  interval and cached-fix policy are.
- **Risk:** Medium. A window that is too narrow drops the start of every NMEA
  burst and geotagging degrades quietly. Fix age must stay under 30 s
  (`include/FurbleGPS.h:354`).
- **Effort:** Medium.
- **Required test:** `gps-power-cycle`, `gps-power-profiler`, `gps-casic`,
  `gps-hold`, the standby and duty scenarios, and a hardware NMEA checksum run
  with the Unit GPS v1.1. Confirm `$PCAS12` standby current on the bench before
  trusting the model.
- **Citation:** `src/FurbleGPS.cpp:485-501`, `525-592`,
  `include/FurbleGPS.h:353-354`, `plans/15:129-169`, `plans/98:269-288`.

### LOCK-03 Scope the display APB lock per board

- **Change:** Hold `ESP_PM_APB_FREQ_MAX` only where the backlight is actually
  APB-clocked. On the AXP192 boards the backlight is a voltage-dimmed PMIC rail,
  so the lock can be dropped while the panel is on. On the StickS3, move the
  LEDC timer to the RC_FAST clock so the PWM can keep running through light
  sleep and the lock can be dropped there too.
- **Current behavior:** `UI::UI()` acquires the lock unconditionally, before
  LVGL starts (`src/FurbleUI.cpp:371-374`). It is released only in
  `sleepDisplay()` and reacquired in `wakeDisplay()`
  (`src/FurbleUI.cpp:1316-1318`, `1333-1335`). Dimming keeps the lock, which is
  why the audit's dimmed case is 50.6 mA with only 10.4 mA of display
  (`plans/98:240-258`).
- **Evidence:** inferred (code-read), modeled (plan 98). Plan 12 records that M5GFX
  cuts LDO2 on StickC and StickC Plus and DCDC3 on Core2, so those backlights
  are PMIC rails, not LEDC (`plans/12:116-124`). The StickS3 backlight is LEDC
  on GPIO38 (`tools/power-model/board-currents.yaml:369-377`). A held
  `APB_FREQ_MAX` lock maps to `PM_MODE_APB_MAX` and blocks light sleep (IDF
  `pm_impl.c:228-241`, `555-568`).
- **Estimated saving:** `modeled`: screen-on menu idle is 81.2 mA with
  40.2 mA of MCU and 41.0 mA of display. If the MCU can light sleep while the
  panel stays on, the MCU term collapses toward the light sleep floor. That is a
  potential 30 to 40 mA on every screen-on state, on every board where the lock
  is not needed. The exact value is `unknown`.
- **Risk:** High on the StickS3: the LEDC flicker class is the hard-won trap,
  and M5GFX owns the LEDC channel, so the clock source change needs a supported
  API or a carefully scoped timer reconfiguration after display init. Changing
  the source can change PWM frequency and brightness. Medium on the AXP192
  boards: the SPI panel bus and M5GFX timing must be checked for tearing and
  brightness steps. The IDF SPI master driver takes its own APB lock during a
  transaction, but M5GFX's exact bus path is not verified in this tree.
- **Effort:** Small board-conditional change for the AXP192 boards. Medium to
  large for the StickS3 LEDC clock change.
- **Required test:** Per board, panel on, dim, off, and wake. Check flicker,
  tearing, brightness steps, and the 120 ms SLPIN to SLPOUT dwell. Confirm the
  APB lock count returns to zero when the panel sleeps. Run the analyzer on menu
  idle and dim. On the StickS3, run the LEDC clock change with a 30 minute
  display soak and a camera shutter run.
- **Citation:** `src/FurbleUI.cpp:371-374`, `1316-1318`, `1333-1335`,
  `plans/12:116-124`, `plans/98:240-258`, IDF `pm_impl.c:228-241`, `555-568`,
  IDF `esp_driver_spi/src/gpspi/spi_master.c:1281-1290`, IDF
  `components/hal/esp32s3/include/hal/ledc_ll.h:30-40`,
  `components/esp_driver_ledc/include/driver/ledc.h:70-77`.

## Logging

### LOG-01 Demote per-transition power lock logs

- **Change:** Move the `ESP_LOGI` calls in `Power::acquire()` and
  `Power::release()` to `ESP_LOGD`, or remove the per-transition line and keep
  only aggregate counters.
- **Current behavior:** Every lock acquire and release logs a line at INFO
  (`src/FurblePower.cpp:81-83`, `122-124`). The GPS burst cycle can produce
  several transitions per second, the display produces two per sleep and wake,
  and the control lock produces two per connection. The log is emitted while
  `m_StatsMutex` is held.
- **Evidence:** inferred (code-read). Release builds compile at
  `CONFIG_LOG_DEFAULT_LEVEL=3` and `CONFIG_LOG_MAXIMUM_LEVEL=3`
  (`sdkconfig.m5stick-s3:2582-2594`), so INFO is printed and DEBUG is compiled
  out. The DEBUG form would cost nothing in release.
- **Estimated saving:** Small and `unknown`. It removes UART and USB
  Serial/JTAG traffic and string formatting on every lock transition, and it
  removes a blocking log from under the stats mutex. Expect well under 0.1 mA
  average; the value is less jitter and less console noise more than milliamps.
- **Risk:** Low. The release diagnostics lose per-transition lines but keep the
  counters exposed by `power stats` (`src/FurbleConsole.cpp:1399-1421`). Debug
  builds still compile the lines.
- **Effort:** Small.
- **Required test:** Build all boards. Run the `console-commands` host test and
  the simulator power scenarios. Confirm `power stats` still reports counts and
  held time.
- **Citation:** `src/FurblePower.cpp:54-124`, `src/FurbleConsole.cpp:1399-1421`,
  `sdkconfig.m5stick-s3:2582-2594`.

### LOG-02 Audit recurring INFO sites

- **Change:** Review INFO logs that can fire on a recurring path and demote the
  ones that are not support-relevant.
- **Current behavior:** The obvious recurring sites are the power lock lines
  above, GPS configuration commands (`src/FurbleGPS.cpp:995`, `1062`), and
  vendor connection progress. Shutter and focus logs are event-driven and
  useful (`src/FurbleControl.cpp:137-160`). Library logs from NimBLE and WiFi are
  at the sdkconfig default.
- **Evidence:** inferred (code-read). No log-rate measurement was made.
- **Estimated saving:** Small and `unknown`. Mostly boot, connect, and
  configuration time, not steady-state idle.
- **Risk:** Low. Losing a support log is the only cost.
- **Effort:** Small.
- **Required test:** Capture the log for boot, scan, connect, shutter, GPS
  configuration, and a 10 minute idle run on the StickS3. Count lines per state
  and confirm the retained lines still tell the support story.
- **Citation:** `src/FurbleGPS.cpp:995`, `1062`, `src/FurbleControl.cpp:137-160`,
  `sdkconfig.m5stick-s3:2582-2594`.

### LOG-03 Change the release default log level, keeping INFO available

- **Change:** Set `CONFIG_LOG_DEFAULT_LEVEL_WARN=y` and
  `CONFIG_LOG_MAXIMUM_LEVEL_INFO=y` in release configs, so INFO strings stay
  compiled but are not printed by default. A diagnostic build or a console
  `log * info` can raise the level at runtime.
- **Current behavior:** Default INFO and maximum INFO
  (`sdkconfig.m5stick-s3:2582-2594`, `sdkconfig.m5stick-c:2152-2157`). Every
  `ESP_LOGI` prints. The debug environments compile verbose local levels, but
  the runtime default is still INFO (`platformio.ini:88`, `102`, `116`, `133`,
  `145`, `157`).
- **Evidence:** inferred (kconfig). IDF says the default level limits what is compiled,
  and the maximum level is the highest that can be selected at runtime
  (IDF `log/Kconfig.level:1-77`). Setting default WARN and maximum INFO is the
  supported combination that keeps INFO in the binary.
- **Estimated saving:** `unknown`. Stops INFO UART and USB Serial/JTAG
  output at runtime, which is mostly a boot and connect cost. The code size is
  unchanged in this variant. A second step, maximum WARN, would also remove the
  strings and code but removes the runtime raise path.
- **Risk:** Medium. Field diagnostics become quieter, and release builds have no
  console to raise the level. A diagnostic artifact or a documented build flag
  is needed before this becomes a release default.
- **Effort:** Small config plus a support decision.
- **Required test:** Build all boards. Capture boot-to-console time and the log
  line count in boot, connect, and idle. On a console build, verify
  `log * info` restores the lines. Run the full host and simulator suites.
- **Citation:** `sdkconfig.m5stick-s3:2582-2594`, `sdkconfig.m5stick-c:2152-2157`,
  IDF `log/Kconfig.level:1-77`, `src/FurbleConsole.cpp:2433-2463`.

## Console and USB serial

### CON-01 Disable the USB Serial/JTAG secondary console in battery releases

- **Change:** Set `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` and
  `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n` in the StickS3 release config, keeping
  the secondary console in the debug environment.
- **Current behavior:** The StickS3 release config has UART0 as the primary
  console and USB Serial/JTAG as the secondary
  (`sdkconfig.m5stick-s3:2265-2280`). The secondary selects
  `USJ_ENABLE_USB_SERIAL_JTAG=y` (`sdkconfig.m5stick-s3:1820-1821`, IDF
  `esp_system/Kconfig:318-322`). Every log line is mirrored to both.
- **Evidence:** inferred (kconfig). IDF says the USB Serial/JTAG module is on by default
  after power-on, and disabling it when it is not used turns off its clock and
  saves power (IDF `esp_driver_usb_serial_jtag/Kconfig:4-12`). It also says the
  module cannot work under light sleep (same file, `14-27`).
- **Estimated saving:** `unknown`. The module clock and PHY current is not
  in the board current table. It is one of the residual nibbles plan 98 lists at
  the floor (`plans/98:364-367`). The secondary also makes USB-attached runs
  active-only, so removing it makes USB-attached measurement possible. That is
  an indirect but real benefit.
- **Risk:** Medium. The native USB port stops providing logs in release, which
  is a support regression. Flashing through the ROM downloader still works, and
  the web installer is unaffected. Debug builds keep the console.
- **Effort:** Small config change plus documentation.
- **Required test:** Build the release and debug S3 envs. Confirm release has no
  USJ console and still flashes, boots, and reconnects over USB in download
  mode. Measure with an analyzer, on battery, with and without the secondary.
  Confirm `USJ_NO_AUTO_LS` no longer applies when the module is disabled.
- **Citation:** `sdkconfig.m5stick-s3:1820-1821`, `2265-2280`, IDF
  `esp_driver_usb_serial_jtag/Kconfig:4-27`, `esp_system/Kconfig:318-322`,
  `plans/98:364-367`.

### CON-02 The Waveshare release inherits the developer console

- **Change:** Stop inheriting `-DFURBLE_CONSOLE=1 -DFURBLE_USB_CONSOLE=1` into
  `env:waveshare-s3-eth`, or make the inheritance explicit and documented.
- **Current behavior:** `env:waveshare-s3-eth` uses
  `extends = env:esp32-s3-headless` and repeats the headless build flags
  (`platformio.ini:58-70`), which include `-DFURBLE_CONSOLE=1` and
  `-DFURBLE_USB_CONSOLE=1` (`platformio.ini:62`). The release artifact therefore
  contains the console task, the USB Serial/JTAG driver, and the console
  commands.
- **Evidence:** inferred (code-read). `FurbleConsole.cpp` is entirely behind
  `FURBLE_CONSOLE` (`src/FurbleConsole.cpp:3`, `2957`), and the console task is
  created at 8 KB stack and priority 2 (`src/FurbleConsole.cpp:68-71`,
  `2920-2926`).
- **Estimated saving:** None for battery life. The board is PoE or USB powered.
  The value is removing a debug surface and a 10 Hz task from a release artifact.
- **Risk:** Low to medium. If the headless hub is expected to keep a serial
  console in the field, removing it is a product decision.
- **Effort:** Small build change and documentation.
- **Required test:** Build both envs. Confirm the release artifact has no console
  task and that the documented hub diagnostics still work. Update
  `docs/console-commands.md` if the release console is removed.
- **Citation:** `platformio.ini:58-70`, `src/FurbleConsole.cpp:3`, `68-71`,
  `2920-2926`.

### CON-04 Lengthen the console task read timeout

- **Change:** Raise the 100 ms console read timeout to 1 s when idle.
- **Current behavior:** `readByte()` waits 100 ms for a byte
  (`src/FurbleConsole.cpp:2848-2853`), and the task loops on it
  (`src/FurbleConsole.cpp:2867-2904`). That is 10 wakes per second in console
  builds.
- **Evidence:** inferred (code-read).
- **Estimated saving:** Negligible. Console builds are developer builds and are
  not used for power claims. This is hygiene, not a battery item.
- **Risk:** Low. Console input latency grows by up to 1 s only when idle; the
  first byte wakes the driver.
- **Effort:** Small.
- **Required test:** `console-commands` host test, a hardware console session,
  and the `power log` cadence check (the task calls `powerLogTick()` before and
  after the read, so a 1 s timeout still meets a 1 s log interval only
  approximately; keep the timeout at or below the `power log` interval).
- **Citation:** `src/FurbleConsole.cpp:2848-2853`, `2867-2904`.

### Console and USB cost summary

- **CON-03, ESP32 console APB lock.** The ESP32 console path holds
  `ESP_PM_APB_FREQ_MAX` for the whole console lifetime
  (`src/FurbleConsole.cpp:2833-2844`). It defeats light sleep entirely. This is
  deliberate and developer-only. Do not copy it into release and do not use a
  console build for a power claim.
- **CON-05, measurement tooling.** The S3 console path uses the USB Serial/JTAG
  driver (`src/FurbleConsole.cpp:2823-2827`). With a host attached, the USJ lock
  keeps the chip out of light sleep. With no host attached, the console task
  still polls the driver every 100 ms. `power stats` and `power log` are
  measurement tools (`src/FurbleConsole.cpp:1399-1421`, `1490-1550`). They add
  output and CPU work on the console task. Use them to correlate state, not as
  the current measurement.
- **CON-06, USB measurement constraint.** The release S3 builds are not free of
  this cost: they keep the secondary USB Serial/JTAG console (CON-01) and
  therefore the USJ module. The release Waveshare build inherits the full
  developer console (CON-02). Any USB-attached run is an active-current run.

## Checked, no action

These were examined and do not justify a power change on the current evidence.

| Item | Finding | Citation |
|---|---|---|
| Bluetooth modem sleep | S3 uses `MODEM_SLEEP_MODE_1`, main XTAL LPCLK, and MAC/BB power down. ESP32 boards use `MODEM_SLEEP_MODE_ORIG` on the main XTAL. This is the correct DFS-compatible set. | `sdkconfig.m5stick-s3:1452-1456`, `2086`, `sdkconfig.m5stick-c:1077-1080`, `plans/07:50-62` |
| Main XTAL powered during light sleep | With the main XTAL as the BLE low power clock, the XTAL must stay powered. Disabling it would break the connected sleep clock. The plan 07 text that calls it meaningful only for the external 32 kHz path does not match the committed S3 config, but the committed config is the functional one. | `sdkconfig.m5stick-s3:1454-1456`, `plans/07:96-114` |
| External 32.768 kHz crystal | Not fitted on the StickS3. The 230 uA BLE light sleep mode is impossible. This was established by the boot log in plan 00. | `plans/00:186`, `tools/power-model/README.md:62-70` |
| GPS UART clock | S3 uses `UART_SCLK_XTAL`; ESP32 uses `UART_SCLK_REF_TICK`. This is the documented DFS-safe configuration. | `src/FurbleGPS.cpp:167-174` |
| RMT IR clock | `RMT_CLK_SRC_DEFAULT` is safe because the IDF RMT driver holds `ESP_PM_APB_FREQ_MAX` between `rmt_enable` and `rmt_disable`, and the code always disables. | `src/FurbleIR.cpp:215-278`, `src/CLAUDE.md:58-64` |
| I2S speaker clock | The IDF I2S driver holds the APB lock while the channel is enabled, and `M5.Speaker.end()` closes the amplifier between patterns. | `src/FurbleFeedback.cpp:14-20`, `166-170`, `330-352` |
| Speaker, vibration, LED | All three are bracketed on demand and shut down between events. No idle cost found. | `src/FurbleFeedback.cpp:150-170`, `320-360` |
| IR and SD tasks | Both block on a queue with `portMAX_DELAY`; no periodic polling. | `src/FurbleIR.cpp:201-208`, `src/FurbleSD.cpp:744-751` |
| MQTT task | Waits on a task notification with a computed next-deadline timeout, not a fixed poll. | `src/FurbleMQTT.cpp:306-333` |
| Disconnect waits | The waits use 20 ms `vTaskDelay` slices and feed the watchdog; no busy spin. | `src/FurbleControl.cpp:771-778`, `844-852`, `src/FurbleControl.h:98` |
| Manual light sleep | No `esp_light_sleep_start()` call exists. Only `esp_pm` automatic sleep is used. | `plans/06:70-80` |
| Task watchdog and brownout | The 5 s task watchdog does not panic, and brownout detection is at level 7. These are safety settings, not power levers. Removing them would trade a reset or brownout risk for unmeasured microamps. | `sdkconfig.m5stick-s3:2281-2287`, `3819-3835` |
| PM_SLP_IRAM_OPT and CPU power-down | Both are already enabled and are worth about 310 us per sleep and about 650 uA respectively. Turning them off would be a regression. | `sdkconfig.m5stick-s3:2114-2118`, IDF `esp_pm/Kconfig:62-70`, `122-135` |
| Charging `NO_LIGHT_SLEEP` lock | The device is on external power while charging, so removing the lock cannot improve battery runtime. It could reduce heat, but that is a separate thermal question. | `src/FurbleUI.cpp:9906-9914`, `plans/13:160-170` |
| `CPU_FREQ_MAX` lock | Created but never acquired. Removing it saves a lock object and a diagnostic slot, not energy. | `include/FurblePower.h:22`, `src/FurblePower.cpp:16-31` |
| Task stacks and priorities | The sizes are 4 to 16 KB per task. Stack size does not change runtime current. Priorities change latency and preemption, not the idle floor. No power lever was found. | `src/main.cpp:364`, `src/FurbleControl.cpp:977`, `src/FurbleGPS.cpp:185`, `src/FurbleConsole.cpp:68-71`, `src/FurbleCompanion.cpp:128`, `src/FurbleMQTT.cpp:220` |
| Busy waits | No unbounded CPU spin was found. Registration waits poll with `vTaskDelay` (10 to 100 ms), disconnect waits use 20 ms slices, and the scan start probe is host-only. | `lib/furble/Fujifilm.cpp:120-145`, `lib/furble/FujifilmSecure.cpp:456-470`, `lib/furble/Scan.cpp:489-496` |
| Flash power-down on S3 | `CONFIG_ESP_SLEEP_POWER_DOWN_FLASH` cannot be enabled with PSRAM. The 700 us flash-ready wait would also be paid on every sleep. | IDF `esp_hw_support/Kconfig:80-96`, `sleep_modes.c:152-157` |
| 2M PHY, MTU, scan duty, connection interval | These are radio airtime levers, not SoC or OS levers. They belong in a radio audit. The connection interval still matters to sleep residency, which is why it appears under LOCK-01. | `plans/08`, `plans/10`, `plans/11` |

## Ranked lists

### Safe now

These are low risk, reversible, and do not require a hardware current
measurement to decide. Each still needs its test.

1. **LOG-01** Demote per-transition power lock logs. No behavior change, less
   console noise, removes a blocking log under the stats mutex.
2. **SOC-03** Apply the stored CPU frequency at `Platform` init. `Settings` is
   already up first. Removes a boot window at the wrong frequency.
3. **SOC-04** Enable `CONFIG_PM_RTOS_IDLE_OPT`. About 180 bytes of IRAM for a
   documented 20 us per sleep.
4. **OS-04** Block the GPS task when disabled. The wake event already exists.
5. **OS-02** Raise the idle control queue timeout. Commands still wake the queue.
6. **OS-03** Raise the per-target queue timeout. The 10 s idle threshold and 1 s
   stats interval both tolerate 250 ms.
7. **CON-04** Raise the console read timeout. Developer builds only.
8. **SOC-09** Enable tickless idle on the Waveshare config, or stop claiming
   light sleep there. Mains board, configuration honesty.
9. **CON-02** Stop inheriting the developer console into the Waveshare release,
   or document it deliberately. No battery effect.

### Needs measurement, ranked by expected value and uncertainty

1. **LOCK-03** Scope the display APB lock. Largest screen-on opportunity. Start
   with the AXP192 boards, where the backlight is a PMIC rail, then decide
   whether the StickS3 LEDC clock change is worth the flicker risk.
2. **OS-01** Lengthen the screen-off UI loop. Largest screen-off opportunity that
   does not touch radio policy. The plan 98 screen-off model cannot rank it
   because it charges no wake cost.
3. **SOC-06** Raise the RTC calibration interval. A documented per-sleep cost
   multiplied by about 200 sleeps per second. Easy to A/B.
4. **LOCK-01** Decide the `SLEEP_CONN` default. The mechanism is already merged
   and the model says about 40 mA. The missing evidence is per-vendor link
   stability and a calibrated analyzer run.
5. **OS-07** Rate-limit the M5PM1 button read. A plausible 0.4 to 0.8 mA
   arithmetic estimate on the S3, entirely unmeasured, with a low-risk fix.
6. **CON-01** Disable the USB Serial/JTAG secondary console in battery releases.
   Direct Kconfig statement that disabling saves power, plus it makes
   USB-attached measurement possible.
7. **SOC-05** Enable GPIO sleep disable. The Kconfig says 200 to 300 uA, which is
   large relative to the light sleep floor, but the wake source risk is high.
8. **LOG-03** Default the release log level to WARN while keeping INFO compiled.
   A support-policy decision with a small, measurable boot and connect effect.
9. **SOC-02** Cap the CPU maximum at 80 MHz, or in the battery profile. Measure
   the frame time trade before choosing.
10. **OS-09** Lower the IMU power when the IMU setting is on. About 0.67 mA, but
    only for users who enabled it.
11. **SOC-01** Lower the DFS minimum. Small, but the only remaining clock lever
    after the APB lock is scoped.
12. **OS-08** Lengthen the PM1 watchdog feed. Tens of microamps at most; read
    `SLP_TO` first.
13. **SOC-10** Flash power-down on the ESP32 boards. Only after the loop is
    lengthened, because the 700 us flash-ready wait dominates at 5 ms.
14. **SOC-07** Lower `CONFIG_FREERTOS_HZ`. Small expected win and real timer
    risk. Close as rejected if the analyzer cannot see it.
15. **SOC-08** Sweep the idle-before-sleep threshold. Do it after OS-01, because
    the loop period changes the answer.
16. **SOC-13** WiFi sleep IRAM option. Only for users who enable WiFi and MQTT.
17. **SOC-12** Deep sleep between intervalometer frames. Large win, but a
    feature-sized project with a watchdog conflict. It is ranked here rather
    than at the top because it is not a tuning change.
18. **OS-05** Reduce the GPS wait cadence. Low value on its own, more relevant
    after OS-01.

### Rejected, or no action on current evidence

| Item | Why rejected |
|---|---|
| External 32.768 kHz crystal and the 230 uA BLE mode | The crystal is absent. This is measured in plan 00. |
| Disable `BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP` | The main XTAL is the BLE low power clock. Disabling it would break the connected sleep clock. |
| Change the modem sleep mode | `MODE_1` on S3 and `ORIG` on ESP32 are the supported DFS-compatible modes. |
| Manual `esp_light_sleep_start()` | It drops the BLE link. The product requires automatic light sleep through `esp_pm`. |
| Remove the charging `NO_LIGHT_SLEEP` lock for battery life | The device is on external power while charging. No battery runtime is gained. |
| Remove the unused `CPU_FREQ_MAX` lock | No energy effect. Diagnostic cleanup only. |
| Shrink task stacks | Saves RAM, not current. No measured power link. |
| Change task priorities for power | No clear mechanism. Priorities change latency and preemption, not idle current. |
| Disable the task watchdog, interrupt watchdog, or brownout detector | Safety settings. The power saved is not measurable and the failure mode is worse. |
| Disable `PM_SLP_IRAM_OPT` or `PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP` | Both are already on and both reduce current. Turning them off is a regression. |
| Enable flash power-down on the S3 | Not possible with PSRAM. The wake wait also dominates at a 5 ms loop. |
| Disable `CONFIG_PM_PROFILING` or `PM_TRACE` in release | `PM_PROFILING` is already off in release. `PM_TRACE` is off everywhere. No action. |
| Remove WiFi or Bluetooth from the build | Both are runtime-off by default, so they cost no idle current. Removing them is a feature decision, not a power fix. |
| Reduce `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` | Unused connection slots cost no idle current. The cost appears only when the user actually connects that many cameras. |
| 2M PHY, MTU, passive scan, TX power | Radio airtime items, outside this SoC and OS scope. They belong in a radio audit. |
| SD card power gating | The card is mounted only when GPX logging is enabled, which is a user opt-in. The mount and unmount cost would dominate a per-point gate. |
| Console APB lock removal on ESP32 | The lock is required for UART receive while the console is in use, and the console is developer-only. Do not copy it into release. |

## Measurement plan

No number in this document is a hardware measurement. The following plan is the
minimum needed to turn any of the ranked items into a decision. It follows the
plan 111 protocol and the plan 158 parity rule.

### Instrument and boundary

- Use a calibrated inline power analyzer at one declared electrical boundary,
  preferably the complete device input with charging disabled.
- Record analyzer model, firmware version, range, sample rate, supply voltage,
  cable, battery presence, ambient conditions, and whether USB data is attached.
- Retain raw samples, not only a displayed average.
- Flash a release build for current. Use a debug build only for state and lock
  counters, and never compare its current to the release build.
- Do not use the native USB port on the StickS3 for a power run. It holds
  `NO_LIGHT_SLEEP` while connected. Use battery power and an external analyzer,
  or UART0 logging through an external bridge if the board exposes it.
- Do not use a console build for a power run. The ESP32 console holds the APB
  lock for its lifetime.

### States

| # | State | Setup | Plan 98 model | Evidence class |
|---|---|---|---|---|
| 1 | Menu idle, screen on | Disconnected, menu page, default brightness | 81.2 mA | `modeled` |
| 2 | Connected idle, screen on | Fujifilm connected, idle | 84.5 mA | `modeled` |
| 3 | Connected, screen off, `SLEEP_CONN` off | Blank the panel | 43.6 mA | `modeled` |
| 4 | Connected, screen off, `SLEEP_CONN` on | Same, Battery Saver on | 3.61 mA | `modeled` |
| 5 | Connected plus GPS active | Unit GPS v1.1 attached, tracking | 107.5 mA | `modeled` |
| 6 | Screen off, disconnected | Idle, panel blank | 0.25 mA | `modeled`, known incomplete |
| 7 | Dimmed | Inactivity dim, default brightness | 50.6 mA | `modeled` |
| 8 | GPS duty standby | `$PCAS12` duty, 5 to 15 s | 102.7 mA | `modeled` |

State 6 is the one this note most strongly disputes. It assumes zero CPU time
for the 5 ms UI loop and no wake cost. Measure it first.

### Procedure

1. Freeze the firmware identity: commit, sdkconfig hashes, build flags, and
   partition table. Record them with the raw analyzer trace.
2. Reach the state and let it settle for at least 60 s.
3. Capture at least 10 minutes per state, three repeats, on the same battery
   voltage band. Alternate A and B runs to remove drift.
4. Annotate every display, GPS, reconnect, shutter, and connection parameter
   transition. Reject a run that silently changes state.
5. Compute mean, median, p95, and a confidence interval over the same window.
   The plan 63 `compare.py` 10 percent threshold is a compatibility policy, not
   a calibrated noise band. Do not use it as the measurement tolerance.
6. Record lock residency and wake counts separately from current. `power stats`
   and `perf tasks` are state evidence, not current measurements. They require a
   `CONFIG_PM_PROFILING` and `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` build,
   which is the debug environment, and that environment is not a current
   reference.

### Candidate A/B tests

| Candidate | A | B | Primary metric | Secondary checks |
|---|---|---|---|---|
| LOCK-03 | Current APB lock on all boards | Lock only where the backlight needs it | State 1 and 7 current on StickC Plus, Core2, StickS3 | Flicker, tearing, brightness, wake |
| OS-01 | 5 ms UI loop | 20 and 50 ms when the panel is off | State 6 and state 4 current | Input latency, shutter p95, watchdog |
| SOC-06 | Calibration interval 1 | 8 and 32 | State 6 current | Wake lateness, BLE soak, GPS soak |
| LOCK-01 | `SLEEP_CONN` off | On | State 3 and 4 current | Reconnects, missed notifications, shutter p95 |
| OS-07 | PM1 poll every UI loop | 20 and 50 ms | State 6 current on StickS3 | Click latency, double click, PMIC recovery |
| CON-01 | USJ secondary on | Off | State 6 current on StickS3 | Flashing, boot, no USJ console |
| SOC-05 | GPIO sleep disable off | On with selective exemptions | State 6 current | Every wake source, NMEA, 24 hour soak |
| LOG-03 | Default INFO | Default WARN, max INFO | Boot and connect energy, state 2 current | Log line count, support story |
| SOC-02 | Max 160 MHz | Max 80 MHz | State 1 current and frame time | Shutter p95, BLE setup |
| OS-09 | IMU normal mode | Accel-only low power | IMU-on idle current | Level page, motion wake, diagnostics |
| SOC-01 | Min 40 MHz | Min 20 and 10 MHz | State 6 current with GPS on | BLE soak, GPS parse, wake latency |
| OS-08 | Feed every 1 s | Every 5 s | State 6 current on StickS3 | `SLP_TO` read, watchdog expiry |
| SOC-10 | Flash PD off | On, ESP32 boards | State 6 current on StickC Plus | Cold boot, NVS, OTA, wake time |

### Acceptance

A candidate is accepted only when all of the following hold:

- The measured delta is larger than the combined repeat noise at the declared
  boundary.
- No regression in shutter round trip p95, boot-to-console, wake-to-first-frame,
  minimum internal heap, or task stack high-water margin beyond the agreed
  budget.
- No increase in reconnects, missed notifications, lost NMEA sentences, or
  watchdog resets.
- The exact test commands and raw traces are retained with the change.
- The doc and the model are updated with the measured constant, not with a
  plan estimate.

If the delta is not visible, close the item as rejected and record the null
result. A null result is a valid result.

## Open items not resolved here

- The real S3 light sleep entry and exit cost on this board is unknown. IDF
  provides 382 us as a sleep-out constant, but entry cost, cache restore, and
  PSRAM behavior are not measured.
- The PM1 `SLP_TO` value and the PM1 current while polled at 200 Hz are unknown.
  Plan 26 still owes the register read.
- The StickS3 backlight current at 32/255 duty is an estimate. The board current
  table flags it as one of the two highest-impact missing calibrations
  (`tools/power-model/README.md:283-286`).
- The `$PCAS12` in-circuit standby current is an estimate, not a measurement.
- The M5GFX SPI bus path under DFS is not verified in this tree. The IDF SPI
  master driver takes its own APB lock during a transaction, but whether M5GFX
  uses that driver for the panel is not established here.
- PSRAM retention current in light sleep is not in any table used by this
  project.
- The effect of a lower `CONFIG_FREERTOS_HZ` on NimBLE host timers is not
  established.
- The cost of the USB Serial/JTAG module when no host is attached is not in the
  board current table.

## References

### Repository

- `src/FurblePower.cpp`, `include/FurblePower.h`: lock creation, counting, and
  `esp_pm_configure()`.
- `src/FurblePlatform.cpp`, `include/FurblePlatform.h`: boot frequency, PMIC
  polling, watchdog feed, power off.
- `src/FurbleControl.cpp`, `include/FurbleControl.h`: connection lock, control
  task, target tasks.
- `src/FurbleGPS.cpp`, `include/FurbleGPS.h`: GPS task, burst window lock, duty
  state machine.
- `src/FurbleUI.cpp`, `include/FurbleUI.h`: UI loop, display states, APB lock,
  timers, charging lock.
- `src/FurbleConsole.cpp`, `include/FurbleConsole.h`: console transport, power
  stats, power log, task.
- `src/FurbleCompanion.cpp`, `src/FurbleMQTT.cpp`, `src/FurbleSD.cpp`,
  `src/FurbleIR.cpp`, `src/FurbleFeedback.cpp`: task and peripheral wake paths.
- `sdkconfig.m5stick-s3`, `sdkconfig.m5stick-c`, `sdkconfig.m5stack-core2`,
  `sdkconfig.esp32-s3-headless`, `sdkconfig.waveshare-s3-eth`: power, log,
  console, watchdog, FreeRTOS, and Bluetooth settings.
- `platformio.ini`: environments and debug flags.
- `tools/power-model/board-currents.yaml`, `tools/power-model/README.md`:
  per-board current inputs and their confidence tags.
- `plans/00-hardware-experiments.md`: measured 3.3 mA connected idle floor,
  14 uA power off, 107.7 s GPS cold refix, no 32 kHz crystal.
- `plans/01-cpu-freq-setting.md`: CPU frequency setting and its risks.
- `plans/06-power-module.md`: lock design and the no-manual-sleep rule.
- `plans/07-ble-sleep.md`: modem sleep and per-board floors.
- `plans/12-display-off.md`: display states, APB lock, PMIC backlight paths.
- `plans/13-auto-off-low-batt.md`: charging lock and power-off policy.
- `plans/15-gps-power.md`: GPS burst lock, duty limits, UART clock.
- `plans/19-interval-deep-sleep.md`: deep sleep between interval frames.
- `plans/26-pm1-watchdog.md`: PM1 watchdog, feed cadence, `SLP_TO`, deep sleep
  conflict.
- `plans/27-usb-console.md`: console transport and USJ light sleep behavior.
- `plans/77-battery-saver-profile.md`: the opt-in power bundle.
- `plans/96-wifi-bt-ble-hardening.md`: WiFi, BLE, and coexistence audit.
- `plans/98-power-optimization-audit.md`: the model table and ranked actions.
- `plans/98b-power-followup.md`: bounded GPS degraded retry.
- `plans/111-power-recommendations.md`: the external measurement protocol.
- `plans/125-esp-optimization-program.md`,
  `plans/126-esp-optimization-audit.md`: the wider optimization program.
- `plans/158-sim-scheduler-parity.md`: why the simulator power numbers are
  advisory.

### ESP-IDF and silicon

- ESP-IDF power management, `esp_pm_configure`, lock types, and automatic light
  sleep: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/power_management.html
- ESP-IDF sleep modes, light sleep wake sources, and flash power-down:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/sleep_modes.html
- ESP-IDF `esp_pm` Kconfig:
  `components/esp_pm/Kconfig` in the pinned framework.
- ESP-IDF `esp_pm` implementation, `get_lowest_allowed_mode()` and
  `vApplicationSleep()`: `components/esp_pm/pm_impl.c`.
- ESP-IDF sleep implementation, S3 sleep-out overhead and RTC calibration:
  `components/esp_hw_support/sleep_modes.c`.
- ESP-IDF FreeRTOS Kconfig, tick rate and tickless idle:
  `components/freertos/Kconfig`.
- ESP-IDF USB Serial/JTAG Kconfig:
  `components/esp_driver_usb_serial_jtag/Kconfig`.
- ESP-IDF console Kconfig: `components/esp_system/Kconfig`.
- ESP-IDF log level Kconfig: `components/log/Kconfig.level`.
- ESP-IDF WiFi Kconfig, `ESP_WIFI_SLP_IRAM_OPT`:
  `components/esp_wifi/Kconfig`.
- ESP-IDF S3 clock configuration, valid CPU frequencies and XTAL dividers:
  `components/esp_hw_support/port/esp32s3/rtc_clk.c`.
- ESP32-S3 Series Datasheet v2.2, Tables 5-8 to 5-10, active, WAITI, light
  sleep, and deep sleep current:
  https://documentation.espressif.com/esp32-s3_datasheet_en.pdf
- ESP32 Series Datasheet v5.3, Tables 4-2 and 5-4, active and light sleep
  current:
  https://documentation.espressif.com/esp32_datasheet_en.pdf
- Bosch BMI270 datasheet BST-BMI270-DS000-08, normal, low power, and suspend
  current:
  https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf
- M5Stack StickS3 product page, PM1 states and battery:
  https://docs.m5stack.com/en/core/StickS3
- M5PM1 driver, watchdog and register map:
  https://github.com/m5stack/M5PM1
- M5Stack StickS3 low power page, PM1 timer and I2C idle sleep:
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- M5Stack Unit GPS v1.1, 5 V 31.64 mA and no backup supply:
  https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1
- Espressif NimBLE power save example, connected idle and light sleep figures:
  https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/power_save
- ST7789V datasheet v1.3, panel normal and sleep-in current:
  https://newhavendisplay.com/content/datasheets/ST7789V.pdf
- ST7735S datasheet v1.1, panel normal and sleep-in current:
  https://cdn.sparkfun.com/assets/9/0/2/5/8/ST7735S_v1.1.pdf
- AXP192 datasheet, English translation, off mode and per-block quiescent
  figures:
  http://images.shoutwiki.com/mindworks/8/8b/2020_infrasonic_wildfire_detector_APX192_Enhanced_Single_Cell_datasheet_en.pdf
- IP5306 datasheet v1.01, standby and 45 mA auto-off threshold:
  https://www.laskakit.cz/user/related_files/ip5306.pdf
