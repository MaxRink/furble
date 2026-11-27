#include "hardware_model.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "FurbleCompanionMemory.h"
#include "FurbleIP5306.h"
#include "FurbleMQTTMemory.h"
#include "clock.h"
#include "crash_log_sim.h"
#include "driver.h"
#include "heap_model.h"

namespace {

constexpr const char *LOG_TAG = "hardware";

// Whether the board carries a calendar RTC chip at all. This is a separate
// question from Board::rtcBatteryBacked, which is whether that chip keeps time
// with the main cell removed.
//
// StickC, StickC Plus and StickC Plus2 carry a BM8563 on the SYS_INT net. The
// Core2 and Tough carry the BM8563 the Core2 row of docs/power/board-power.md
// lists. The StickS3 and the Core have none: docs/console-commands.md records
// that M5Unified finds no calendar RTC chip on either. The headless S3 has no
// display, PMIC or RTC, and the Waveshare StampS3 adds none.
struct rtc_fact_t {
  const char *profile;
  bool rtc;
  const char *source;
};

constexpr rtc_fact_t RTC_FACTS[] = {
    {"stickc",           true,  "BM8563, docs/power/board-power.md"     },
    {"stickc-plus",      true,  "BM8563, docs/power/board-power.md"     },
    {"stickc-plus2",     true,  "BM8563 alarm wake, src/FurbleBoard.cpp"},
    {"sticks3",          false, "no RTC chip, docs/console-commands.md" },
    {"core-4mb",         false, "no RTC chip, docs/console-commands.md" },
    {"core-16mb",        false, "no RTC chip, docs/console-commands.md" },
    {"core2",            true,  "BM8563, docs/power/board-power.md"     },
    {"tough",            true,  "BM8563, docs/power/board-power.md"     },
    {"headless-s3",      false, "no RTC, docs/power/board-power.md"     },
    {"waveshare-s3-eth", false, "no RTC on the StampS3"                 },
};

/*
 * One modelled board row.
 *
 * `ir`, `sd`, `audio` and `touch` are the capabilities the firmware reads, not
 * raw silicon inventory, so a scenario can hold the row against
 * `Board::profile()`:
 *   ir     an IR emitter the firmware can drive (a GPIO and a DIO-mode flash)
 *   sd     a microSD slot on the display SPI bus that the firmware can mount
 *   audio  a speaker or amplifier the firmware drives for feedback sound
 *   touch  a touch panel
 * `rtc` is a calendar chip at all and `rtcBatteryBacked` is whether it keeps
 * time with the main cell out; RTC_FACTS is the per-row evidence for the first
 * and docs/power/board-power.md section 2 for the second. A buzzer without a
 * speaker path is not `audio`.
 *
 * `powerOff` names the modelled shutdown path and `ext5vShared` records that
 * the Port A GPS rail and the external 5 V net are the same switch.
 */
struct board_caps_t {
  const char *mcu;
  uint32_t flashKb;
  const char *pmic;
  const char *gauge;
  const char *psram;
  const char *usb;
  const char *display;
  int width;
  int height;
  bool touch;
  bool battery;
  bool wifi;
  bool ethernet;
  bool ble;
  bool ir;
  bool sd;
  bool audio;
  bool rtc;
  // The board profile's own columns, mirroring include/FurbleBoard.h in
  // src/FurbleBoard.cpp: whether a cell keeps the calendar through a power
  // off, and which wake path the board has.
  bool rtcBatteryBacked;
  const char *timedWake;  // "none", "pmic-timer" or "rtc-alarm"
  bool vbus;
  bool poe;
  // The columns the firmware row carries that the model now checks beside its
  // own capabilities, so `board-row-*` can pin each pair to one literal.
  bool rtcOnBus;
  bool canPowerOff;
  bool ledActiveLow;
  bool irMarkLow;
  const char *gpsPinSource;  // "board-support", "profile" or "none"
  int8_t gpsRxPin;           // PROFILE source, -1 when none
  int8_t gpsTxPin;           // PROFILE source, -1 when none
  const char *powerOff;
  // The platform can cut power: `Profile::powerOffPath`. It is a different
  // question from `Profile::canPowerOff`, the UI policy gate, and the Core is
  // the board where the two disagree (plan 354, #497).
  bool powerOffPath;
  bool ext5vShared;
};

const board_caps_t STICKC = {.mcu = "esp32",
                             .flashKb = 4096,
                             .pmic = "axp192",
                             .gauge = "voltage_current",
                             .psram = "absent",
                             .usb = "uart",
                             .display = "80x160",
                             .width = 80,
                             .height = 160,
                             .touch = false,
                             .battery = true,
                             .wifi = true,
                             .ethernet = false,
                             .ble = true,
                             .ir = true,
                             .sd = false,
                             .audio = false,
                             .rtc = true,
                             .rtcBatteryBacked = true,
                             .timedWake = "none",
                             .vbus = true,
                             .poe = false,
                             .rtcOnBus = true,
                             .canPowerOff = true,
                             .ledActiveLow = true,
                             .irMarkLow = true,
                             .gpsPinSource = "board-support",
                             .gpsRxPin = -1,
                             .gpsTxPin = -1,
                             .powerOff = "axp192-off",
                             .powerOffPath = true,
                             .ext5vShared = false};
const board_caps_t STICKC_PLUS = {.mcu = "esp32",
                                  .flashKb = 4096,
                                  .pmic = "axp192",
                                  .gauge = "voltage_current",
                                  .psram = "absent",
                                  .usb = "uart",
                                  .display = "135x240",
                                  .width = 135,
                                  .height = 240,
                                  .touch = false,
                                  .battery = true,
                                  .wifi = true,
                                  .ethernet = false,
                                  .ble = true,
                                  .ir = true,
                                  .sd = false,
                                  .audio = true,
                                  .rtc = true,
                                  .rtcBatteryBacked = true,
                                  // The BM8563 INT reaches the AXP192 power key, so the
                                  // alarm is the wake path (plan 350).
                                  .timedWake = "rtc-alarm",
                                  .vbus = true,
                                  .poe = false,
                                  .rtcOnBus = true,
                                  .canPowerOff = true,
                                  .ledActiveLow = true,
                                  .irMarkLow = true,
                                  .gpsPinSource = "board-support",
                                  .gpsRxPin = -1,
                                  .gpsTxPin = -1,
                                  .powerOff = "axp192-off",
                                  .powerOffPath = true,
                                  .ext5vShared = false};
const board_caps_t STICKC_PLUS2 = {.mcu = "esp32",
                                   .flashKb = 4096,
                                   .pmic = "none",
                                   .gauge = "voltage",
                                   .psram = "absent",
                                   .usb = "uart",
                                   .display = "135x240",
                                   .width = 135,
                                   .height = 240,
                                   .touch = false,
                                   .battery = true,
                                   .wifi = true,
                                   .ethernet = false,
                                   .ble = true,
                                   .ir = true,
                                   .sd = false,
                                   .audio = true,
                                   .rtc = true,
                                   // The BM8563 stays powered across the HOLD power-off
                                   // the timed wake uses, so the calendar is the retention
                                   // mechanism (plan 350).
                                   .rtcBatteryBacked = true,
                                   .timedWake = "rtc-alarm",
                                   .vbus = true,
                                   .poe = false,
                                   .rtcOnBus = true,
                                   .canPowerOff = true,
                                   .ledActiveLow = false,
                                   .irMarkLow = false,
                                   .gpsPinSource = "board-support",
                                   .gpsRxPin = -1,
                                   .gpsTxPin = -1,
                                   .powerOff = "hold-latch",
                                   .powerOffPath = true,
                                   .ext5vShared = false};
const board_caps_t STICKS3 = {.mcu = "esp32s3",
                              .flashKb = 8192,
                              .pmic = "m5pm1",
                              .gauge = "voltage_charge",
                              .psram = "octal",
                              .usb = "serial_jtag",
                              .display = "135x240",
                              .width = 135,
                              .height = 240,
                              .touch = false,
                              .battery = true,
                              .wifi = true,
                              .ethernet = false,
                              .ble = true,
                              .ir = true,
                              .sd = false,
                              .audio = true,
                              // The board has no RTC chip. Keep this in step with
                              // RTC_FACTS; hwRtcCapability() asserts the pair.
                              .rtc = false,
                              .rtcBatteryBacked = false,
                              .timedWake = "pmic-timer",
                              .vbus = true,
                              .poe = false,
                              .rtcOnBus = false,
                              .canPowerOff = true,
                              .ledActiveLow = false,
                              .irMarkLow = false,
                              .gpsPinSource = "board-support",
                              .gpsRxPin = -1,
                              .gpsTxPin = -1,
                              .powerOff = "pm1-off",
                              .powerOffPath = true,
                              .ext5vShared = false};
const board_caps_t CORE = {.mcu = "esp32",
                           .flashKb = 4096,
                           .pmic = "ip5306",
                           .gauge = "four_level",
                           .psram = "absent",
                           .usb = "uart",
                           .display = "320x240",
                           .width = 320,
                           .height = 240,
                           .touch = false,
                           .battery = true,
                           .wifi = true,
                           .ethernet = false,
                           .ble = true,
                           .ir = false,
                           .sd = true,
                           .audio = true,
                           .rtc = false,
                           .rtcBatteryBacked = false,
                           .timedWake = "none",
                           .vbus = true,
                           .poe = false,
                           .rtcOnBus = false,
                           .canPowerOff = false,
                           .ledActiveLow = false,
                           .irMarkLow = false,
                           .gpsPinSource = "none",
                           .gpsRxPin = -1,
                           .gpsTxPin = -1,
                           .powerOff = "ip5306-light-load",
                           .powerOffPath = true,
                           .ext5vShared = false};
// Current Basic units ship 16 MB, while the committed `m5stack-core` build is
// configured for 4 MB (sdkconfig.m5stack-core, partitions_two_ota_large.csv).
// Both rows exist so a scenario names the flash assumption it means.
const board_caps_t CORE_16MB = {.mcu = "esp32",
                                .flashKb = 16384,
                                .pmic = "ip5306",
                                .gauge = "four_level",
                                .psram = "absent",
                                .usb = "uart",
                                .display = "320x240",
                                .width = 320,
                                .height = 240,
                                .touch = false,
                                .battery = true,
                                .wifi = true,
                                .ethernet = false,
                                .ble = true,
                                .ir = false,
                                .sd = true,
                                .audio = true,
                                .rtc = false,
                                .rtcBatteryBacked = false,
                                .timedWake = "none",
                                .vbus = true,
                                .poe = false,
                                .rtcOnBus = false,
                                .canPowerOff = false,
                                .ledActiveLow = false,
                                .irMarkLow = false,
                                .gpsPinSource = "none",
                                .gpsRxPin = -1,
                                .gpsTxPin = -1,
                                .powerOff = "ip5306-light-load",
                                .powerOffPath = true,
                                .ext5vShared = false};
const board_caps_t CORE2 = {.mcu = "esp32",
                            .flashKb = 16384,
                            .pmic = "axp192",
                            .gauge = "voltage_current",
                            .psram = "quad",
                            .usb = "uart",
                            .display = "320x240",
                            .width = 320,
                            .height = 240,
                            .touch = true,
                            .battery = true,
                            .wifi = true,
                            .ethernet = false,
                            .ble = true,
                            .ir = false,
                            .sd = true,
                            .audio = true,
                            .rtc = true,
                            .rtcBatteryBacked = true,
                            .timedWake = "none",
                            .vbus = true,
                            .poe = false,
                            .rtcOnBus = true,
                            .canPowerOff = true,
                            .ledActiveLow = false,
                            .irMarkLow = false,
                            .gpsPinSource = "board-support",
                            .gpsRxPin = -1,
                            .gpsTxPin = -1,
                            .powerOff = "axp192-off",
                            .powerOffPath = true,
                            .ext5vShared = true};
const board_caps_t TOUGH = {.mcu = "esp32",
                            .flashKb = 16384,
                            .pmic = "axp192",
                            .gauge = "voltage_current",
                            .psram = "quad",
                            .usb = "uart",
                            .display = "320x240",
                            .width = 320,
                            .height = 240,
                            .touch = true,
                            .battery = true,
                            .wifi = true,
                            .ethernet = false,
                            .ble = true,
                            .ir = false,
                            .sd = true,
                            .audio = true,
                            .rtc = true,
                            .rtcBatteryBacked = true,
                            .timedWake = "rtc-alarm",
                            .vbus = true,
                            .poe = false,
                            .rtcOnBus = true,
                            .canPowerOff = true,
                            .ledActiveLow = false,
                            .irMarkLow = false,
                            .gpsPinSource = "board-support",
                            .gpsRxPin = -1,
                            .gpsTxPin = -1,
                            .powerOff = "axp192-off",
                            .powerOffPath = true,
                            .ext5vShared = true};
const board_caps_t HEADLESS = {.mcu = "esp32s3",
                               .flashKb = 8192,
                               .pmic = "none",
                               .gauge = "none",
                               .psram = "octal",
                               .usb = "serial_jtag",
                               .display = "none",
                               .width = 0,
                               .height = 0,
                               .touch = false,
                               .battery = false,
                               .wifi = true,
                               .ethernet = false,
                               .ble = true,
                               .ir = false,
                               .sd = false,
                               .audio = false,
                               .rtc = false,
                               .rtcBatteryBacked = false,
                               .timedWake = "none",
                               .vbus = true,
                               .poe = false,
                               .rtcOnBus = false,
                               .canPowerOff = true,
                               .ledActiveLow = true,
                               .irMarkLow = false,
                               .gpsPinSource = "none",
                               .gpsRxPin = -1,
                               .gpsTxPin = -1,
                               .powerOff = "deep-sleep",
                               .powerOffPath = true,
                               .ext5vShared = false};
const board_caps_t WAVESHARE = {.mcu = "esp32s3",
                                .flashKb = 16384,
                                .pmic = "none",
                                .gauge = "none",
                                .psram = "octal",
                                .usb = "serial_jtag",
                                .display = "none",
                                .width = 0,
                                .height = 0,
                                .touch = false,
                                .battery = false,
                                .wifi = true,
                                .ethernet = true,
                                .ble = true,
                                .ir = false,
                                .sd = false,
                                .audio = false,
                                .rtc = false,
                                .rtcBatteryBacked = false,
                                .timedWake = "none",
                                .vbus = false,
                                .poe = false,
                                .rtcOnBus = false,
                                .canPowerOff = false,
                                .ledActiveLow = false,
                                .irMarkLow = false,
                                .gpsPinSource = "none",
                                .gpsRxPin = -1,
                                .gpsTxPin = -1,
                                .powerOff = "none",
                                .powerOffPath = false,
                                .ext5vShared = false};

// IP5306 light-load cutoff. The part drops its boost when the load stays below
// 45 mA for the window REG_SYS_CTL2 (0x02) bits 3:2 select, unless the "BOOST
// output normally open" bit is set.
//
// The register meanings and the bits 3:2 table live in include/FurbleIP5306.h,
// which the firmware also uses, so the model cannot drift from the shipped
// decode. M5Unified 0.2.20 writes 0x02 = 0x6C, whose bits 3:2 are 11, the 64 s
// window; the datasheet headline row's 32 s is the power-on default of field
// 01, which the firmware overwrites.
constexpr int IP5306_LIGHT_LOAD_MA = 45;
constexpr uint8_t IP5306_SYS_CTL2_INIT = 0x6C;
constexpr uint8_t IP5306_SYS_CTL0_INIT = 0x31;

/** The bits 3:2 selector of REG_SYS_CTL2, 0 to 3. */
constexpr uint8_t ip5306LightLoadField(uint8_t sysCtl2) {
  return static_cast<uint8_t>((sysCtl2 & Furble::IP5306::SYS_CTL2_LIGHT_LOAD_MASK) >> 2);
}

/** The window that byte selects, in milliseconds, from the firmware decoder. */
constexpr uint32_t ip5306LightLoadMs(uint8_t sysCtl2) {
  return static_cast<uint32_t>(Furble::IP5306::lightLoadSeconds(sysCtl2)) * 1000U;
}

struct pmic_state_t {
  std::string variant;
  std::string gauge;
  bool batteryPresent = true;
  bool charging = false;
  bool chargeEnabled = true;
  int batteryPercent = 80;
  int batteryMillivolts = 4000;
  int chargeCurrentMa = 0;
  bool watchdogEnabled = false;
  uint32_t watchdogFeeds = 0;
  bool downloadLocked = true;
  bool powerLatch = true;
  uint32_t brownouts = 0;
  // M5PM1 v1.0.6 register 0x06. One byte, so every field in it shares a
  // read-modify-write hazard with the others. The boot value is the manual's
  // reset byte 0x17 with the 5 V boost off, because the StickS3 firmware passes
  // output_power = false; hwStart() applies it and the constants below carry
  // the citations.
  uint8_t pwrCfg = 0x17;
  uint32_t pwrCfgLostUpdates = 0;
  bool pwrCfgStale = false;
  // The copy a stale write carries: the byte as of the last rail enable, not the
  // boot byte, because a read only goes stale if something read it while the
  // port was powered. Only the seed that asks for a stale write consumes it.
  uint8_t pwrCfgSnapshot = 0x17;
  bool displayOff = false;
  bool statusLedSaved = false;
  bool statusLedLevel = false;
  uint32_t watchdogCycles = 0;
  // What the modelled M5PM1 reports in PWR_SRC 0x04 bits 2:0, and whether the
  // register answers at all. The default is a battery-only board, which is the
  // field case the EXT_5V rail exists for. A scenario that models an external
  // 5 V source on the port seeds "port"; "unreadable" makes the read fail.
  std::string railSupply = "battery";
  // Modelled window one IR burst holds the EXT_5V rail, seeded by
  // `ir_burst_ms`. See hwIrBurstMs().
  uint32_t irBurstMs = 100;
  // Register bytes a scenario can read back. Only the AXP192/AXP2101 ADC,
  // rail and LDOIO registers and the IP5306 system-control bytes are modelled;
  // every other address reads as zero with a zero write count.
  uint8_t reg[256] = {};
  uint32_t regWrites[256] = {};
  // Writes the part cannot accept: an AXP192 register on an AXP2101 part, or
  // the reverse. Fail closed so a scenario can assert the guard ran.
  uint32_t violations = 0;
  // IP5306 REG_SYS_CTL0 bit 1, "BOOST output normally open". Clear after the
  // vendor init block, so the 45 mA light-load cutoff is armed.
  bool boostKeepOn = false;
  int loadMa = 60;
  // The window the timer runs for. It is the value the 0x02 field decodes to,
  // unless a scenario seeds one, either to shorten the virtual time or to
  // exercise another field value. lightLoadWindowSeeded records which, so a
  // scenario can prove the default came from the register byte.
  uint32_t lightLoadWindowMs = ip5306LightLoadMs(IP5306_SYS_CTL2_INIT);
  bool lightLoadWindowSeeded = false;
  // A scenario-selected REG_SYS_CTL2 byte, written after the vendor block in
  // hwStart(), so a scenario can decode a field value the vendor block does not
  // select.
  bool sysCtl2Seeded = false;
  uint8_t sysCtl2Seed = IP5306_SYS_CTL2_INIT;
  uint32_t lightLoadElapsedMs = 0;
  uint32_t lightLoadCuts = 0;
  bool panelOff = false;
  uint32_t lastTickMs = 0;
  bool ticked = false;
  // Port A 5 V. On Core2 and Tough the same switch feeds the external 5 V net,
  // so a GPS rail cut drops BUS_5V with it.
  bool gpsRail = false;
  bool ext5v = false;
  uint32_t ext5vDrops = 0;
  // Shutdown bookkeeping: what the modelled path did, not what was asked for.
  uint32_t powerOffAttempts = 0;
  uint32_t powerOffRefused = 0;
};

// The switched EXT 5 V output on the board. The M5PM1 owns the switch on the
// StickS3; the AXP and IP5306 boards model the same shared output.
struct rail_state_t {
  bool on = true;
  // The board cannot switch the rail, or the PMIC refuses a cut. Both keep the
  // rail on whatever a holder asks, which is what `seed gps_rail_fixed` and
  // `seed gps_rail_refused` model.
  bool fixed = false;
  bool refused = false;
  // No holder at boot: the firmware's railAcquire is what starts one, so a
  // scenario that seeds the GPS setting off sees an unowned port.
  bool gpsWants = false;
  bool irWants = false;
  bool irInFlight = false;
  uint32_t refusals = 0;
  uint32_t irTransmits = 0;
  uint32_t irFailures = 0;
  // Recomputed on every change so a query can return a stable string.
  std::string holders = "gps";
};

enum class power_state_t { ON, OFF, DOWNLOAD };
enum class usb_mode_t { APP, ROM, ESPTOOL };

struct power_button_t {
  power_state_t state = power_state_t::ON;
  uint32_t clicks = 0;
  uint32_t doubleClicks = 0;
  uint32_t longPresses = 0;
  uint32_t resets = 0;
  uint32_t offs = 0;
  uint32_t downloads = 0;
  uint32_t recoveries = 0;
};

struct usb_state_t {
  usb_mode_t mode = usb_mode_t::APP;
  bool hostOpen = false;
  bool dtr = false;
  bool rts = false;
  uint32_t romEntries = 0;
  uint32_t syncs = 0;
  uint32_t recoveries = 0;
};

struct wifi_state_t {
  std::string state = "disabled";
  std::string reason = "none";
  std::string ip = "192.0.2.10";
  std::string ssid = "sim-ap";
  int rssi = -55;
  uint32_t attempts = 0;
  uint32_t retryMs = 0;
  bool ntpEnabled = true;
  bool ntpSynced = false;
  int64_t ntpOffsetUs = 0;
};

struct net_state_t {
  std::string ethernet = "down";
  uint32_t linkUps = 0;
  uint32_t linkDowns = 0;
};

struct ble_state_t {
  std::string bond = "none";
  std::string auth = "idle";
  std::string gattCache = "valid";
  std::string companion = "off";
  uint32_t authTimeouts = 0;
  uint32_t reidentifyCount = 0;
  uint32_t reconnects = 0;
  uint32_t rejectedLinks = 0;
};

struct peripheral_state_t {
  std::string audio = "idle";
  std::string ir = "idle";
  std::string sd = "absent";
  uint32_t audioOvercurrents = 0;
  uint32_t irTimeouts = 0;
  uint32_t sdErrors = 0;
};

struct mqtt_model_state_t {
  bool enabled = false;
  size_t ownerStack = 10240;
  size_t workerStack = 4096;
  bool tls = false;
  bool expectOverflow = false;
  bool ha = false;
  size_t bufferBytes = 2048;
  std::string haState = "off";
  size_t haPeak = 0;
  std::string ownerGate = "open";
  std::string startGate = "open";
  std::string discoveryGate = "open";
  std::string haCache = "valid";
  uint32_t discoveryFragments = 0;
  size_t queueBytes = 0;
  std::string queueRegion = "none";
  size_t cameraBytes = 0;
  size_t mainStackBytes = 12288;
  // Bytes used: perf tasks left 3,020 of 12,288 bytes free, MQTT connected.
  size_t mainHighWater = 9268;
  std::string state = "off";
  bool overflow = false;
  size_t heapFreeAfter = 0;
};

// The modelled calendar chip. The register reading is an anchor plus the
// virtual clock, so a `wait` moves the clock the way wall time moves on a
// board. Nothing here replaces a firmware driver: TimeKeeper reads the same
// sample it reads from M5.Rtc, through its simulator hooks.
struct rtc_state_t {
  Furble::Sim::rtc_mode_t mode = Furble::Sim::rtc_mode_t::OFF;
  bool hasReading = false;
  bool voltLow = false;
  bool restored = false;  // a sidecar reading outranks the scenario seed
  uint64_t epochS = 0;    // register reading at anchorUs
  uint64_t anchorUs = 0;  // virtual clock when that reading was taken
};

constexpr const char *RTC_SIDECAR_MAGIC = "furble-sim-rtc";
constexpr unsigned RTC_SIDECAR_VERSION = 1;

struct rtc_sidecar_t {
  bool batteryBacked = false;
  bool hasReading = false;
  bool voltLow = false;
  uint64_t epochS = 0;
};

std::string rtcSidecarPath(void) {
  const char *explicitPath = std::getenv("FURBLE_SIM_RTC_STATE");
  if (explicitPath != nullptr && explicitPath[0] != '\0') {
    return explicitPath;
  }
  const char *prefs = std::getenv("FURBLE_SIM_PREFS");
  if (prefs == nullptr || prefs[0] == '\0') {
    return std::string();
  }
  return std::string(prefs) + ".rtc";
}

void discardRtcSidecar(void) {
  const std::string path = rtcSidecarPath();
  if (!path.empty()) {
    std::remove(path.c_str());
  }
}

void persistRtcSidecar(const rtc_state_t &rtc) {
  const std::string path = rtcSidecarPath();
  if (path.empty() || rtc.mode == Furble::Sim::rtc_mode_t::OFF) {
    return;
  }
  FILE *file = std::fopen(path.c_str(), "w");
  if (file == nullptr) {
    return;
  }
  std::fprintf(file, "%s %u %s %s %s %llu\n", RTC_SIDECAR_MAGIC, RTC_SIDECAR_VERSION,
               rtc.mode == Furble::Sim::rtc_mode_t::BACKED ? "backed" : "unbacked",
               rtc.hasReading ? "set" : "unset", rtc.voltLow ? "low" : "ok",
               static_cast<unsigned long long>(rtc.epochS));
  std::fclose(file);
}

bool readRtcSidecar(rtc_sidecar_t &out) {
  const std::string path = rtcSidecarPath();
  if (path.empty()) {
    return false;
  }
  FILE *file = std::fopen(path.c_str(), "r");
  if (file == nullptr) {
    return false;
  }
  char magic[32] = {};
  char backing[16] = {};
  char registers[16] = {};
  char supply[16] = {};
  unsigned version = 0;
  unsigned long long epochS = 0;
  const int fields = std::fscanf(file, "%31s %u %15s %15s %15s %llu", magic, &version, backing,
                                 registers, supply, &epochS);
  std::fclose(file);
  if ((fields != 6) || (std::strcmp(magic, RTC_SIDECAR_MAGIC) != 0)
      || (version != RTC_SIDECAR_VERSION)
      || ((std::strcmp(backing, "backed") != 0) && (std::strcmp(backing, "unbacked") != 0))
      || ((std::strcmp(registers, "set") != 0) && (std::strcmp(registers, "unset") != 0))
      || ((std::strcmp(supply, "low") != 0) && (std::strcmp(supply, "ok") != 0))) {
    return false;
  }
  out.batteryBacked = std::strcmp(backing, "backed") == 0;
  out.hasReading = std::strcmp(registers, "set") == 0;
  out.voltLow = std::strcmp(supply, "low") == 0;
  out.epochS = static_cast<uint64_t>(epochS);
  return true;
}

// A cold boot is a complete power loss: the cell did not keep the calendar
// either. Every other reset reason leaves the chip running.
bool coldBootReason(uint32_t reason) {
  return (reason == ESP_RST_POWERON) || (reason == ESP_RST_BROWNOUT)
         || (reason == ESP_RST_PWR_GLITCH);
}

uint64_t rtcReadingLocked(const rtc_state_t &rtc) {
  const uint64_t nowUs = Furble::Sim::clockMicros();
  const uint64_t elapsedUs = (nowUs >= rtc.anchorUs) ? (nowUs - rtc.anchorUs) : 0;
  return rtc.epochS + (elapsedUs / 1000000ULL);
}

struct hardware_state_t {
  std::mutex mutex;
  std::string profile;
  board_caps_t caps = STICKS3;
  pmic_state_t pmic;
  power_button_t power;
  usb_state_t usb;
  wifi_state_t wifi;
  net_state_t net;
  ble_state_t ble;
  peripheral_state_t peripheral;
  rail_state_t rail;
  mqtt_model_state_t mqtt;
  rtc_state_t rtc;
  uint64_t leakProbe = 0;
  // Blocks held by heap-squeeze, freed by heap-release.
  std::vector<uint64_t> squeeze;
  std::string bootDefaultsMarker = "current";
  bool bootSeedFail = false;
  uint32_t bootDefaultScans = 0;
  uint32_t bootSeedAttempts = 0;
  bool bootMarkerWritten = true;
  bool psramMemtest = true;
  std::string bootProfile = "debug";
  uint32_t resetToAppMs = 1300;
  std::string wifiFastConnect = "usable";
  std::string wifiAuth = "known";
  uint32_t wifiDirectFailures = 0;
  uint32_t wifiScans = 0;
  bool wifiMovedAp = false;
  std::string mqttStartGate = "open";
  std::string mqttDiscoveryGate = "open";
  std::string mqttHaCache = "valid";
  uint32_t mqttDiscoveryFragments = 0;
  size_t mqttDiscoveryPayload = 3005;
  bool companionEnabled = false;
  // The first enable creates the retained task and GATT database. A seeded
  // boot enable is already part of the seeded heap; a runtime first enable is
  // charged once and passes CompanionMemory::startGate first.
  bool companionResident = false;
  std::string companionGate = "none";
  std::string otaSlot = "ota_0";
  bool otaRollback = false;
  uint32_t otaUpdates = 0;
  uint32_t otaCommits = 0;
  std::string iconState = "idle";
  uint32_t resetReason = ESP_RST_POWERON;
  std::string resetReasonName = "poweron";
  bool psramExplicit = false;
  bool started = false;
  bool failClosed = false;
};

hardware_state_t &state(void) {
  static hardware_state_t instance;
  return instance;
}

const board_caps_t &capsFor(const std::string &profile) {
  if (profile == "stickc") {
    return STICKC;
  }
  if (profile == "stickc-plus") {
    return STICKC_PLUS;
  }
  if (profile == "stickc-plus2") {
    return STICKC_PLUS2;
  }
  if (profile == "core-4mb") {
    return CORE;
  }
  if (profile == "core-16mb") {
    return CORE_16MB;
  }
  if (profile == "core2") {
    return CORE2;
  }
  if (profile == "tough") {
    return TOUGH;
  }
  if (profile == "headless-s3") {
    return HEADLESS;
  }
  if (profile == "waveshare-s3-eth") {
    return WAVESHARE;
  }
  return STICKS3;
}

std::string defaultProfile(void) {
#if defined(FURBLE_M5STICKS3)
  return "sticks3";
#elif defined(FURBLE_M5STICKC_PLUS)
  return "stickc-plus";
#elif defined(FURBLE_M5STICKC)
  return "stickc";
#elif defined(FURBLE_M5COREX)
  return "core-4mb";
#else
  return "sticks3";
#endif
}

const rtc_fact_t *rtcFactFor(const std::string &profile) {
  for (const rtc_fact_t &fact : RTC_FACTS) {
    if (profile == fact.profile) {
      return &fact;
    }
  }
  return nullptr;
}

// M5PM1 v1.0.6 M5PM1.h register 0x06: [0] CHG_EN, [1] DCDC_EN, [2] LDO_EN,
// [3] BOOST_EN, [4] LED_CTRL. On this board BOOST_EN is the EXT 5 V output the
// Port A rail and the IR parts sit behind, and LED_CTRL is the status LED level
// the panel-off path saves and clears.
constexpr uint8_t PWR_CFG_CHG_EN = 1U << 0;
constexpr uint8_t PWR_CFG_DCDC_EN = 1U << 1;
constexpr uint8_t PWR_CFG_LDO_EN = 1U << 2;
constexpr uint8_t PWR_CFG_BOOST_EN = 1U << 3;
constexpr uint8_t PWR_CFG_LED_CTRL = 1U << 4;
// M5PM1-UM section 2 register (7), PWR_CFG 0x06, page 13 of 33, as recorded in
// docs/components/m5pm1.md: the chip resets this byte to 0x17, with the 5 V
// boost off. Charge, both regulated rails and the status LED are on.
constexpr uint8_t PWR_CFG_RESET =
    PWR_CFG_CHG_EN | PWR_CFG_DCDC_EN | PWR_CFG_LDO_EN | PWR_CFG_LED_CTRL;
// The board stays at the reset value at boot. M5Unified's own config_t
// output_power defaults to true (M5Unified.hpp) and M5Unified::_begin() calls
// Power.setExtOutput(cfg.output_power), which would set BOOST_EN, but the
// StickS3 firmware overrides it: Platform::init() passes output_power = false
// so the rail manager is the only thing that powers the port
// (src/FurblePlatform.cpp, FURBLE_HAS_M5PM1 branch, plans/364). The modelled
// boot byte is therefore the documented reset value, and BOOST_EN moves only
// when a holder asks for it.
static_assert(PWR_CFG_RESET == 0x17, "M5PM1-UM section 2 register (7) reset value");

/** The rail follows the one output bit, so a lost update moves it. */
void applyRailFromPwrCfgLocked(hardware_state_t &hw) {
  hw.rail.on = (hw.pmic.pwrCfg & PWR_CFG_BOOST_EN) != 0;
}

/**
 * Apply one masked write to PWR_CFG and return the new byte.
 *
 * `stale` models a read taken before another holder changed the register: the
 * write carries the old value of every bit outside its own mask, so a field the
 * caller never asked about changes anyway. That is the lost update the byte
 * layout makes possible, and the model counts it rather than hiding it.
 */
uint8_t pwrCfgWriteMaskedLocked(hardware_state_t &hw, uint8_t mask, uint8_t value, bool stale) {
  const uint8_t current = hw.pmic.pwrCfg;
  const uint8_t base = stale ? hw.pmic.pwrCfgSnapshot : current;
  const uint8_t next = static_cast<uint8_t>((base & static_cast<uint8_t>(~mask)) | (value & mask));
  if (next != static_cast<uint8_t>((current & static_cast<uint8_t>(~mask)) | (value & mask))) {
    hw.pmic.pwrCfgLostUpdates++;
    ESP_LOGW(LOG_TAG, "PWR_CFG lost update: 0x%02x -> 0x%02x for mask 0x%02x",
             static_cast<unsigned>(current), static_cast<unsigned>(next),
             static_cast<unsigned>(mask));
  }
  hw.pmic.pwrCfg = next;
  applyRailFromPwrCfgLocked(hw);
  return next;
}

/**
 * Record the PWR_CFG byte a reader would have latched before a rail change.
 *
 * A stale read only exists if something read the byte while the output was on,
 * so the snapshot moves when the rail manager switches the port on: that is the
 * byte a panel-off path would still be holding after the rail moves again. A
 * boot-time snapshot would model a read the firmware never makes.
 */
void latchPwrCfgSnapshotLocked(hardware_state_t &hw) {
  hw.pmic.pwrCfgSnapshot = hw.pmic.pwrCfg;
}

/** True when the model's PMIC is the one that owns PWR_CFG on this board. */
bool pwrCfgModelled(const hardware_state_t &hw) {
  return hw.pmic.variant == "m5pm1";
}

/**
 * Compare the selected profile's RTC capability with its verified row.
 *
 * Every profile used to say yes and nothing checked it, so a wrong answer cost
 * nothing until a scenario depended on it. A mismatch is a model defect: record
 * it, keep the app running so the scenario can assert the pair, and say which
 * row disagrees.
 */
bool rtcCapabilityMatchesLocked(hardware_state_t &hw) {
  const rtc_fact_t *fact = rtcFactFor(hw.profile);
  if (fact == nullptr) {
    return false;
  }
  if (hw.caps.rtc == fact->rtc) {
    return true;
  }
  hw.failClosed = true;
  ESP_LOGE(LOG_TAG, "SIM FAIL: profile %s RTC capability %s, board has %s (%s)", hw.profile.c_str(),
           hw.caps.rtc ? "yes" : "no", fact->rtc ? "one" : "none", fact->source);
  return false;
}

/** The Port A rail and, on Core2 and Tough, the external 5 V net behind it. */
void pmicSetGpsRailLocked(hardware_state_t &hw, bool on) {
  hw.pmic.gpsRail = on;
  if (!hw.caps.ext5vShared) {
    return;
  }
  if (hw.pmic.ext5v && !on) {
    hw.pmic.ext5vDrops++;
  }
  hw.pmic.ext5v = on;
}

/** Rebuild the holder list so a query returns a stable string. */
void refreshRailHoldersLocked(hardware_state_t &hw) {
  std::string holders;
  const auto add = [&holders](const char *name) {
    if (!holders.empty()) {
      holders += '+';
    }
    holders += name;
  };
  if (hw.rail.gpsWants) {
    add("gps");
  }
  if (hw.rail.irWants || hw.rail.irInFlight) {
    add("ir");
  }
  hw.rail.holders = holders.empty() ? std::string("none") : holders;
}

/**
 * One rail request on behalf of one holder. Caller holds the model lock.
 *
 * The board has one switched output behind more than one holder, so the rail is
 * on while any holder wants it. A holder that asks for a state it cannot have
 * gets a refusal in the read-back, which is what Platform::setRail returns to
 * the GPS driver. A stale PWR_CFG write can move the output bit afterwards, so
 * the read-back is always the byte and not the request.
 */
bool setRailLocked(hardware_state_t &hw, Furble::Sim::rail_holder_t holder, bool wanted) {
  if (holder == Furble::Sim::rail_holder_t::GPS) {
    hw.rail.gpsWants = wanted;
  } else {
    hw.rail.irWants = wanted;
  }
  refreshRailHoldersLocked(hw);

  const bool needed = hw.rail.gpsWants || hw.rail.irWants || hw.rail.irInFlight;
  const bool blocked = !needed && (hw.rail.fixed || hw.rail.refused);
  if (blocked) {
    hw.rail.refusals++;
    // The read-back is the rail, so the external 5 V net follows it here too.
    pmicSetGpsRailLocked(hw, hw.rail.on);
    return hw.rail.on;
  }

  const bool boostedBefore = (hw.pmic.pwrCfg & PWR_CFG_BOOST_EN) != 0;
  pwrCfgWriteMaskedLocked(hw, PWR_CFG_BOOST_EN, needed ? PWR_CFG_BOOST_EN : 0, false);
  if (!boostedBefore && hw.rail.on) {
    // A reader that latched the byte here holds the powered value, which is what
    // a panel-off path that read PWR_CFG before another writer moved it has.
    latchPwrCfgSnapshotLocked(hw);
  }
  // A request the board cannot honour is a refusal, whichever direction it
  // asked for, and the caller sees it in the read-back.
  if (hw.rail.on != wanted) {
    hw.rail.refusals++;
  }
  // On Core2 and Tough the same switch feeds the external 5 V net, so the net
  // follows the read-back rather than the request.
  pmicSetGpsRailLocked(hw, hw.rail.on);
  return hw.rail.on;
}

}  // namespace

namespace Furble::Sim {

bool hwSetRail(rail_holder_t holder, bool wanted) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return setRailLocked(state(), holder, wanted);
}

}  // namespace Furble::Sim

namespace {

/**
 * IR depends on the board's switched output where the two share one switch.
 *
 * On the M5PM1 boards EXT_5V feeds the Port A rail and the IR parts, so the rail
 * answers before the IR state. On the AXP and IP5306 boards the modelled rail is
 * the Port A supply and the IR emitter is its own GPIO, so it is independent.
 */
const char *irStateLocked(const hardware_state_t &hw) {
  if (!hw.caps.ir) {
    return "absent";
  }
  if (hw.rail.irInFlight) {
    return "transmitting";
  }
  if (hw.peripheral.ir == "timeout") {
    return "timeout";
  }
  if (pwrCfgModelled(hw) && !hw.rail.on) {
    return "unpowered";
  }
  return hw.peripheral.ir.c_str();
}

/** Begin one IR transfer window. Both ends sit behind the switched 5 V output. */
bool irStartLocked(hardware_state_t &hw) {
  const bool unpowered = pwrCfgModelled(hw) && !hw.rail.on;
  if (!hw.caps.ir || unpowered) {
    hw.rail.irFailures++;
    ESP_LOGW(LOG_TAG, "IR transfer refused: %s", !hw.caps.ir ? "no IR on this board" : "rail off");
    return false;
  }
  hw.rail.irInFlight = true;
  (void)setRailLocked(hw, Furble::Sim::rail_holder_t::IR, true);
  hw.rail.irTransmits++;
  return true;
}

bool irEndLocked(hardware_state_t &hw) {
  if (!hw.rail.irInFlight) {
    return false;
  }
  hw.rail.irInFlight = false;
  (void)setRailLocked(hw, Furble::Sim::rail_holder_t::IR, false);
  return true;
}

uint32_t reasonForName(const std::string &name) {
  if (name == "software") {
    return ESP_RST_SW;
  }
  if (name == "panic") {
    return ESP_RST_PANIC;
  }
  if (name == "int_wdt") {
    return ESP_RST_INT_WDT;
  }
  if (name == "task_wdt" || name == "tg1wdt") {
    return ESP_RST_TASK_WDT;
  }
  if (name == "other_wdt") {
    return ESP_RST_WDT;
  }
  if (name == "brownout") {
    return ESP_RST_BROWNOUT;
  }
  if (name == "usb_uart_chip_reset" || name == "usb") {
    return ESP_RST_USB;
  }
  if (name == "jtag") {
    return ESP_RST_JTAG;
  }
  if (name == "power_glitch") {
    return ESP_RST_PWR_GLITCH;
  }
  if (name == "deepsleep") {
    return ESP_RST_DEEPSLEEP;
  }
  return ESP_RST_POWERON;
}

const char *reasonName(uint32_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "other_wdt";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_USB:
      return "usb_uart_chip_reset";
    case ESP_RST_JTAG:
      return "jtag";
    case ESP_RST_PWR_GLITCH:
      return "power_glitch";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    default:
      return "poweron";
  }
}

void enterRomLocked(hardware_state_t &hw, const char *reason) {
  hw.usb.mode = usb_mode_t::ROM;
  hw.usb.romEntries++;
  hw.power.state = power_state_t::DOWNLOAD;
  hw.pmic.downloadLocked = false;
  ESP_LOGW(LOG_TAG, "%s; console and BLE are unavailable until a power-button reset", reason);
}

void recoverToAppLocked(hardware_state_t &hw, const char *reason) {
  hw.usb.mode = usb_mode_t::APP;
  hw.usb.recoveries++;
  hw.power.recoveries++;
  hw.power.state = power_state_t::ON;
  hw.resetReason = ESP_RST_POWERON;
  hw.resetReasonName = "poweron";
  ESP_LOGI(LOG_TAG, "%s; application console and BLE are available", reason);
}

void powerOnLocked(hardware_state_t &hw) {
  hw.power.state = power_state_t::ON;
  hw.resetReason = ESP_RST_POWERON;
  hw.resetReasonName = "poweron";
}

/*
 * Modelled PMIC register bytes.
 *
 * Only the addresses a scenario can hold against the vendor init block are
 * modelled. Each part reuses the same address for a different purpose, which
 * is why the meaning is part of the model:
 *
 *   0x12  AXP192 rail enables            AXP2101 BATFET control
 *   0x30  -                              AXP2101 ADC enable
 *   0x82  AXP192 ADC enable 1            AXP2101 DCDC1 voltage (CPU rail)
 *   0x83  AXP192 ADC enable 2            -
 *   0x84  AXP192 ADC sample rate         -
 *   0x90  AXP192 LDOIO0 mode             AXP2101 LDO enable 0
 *   0x00  IP5306 SYS_CTL0 (boost keep-on)
 *   0x01  IP5306 SYS_CTL1
 *   0x02  IP5306 SYS_CTL2 (light-load window field)
 *
 * M5Unified 0.2.20 `Power_Class.cpp` writes 0x12/0x82=0xFF/0x83=0x80/0x84=0x32
 * /0x90=0x07 on every AXP192 board and 0x12=0x00 (BATFET disable) plus
 * 0x30=0x0F on the Core2 v1.1 AXP2101 path, and never writes 0x82 there. A
 * write to the other part's layout is refused and counted, because on an
 * AXP2101 0x82 moves DCDC1, which is the CPU rail on that board.
 */
struct reg_entry_t {
  uint8_t reg;
  const char *axp192Meaning;
  const char *axp2101Meaning;
  const char *ip5306Meaning;
};

constexpr reg_entry_t REG_ENTRIES[] = {
    {0x00, nullptr,               nullptr,                 "ip5306-sys-ctl0"},
    {0x01, nullptr,               nullptr,                 "ip5306-sys-ctl1"},
    {0x02, nullptr,               nullptr,                 "ip5306-sys-ctl2"},
    {0x12, "axp192-rail-enables", "axp2101-batfet",        nullptr          },
    {0x30, nullptr,               "axp2101-adc-enable",    nullptr          },
    {0x82, "axp192-adc-enable-1", "axp2101-dcdc1-voltage", nullptr          },
    {0x83, "axp192-adc-enable-2", nullptr,                 nullptr          },
    {0x84, "axp192-adc-rate",     nullptr,                 nullptr          },
    {0x90, "axp192-ldoio0-mode",  "axp2101-ldo-enable-0",  nullptr          },
};

const reg_entry_t *regEntry(uint8_t reg) {
  for (const reg_entry_t &entry : REG_ENTRIES) {
    if (entry.reg == reg) {
      return &entry;
    }
  }
  return nullptr;
}

const char *regMeaning(const std::string &variant, uint8_t reg) {
  const reg_entry_t *entry = regEntry(reg);
  if (entry == nullptr) {
    return "unknown";
  }
  if (variant == "axp192") {
    return entry->axp192Meaning == nullptr ? "unknown" : entry->axp192Meaning;
  }
  if (variant == "axp2101") {
    return entry->axp2101Meaning == nullptr ? "unknown" : entry->axp2101Meaning;
  }
  if (variant == "ip5306") {
    return entry->ip5306Meaning == nullptr ? "unknown" : entry->ip5306Meaning;
  }
  return "unknown";
}

/** True when the modelled part carries this address in its own register map. */
bool partHasRegister(const std::string &variant, uint8_t reg) {
  return std::strcmp(regMeaning(variant, reg), "unknown") != 0;
}

/** One modelled register write. Returns false and counts a violation when the
 * part does not have that register at all. */
bool pmicWriteLocked(hardware_state_t &hw, uint8_t reg, uint8_t value) {
  if (!partHasRegister(hw.pmic.variant, reg)) {
    hw.pmic.violations++;
    ESP_LOGE(LOG_TAG, "%s does not have register 0x%02X; write refused", hw.pmic.variant.c_str(),
             static_cast<unsigned>(reg));
    return false;
  }
  hw.pmic.reg[reg] = value;
  hw.pmic.regWrites[reg]++;
  return true;
}

/**
 * One modelled register write that only makes sense on the part it was written
 * for.
 *
 * The AXP192 and the AXP2101 share addresses, so "the part has 0x82" is not
 * enough: on the AXP2101 it is the DCDC1 voltage register, which is the CPU rail
 * on a Core2 v1.1. A byte written for the wrong register map is refused and
 * counted instead of being applied to whatever sits at that address.
 */
bool pmicWriteForLocked(hardware_state_t &hw, uint8_t reg, uint8_t value, const char *meaning) {
  if (std::strcmp(regMeaning(hw.pmic.variant, reg), meaning) != 0) {
    hw.pmic.violations++;
    ESP_LOGE(LOG_TAG, "%s register 0x%02X is %s, not %s; write refused", hw.pmic.variant.c_str(),
             static_cast<unsigned>(reg), regMeaning(hw.pmic.variant, reg), meaning);
    return false;
  }
  return pmicWriteLocked(hw, reg, value);
}

/** The vendor init block for the modelled part, as M5.begin() writes it. */
void pmicInitLocked(hardware_state_t &hw) {
  const std::string &variant = hw.pmic.variant;
  if (variant == "axp192") {
    // Rail enables: the StickC pair ends with LDO3 on for the LCD and DCDC3
    // off; Core2 and Tough end with LDO2 on and LDO3/DCDC3 off. M5Unified no
    // longer writes 0x12 as a whole byte, so only the per-rail bits land.
    const bool stick = hw.profile == "stickc" || hw.profile == "stickc-plus";
    pmicWriteLocked(hw, 0x12, stick ? 0x08 : 0x04);
    pmicWriteLocked(hw, 0x82, 0xFF);  // ADC all on
    pmicWriteLocked(hw, 0x83, 0x80);  // ADC temperature on
    pmicWriteLocked(hw, 0x84, 0x32);  // ADC 25 Hz
    pmicWriteLocked(hw, 0x90, 0x07);  // LDOIO0 floating
  } else if (variant == "axp2101") {
    pmicWriteLocked(hw, 0x12, 0x00);  // BATFET disable
    pmicWriteLocked(hw, 0x30, 0x0F);  // ADC enabled
  } else if (variant == "ip5306") {
    (void)pmicWriteLocked(hw, 0x00, IP5306_SYS_CTL0_INIT);
    pmicWriteLocked(hw, 0x01, 0x1D);
    // The light-load selector. The vendor block writes 0x6C, bits 3:2 = 11,
    // which is 64 s. A scenario may select another field value to prove the
    // decode follows the byte, so a seeded byte is written in its place.
    const uint8_t sysCtl2 = hw.pmic.sysCtl2Seeded ? hw.pmic.sysCtl2Seed : IP5306_SYS_CTL2_INIT;
    if (pmicWriteForLocked(hw, 0x02, sysCtl2, "ip5306-sys-ctl2")
        && !hw.pmic.lightLoadWindowSeeded) {
      hw.pmic.lightLoadWindowMs = ip5306LightLoadMs(hw.pmic.reg[0x02]);
    }
    // Platform::keepIp5306BoostOn() holds SYS_CTL0 bit 1 for the session through
    // Furble::IP5306::holdBoostOn(), because a panel-off Core draws less than
    // 45 mA and the charger would read that as an idle load. The model takes the
    // same step, so the cut is armed only after the manual Off releases the bit.
    if (pmicWriteForLocked(hw, 0x00, Furble::IP5306::withBoostKeptOn(hw.pmic.reg[0x00]),
                           "ip5306-sys-ctl0")) {
      hw.pmic.boostKeepOn = Furble::IP5306::boostKeptOn(hw.pmic.reg[0x00]);
    }
  }
}

/**
 * The modelled shutdown path.
 *
 * The IP5306 has no shutdown command (plans/354): the vendor path sleeps the
 * panel, clears the boost keep-on bit and deep sleeps, and the rail then drops
 * once the load stays under 45 mA for the light-load window. The request is
 * accepted and deferred, so the modelled path reports success and releases the
 * bit. Every other modelled board cuts a PMIC output, releases the HOLD latch,
 * or deep sleeps.
 */
bool pmicPowerOffLocked(hardware_state_t &hw) {
  hw.pmic.powerOffAttempts++;
  if (hw.caps.powerOff != nullptr && std::strcmp(hw.caps.powerOff, "ip5306-light-load") == 0) {
    // The vendor path sleeps the panel, clears the keep-on bit and deep sleeps;
    // the rail then drops after the light-load window. The request is therefore
    // accepted and deferred, not refused (plans/354, finding 2): the model
    // releases the bit and reports the deferral so a scenario can see the boost
    // cut follow on the virtual clock.
    (void)pmicWriteForLocked(
        hw, 0x00, static_cast<uint8_t>(hw.pmic.reg[0x00] & ~Furble::IP5306::SYS_CTL0_BOOST_KEEP_ON),
        "ip5306-sys-ctl0");
    hw.pmic.boostKeepOn = false;
    // The rail is still up: the panel is dark and the charger owns the next
    // step, so the state machine stays ON until the light-load cut fires.
    hw.pmic.panelOff = true;
    ESP_LOGI(LOG_TAG, "IP5306 keep-on released; the light-load cut removes the boost after %u ms",
             static_cast<unsigned>(hw.pmic.lightLoadWindowMs));
    return true;
  }
  hw.power.state = power_state_t::OFF;
  return true;
}

/**
 * Advance the modelled light-load timer.
 *
 * The IP5306 drops the boost when the load stays under 45 mA with the panel
 * off for the configured window, unless the keep-on bit is set. The window is
 * read from REG_SYS_CTL2 bits 3:2, so the model cuts at the 64 s the vendor
 * block writes and not at the datasheet headline row's 32 s, which is the
 * power-on default of a field the firmware overwrites.
 */
void pmicLightLoadTickLocked(hardware_state_t &hw, uint32_t nowMs) {
  const uint32_t delta = hw.pmic.ticked ? (nowMs - hw.pmic.lastTickMs) : 0;
  hw.pmic.lastTickMs = nowMs;
  hw.pmic.ticked = true;
  if (hw.pmic.variant != "ip5306" || hw.power.state != power_state_t::ON) {
    hw.pmic.lightLoadElapsedMs = 0;
    return;
  }
  if (!hw.pmic.panelOff || hw.pmic.loadMa >= IP5306_LIGHT_LOAD_MA || hw.pmic.boostKeepOn) {
    hw.pmic.lightLoadElapsedMs = 0;
    return;
  }
  hw.pmic.lightLoadElapsedMs += delta;
  if (hw.pmic.lightLoadElapsedMs >= hw.pmic.lightLoadWindowMs) {
    hw.pmic.lightLoadCuts++;
    hw.power.state = power_state_t::OFF;
    hw.pmic.lightLoadElapsedMs = 0;
    ESP_LOGW(LOG_TAG, "IP5306 light-load cutoff after %u ms below %d mA with the panel off",
             static_cast<unsigned>(hw.pmic.lightLoadWindowMs), IP5306_LIGHT_LOAD_MA);
  }
}

void psramProbeTask(void *) {
  vTaskDelay(portMAX_DELAY);
}

}  // namespace

namespace Furble::Sim {

void hwSetProfile(const std::string &profile) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().profile = profile;
  state().caps = capsFor(profile);
  (void)rtcCapabilityMatchesLocked(state());
}

const char *hwProfile(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().profile.c_str();
}

const char *hwMcu(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().caps.mcu;
}

const char *hwPmicVariant(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.variant.c_str();
}

const char *hwUsbTransport(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().caps.usb;
}

const char *hwGauge(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.gauge.c_str();
}

const char *hwDisplayClass(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().caps.display;
}

void hwSetPsramMode(const std::string &mode) {
  {
    const std::lock_guard<std::mutex> lock(state().mutex);
    state().psramExplicit = true;
  }
  if (mode == "quad" || mode == "octal") {
    const size_t bytes = 8u * 1024u * 1024u;
    heapSetPsram(mode, bytes, bytes / 2);
  } else if (mode == "init_fail") {
    heapSetPsram("init_fail", 0, 0);
  } else {
    heapSetPsram("absent", 0, 0);
  }
}

void hwSetPmicVariant(const std::string &variant) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.variant = variant;
  if (variant == "none") {
    state().pmic.batteryPresent = false;
    state().pmic.chargeEnabled = false;
    state().pmic.powerLatch = false;
    state().pmic.watchdogEnabled = false;
  } else if (variant == "ip5306" || variant == "axp192" || variant == "axp2101") {
    state().pmic.powerLatch = true;
  }
}

void hwSetLoadMa(int loadMa) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.loadMa = loadMa;
  state().pmic.lightLoadElapsedMs = 0;
}

void hwSetLightLoadWindowMs(uint32_t windowMs) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.lightLoadWindowMs = windowMs;
  state().pmic.lightLoadWindowSeeded = true;
  state().pmic.lightLoadElapsedMs = 0;
}

void hwSetIp5306SysCtl2(uint8_t value) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  hardware_state_t &hw = state();
  hw.pmic.sysCtl2Seeded = true;
  hw.pmic.sysCtl2Seed = value;
  hw.pmic.lightLoadElapsedMs = 0;
  // Seeds run before hwStart(), which is where the vendor block lands, so the
  // byte is held until then. A model that already started takes it at once.
  if (!hw.started) {
    return;
  }
  if (pmicWriteForLocked(hw, 0x02, value, "ip5306-sys-ctl2") && !hw.pmic.lightLoadWindowSeeded) {
    hw.pmic.lightLoadWindowMs = ip5306LightLoadMs(hw.pmic.reg[0x02]);
  }
}

void hwSetBatteryGauge(const std::string &gauge) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.gauge = gauge;
}

void hwSetRailSupply(const std::string &supply) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.railSupply = supply;
}

std::string hwRailSupplyName(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  // By value: returning c_str() here handed the caller a pointer into a string
  // the lock no longer protected (plans/364-review-feedback.md, N7).
  return state().pmic.railSupply;
}

void hwSetIrBurstMs(uint32_t milliseconds) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.irBurstMs = milliseconds;
}

uint32_t hwIrBurstMs(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.irBurstMs;
}

void hwSetResetReasonName(const std::string &name) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().resetReason = reasonForName(name);
  state().resetReasonName = reasonName(state().resetReason);
}

void hwSetBatteryState(const std::string &batteryState) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  pmic_state_t &pmic = state().pmic;
  if (batteryState == "absent") {
    pmic.batteryPresent = false;
    pmic.batteryPercent = 0;
    pmic.batteryMillivolts = 0;
    pmic.charging = false;
  } else {
    pmic.batteryPresent = true;
    if (batteryState == "charging") {
      pmic.charging = true;
      pmic.chargeCurrentMa = 100;
    } else if (batteryState == "full") {
      pmic.charging = false;
      pmic.batteryPercent = 100;
      pmic.batteryMillivolts = 4200;
    } else if (batteryState == "low") {
      pmic.batteryPercent = 4;
      pmic.batteryMillivolts = 3400;
      pmic.charging = false;
    }
  }
}

void hwSetMqttStackModel(bool enabled,
                         size_t ownerStack,
                         size_t workerStack,
                         bool tls,
                         bool expectOverflow) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqtt.enabled = enabled;
  state().mqtt.ownerStack = ownerStack;
  state().mqtt.workerStack = workerStack;
  state().mqtt.tls = tls;
  state().mqtt.expectOverflow = expectOverflow;
}

void hwSetMqttHa(bool enabled) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqtt.ha = enabled;
}

void hwSetMqttBuffer(size_t bytes) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqtt.bufferBytes = bytes;
}

void hwSetMqttMainStack(size_t bytes, size_t highWater) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqtt.mainStackBytes = bytes;
  state().mqtt.mainHighWater = highWater;
}

void hwSetBootDefaultsMarker(const std::string &marker) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().bootDefaultsMarker = marker;
  state().bootMarkerWritten = marker == "current";
}

void hwSetBootProfile(const std::string &profile) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().bootProfile = profile;
  state().psramMemtest = profile == "debug";
}

void hwSetPsramMemtest(bool enabled) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().psramMemtest = enabled;
}

void hwSetWifiFastConnect(const std::string &mode) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().wifiFastConnect = mode;
}

void hwSetWifiAuth(const std::string &auth) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().wifiAuth = auth;
}

void hwSetMqttDiscoveryPayload(size_t bytes) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqttDiscoveryPayload = bytes;
}

void hwSetRtcModel(const std::string &mode) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  rtc_state_t &rtc = state().rtc;
  rtc = {};
  rtc.anchorUs = clockMicros();
  if (mode == "backed") {
    rtc.mode = rtc_mode_t::BACKED;
  } else if (mode == "unbacked") {
    rtc.mode = rtc_mode_t::UNBACKED;
  } else {
    // No chip: the wall clock stays on the host clock, exactly as it was
    // before the model existed, and no sidecar from an earlier run can leak
    // into this one.
    discardRtcSidecar();
    return;
  }

  rtc_sidecar_t sidecar = {};
  const bool cold = coldBootReason(state().resetReason);
  const bool haveSidecar = readRtcSidecar(sidecar);
  if (!cold && haveSidecar) {
    rtc.restored = true;
    // Only a cell-backed chip keeps its registers while the rail is down. A
    // chip without one comes back unset.
    if (rtc.mode == rtc_mode_t::BACKED) {
      rtc.hasReading = sidecar.hasReading;
      rtc.epochS = sidecar.epochS;
      rtc.voltLow = sidecar.voltLow;
    }
    ESP_LOGI(LOG_TAG, "rtc %s restored from the chip: registers %s at %llu s", mode.c_str(),
             rtc.hasReading ? "set" : "unset", static_cast<unsigned long long>(rtc.epochS));
  } else {
    if (haveSidecar) {
      // A cold boot is a full power loss: the cell did not keep the calendar,
      // and an install seed must not quietly resurrect the old reading.
      discardRtcSidecar();
      rtc.restored = true;
      ESP_LOGI(LOG_TAG, "rtc %s lost the calendar on a cold boot", mode.c_str());
    } else {
      ESP_LOGI(LOG_TAG, "rtc %s installed without a calendar", mode.c_str());
    }
  }
}

void hwSetRtcEpoch(uint64_t seconds) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  rtc_state_t &rtc = state().rtc;
  if (rtc.restored) {
    // The sidecar is what the chip actually holds. A cold-start install seed
    // must not overwrite a cell-backed reading.
    return;
  }
  rtc.epochS = seconds;
  rtc.anchorUs = clockMicros();
  rtc.hasReading = true;
  rtc.voltLow = false;
  persistRtcSidecar(rtc);
}

void hwSetRtcClockLost(bool lost) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  rtc_state_t &rtc = state().rtc;
  if (rtc.mode == rtc_mode_t::OFF) {
    return;
  }
  // A PCF8563 keeps its last register value when the backup supply drops; the
  // voltage-low flag is what says the value cannot be trusted. Keep the
  // reading so anything that formats a date still sees registers, and let the
  // production read path refuse it through the flag.
  rtc.voltLow = lost;
  persistRtcSidecar(rtc);
}

bool hwRtcPresent(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().rtc.mode != rtc_mode_t::OFF;
}

bool hwRtcBatteryBacked(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().rtc.mode == rtc_mode_t::BACKED;
}

uint64_t hwRtcEpochSeconds(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const rtc_state_t &rtc = state().rtc;
  if (rtc.mode == rtc_mode_t::OFF) {
    return 0;
  }
  return rtcReadingLocked(rtc);
}

bool hwRtcRead(uint64_t &epoch_us, bool &volt_low) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const rtc_state_t &rtc = state().rtc;
  volt_low = rtc.voltLow;
  if ((rtc.mode == rtc_mode_t::OFF) || !rtc.hasReading) {
    return false;
  }
  epoch_us = rtcReadingLocked(rtc) * 1000000ULL;
  return true;
}

void hwRtcWrite(uint64_t epoch_us) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  rtc_state_t &rtc = state().rtc;
  if (rtc.mode == rtc_mode_t::OFF) {
    return;
  }
  rtc.epochS = epoch_us / 1000000ULL;
  rtc.anchorUs = clockMicros();
  rtc.hasReading = true;
  rtc.voltLow = false;
  persistRtcSidecar(rtc);
}

bool hwRtcTimedAlarmBoard(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return std::strcmp(state().caps.timedWake, "rtc-alarm") == 0;
}

void hwRtcPowerOff(uint32_t seconds) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  rtc_state_t &rtc = state().rtc;
  if (rtc.mode == rtc_mode_t::OFF) {
    return;
  }
  if (rtc.mode == rtc_mode_t::BACKED) {
    // The cell keeps the chip counting through the sleep.
    rtc.epochS = rtcReadingLocked(rtc) + seconds;
    rtc.anchorUs = clockMicros();
  } else {
    // No cell: the registers do not survive the rail going down.
    rtc.epochS = 0;
    rtc.anchorUs = clockMicros();
    rtc.hasReading = false;
    rtc.voltLow = false;
  }
  persistRtcSidecar(rtc);
}

void hwRtcPersist(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  rtc_state_t &rtc = state().rtc;
  if (rtc.mode == rtc_mode_t::OFF) {
    return;
  }
  rtc.epochS = rtcReadingLocked(rtc);
  rtc.anchorUs = clockMicros();
  persistRtcSidecar(rtc);
}

void hwSetRailFixed(bool fixed) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().rail.fixed = fixed;
}

void hwSetRailRefused(bool refused) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().rail.refused = refused;
}

void hwSetPwrCfgStale(bool stale) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.pwrCfgStale = stale;
  // The snapshot is taken now, before the boot path and the scenario steps can
  // move the rail. The next LED read-modify-write carries it.
  state().pmic.pwrCfgSnapshot = state().pmic.pwrCfg;
}

void hwStart(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  hardware_state_t &hw = state();
  if (hw.started) {
    return;
  }
  hw.started = true;
  if (hw.profile.empty()) {
    hw.profile = defaultProfile();
    hw.caps = capsFor(hw.profile);
  }
  if (hw.pmic.variant.empty()) {
    hw.pmic.variant = hw.caps.pmic;
  }
  if (hw.pmic.gauge.empty()) {
    hw.pmic.gauge = hw.caps.gauge;
  }
  hw.pmic.batteryPresent = hw.caps.battery;
  hw.pmic.powerLatch = hw.caps.pmic[0] != 'n';
  // The vendor init block, per part. The register bytes it leaves behind are
  // what a scenario reads back with pmic.reg.<addr>.
  pmicInitLocked(hw);
  hw.power.state = power_state_t::ON;
  hw.usb.mode = usb_mode_t::APP;

  // The PMIC resets PWR_CFG to the documented 0x17 with the 5 V boost off, and
  // the StickS3 firmware keeps it there by passing output_power = false, so the
  // port is unpowered until a holder asks. The stale-read snapshot starts at the
  // same byte and moves when the rail manager switches the port on.
  hw.pmic.pwrCfg = PWR_CFG_RESET;
  applyRailFromPwrCfgLocked(hw);
  hw.pmic.pwrCfgSnapshot = hw.pmic.pwrCfg;

  // The RTC capability is a board-level fact, so check the pair at boot as well
  // as when a profile is selected.
  (void)rtcCapabilityMatchesLocked(hw);

  if (!hw.psramExplicit) {
    const std::string mode = hw.caps.psram;
    if (mode == "octal" || mode == "quad") {
      const size_t bytes = 8u * 1024u * 1024u;
      heapSetPsram(mode, bytes, bytes / 2);
    }
  }

  ESP_LOGI(LOG_TAG, "profile %s mcu %s flash %u KiB psram %s pmic %s usb %s display %s",
           hw.profile.c_str(), hw.caps.mcu, static_cast<unsigned>(hw.caps.flashKb),
           heapPsramState(), hw.pmic.variant.c_str(), hw.caps.usb, hw.caps.display);
  if (heapPsramInitFailed()) {
    ESP_LOGE(LOG_TAG, "SPIRAM init failed; external allocations are unavailable");
  }

  mqtt_model_state_t &mqtt = hw.mqtt;
  if (mqtt.enabled) {
    // The 30,007-byte hardware seed was measured with the 8 KiB main stack.
    // Charge the 12 KiB revert before the MQTT owner so the pool matches the
    // production stack rather than the experiment.
    if (mqtt.mainStackBytes > 8192) {
      const size_t delta = mqtt.mainStackBytes - 8192;
      if (heapAllocTagged(delta, "main_stack_12k", false, true) == 0) {
        mqtt.state = "alloc_failed";
        ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                 static_cast<unsigned>(delta));
        mqtt.heapFreeAfter = heapFree();
        return;
      }
    }
    // perf tasks left 3,020 of 12,288 bytes free on main with MQTT
    // connected, so main used about 9,268 bytes. The hardware 8 KiB stack
    // overflowed after MQTT was enabled; require the 12 KiB stack.
    if (mqtt.mainStackBytes < mqtt.mainHighWater) {
      mqtt.state = "main_overflow";
      mqtt.overflow = true;
      ESP_LOGE("mqtt", "***ERROR*** A stack overflow in task main");
    } else if (mqtt.ownerStack < 10240) {
      if (!mqtt.expectOverflow) {
        hw.failClosed = true;
        ESP_LOGE(LOG_TAG, "SIM FAIL: MQTT owner stack %u below 10240 bytes",
                 static_cast<unsigned>(mqtt.ownerStack));
        requestFailureExit();
      } else {
        mqtt.state = "overflow";
        mqtt.overflow = true;
        ESP_LOGE("mqtt", "***ERROR*** A stack overflow in task mqtt");
      }
    } else {
      const size_t ownerFreeFloor = mqtt.tls ? Furble::MQTTMemory::OWNER_TLS_INTERNAL_FREE_FLOOR
                                             : Furble::MQTTMemory::OWNER_INTERNAL_FREE_FLOOR;
      const size_t ownerLargestFloor = Furble::MQTTMemory::OWNER_INTERNAL_LARGEST_FLOOR;
      const size_t freeFloor = mqtt.tls ? Furble::MQTTMemory::START_TLS_INTERNAL_FREE_FLOOR
                                        : Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR;
      const size_t largestFloor = mqtt.tls ? Furble::MQTTMemory::START_TLS_INTERNAL_LARGEST_FLOOR
                                           : Furble::MQTTMemory::START_INTERNAL_LARGEST_FLOOR;
      if (heapFree() < ownerFreeFloor || heapLargest() < ownerLargestFloor) {
        mqtt.ownerGate = "closed";
        mqtt.state = "owner_blocked";
        ESP_LOGW("mqtt", "MQTT owner not created: internal free %u, largest %u (need %u/%u)",
                 static_cast<unsigned>(heapFree()), static_cast<unsigned>(heapLargest()),
                 static_cast<unsigned>(ownerFreeFloor), static_cast<unsigned>(ownerLargestFloor));
      } else if (heapAllocTagged(mqtt.ownerStack, "mqtt_owner", false, true) == 0) {
        mqtt.state = "alloc_failed";
        ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                 static_cast<unsigned>(mqtt.ownerStack));
      } else if (heapFree() < freeFloor || heapLargest() < largestFloor) {
        mqtt.startGate = "closed";
        mqtt.state = "start_blocked";
        ESP_LOGW("mqtt", "MQTT start deferred: internal free %u, largest %u (need %u/%u)",
                 static_cast<unsigned>(heapFree()), static_cast<unsigned>(heapLargest()),
                 static_cast<unsigned>(freeFloor), static_cast<unsigned>(largestFloor));
      } else {
        mqtt.ownerGate = "open";
        mqtt.startGate = "open";
        bool queueReady = false;
        if (heapPsramPresent()
            && heapPsramLargest() >= Furble::MQTTMemory::EVENT_QUEUE_BLOCK_BYTES) {
          const uint64_t queue =
              heapAllocTagged(Furble::MQTTMemory::EVENT_QUEUE_BYTES, "mqtt_queue", true, true);
          if (queue != 0) {
            mqtt.queueRegion = "psram";
            mqtt.queueBytes = Furble::MQTTMemory::EVENT_QUEUE_BYTES;
            queueReady = true;
          }
        }
        if (!queueReady) {
          if (heapAllocTagged(Furble::MQTTMemory::EVENT_QUEUE_BYTES, "mqtt_queue", false, true)
              == 0) {
            mqtt.state = "alloc_failed";
            ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                     static_cast<unsigned>(Furble::MQTTMemory::EVENT_QUEUE_BYTES));
            mqtt.heapFreeAfter = heapFree();
            return;
          }
          mqtt.queueRegion = "internal";
          mqtt.queueBytes = Furble::MQTTMemory::EVENT_QUEUE_BYTES;
        }
        // The esp-mqtt client task fills the first event copy instead of
        // building the event on its 3 KiB stack, and the owner receives into
        // the second. Production allocates both in one block with the queue,
        // in PSRAM when the block fits there.
        const bool copiesPsram =
            heapPsramPresent()
            && heapPsramLargest() >= Furble::MQTTMemory::EVENT_COPIES_BLOCK_BYTES;
        if (heapAllocTagged(Furble::MQTTMemory::EVENT_COPIES_BYTES, "mqtt_event_copies",
                            copiesPsram, true)
            == 0) {
          mqtt.state = "alloc_failed";
          ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                   static_cast<unsigned>(Furble::MQTTMemory::EVENT_COPIES_BYTES));
          mqtt.heapFreeAfter = heapFree();
          return;
        }
        if (heapAllocTagged(mqtt.workerStack, mqtt.tls ? "mqtt_tls_worker" : "mqtt_worker", false,
                            true)
            == 0) {
          mqtt.state = "alloc_failed";
          ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                   static_cast<unsigned>(mqtt.workerStack));
          mqtt.heapFreeAfter = heapFree();
          return;
        }
        for (int buffer = 0; buffer < 2; buffer++) {
          if (heapAllocTagged(mqtt.bufferBytes, "mqtt_buffer", false, true) == 0) {
            mqtt.state = "alloc_failed";
            ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                     static_cast<unsigned>(mqtt.bufferBytes));
            mqtt.heapFreeAfter = heapFree();
            return;
          }
        }
        // publishState(true) loads the saved camera list before discovery.
        constexpr size_t CAMERA_SNAPSHOT_BYTES = 5120;
        if (heapAllocTagged(CAMERA_SNAPSHOT_BYTES, "mqtt_camera", false, true) == 0) {
          mqtt.state = "alloc_failed";
          ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                   static_cast<unsigned>(CAMERA_SNAPSHOT_BYTES));
          mqtt.heapFreeAfter = heapFree();
          return;
        }
        mqtt.cameraBytes = CAMERA_SNAPSHOT_BYTES;
        mqtt.state = "created";
        mqtt.discoveryFragments = static_cast<uint32_t>(
            (hw.mqttDiscoveryPayload + mqtt.bufferBytes - 1) / mqtt.bufferBytes);
        ESP_LOGI("mqtt", "owner stack %u worker stack %u%s queue %s created",
                 static_cast<unsigned>(mqtt.ownerStack), static_cast<unsigned>(mqtt.workerStack),
                 mqtt.tls ? " TLS" : "", mqtt.queueRegion.c_str());
      }
    }
    if (mqtt.bufferBytes > 2048) {
      hw.failClosed = true;
      ESP_LOGE(LOG_TAG, "SIM FAIL: MQTT buffer %u above 2048 bytes",
               static_cast<unsigned>(mqtt.bufferBytes));
      requestFailureExit();
    }
    if (mqtt.ha && mqtt.state == "created") {
      if (heapFree() < 10240 || heapLargest() < 4096) {
        mqtt.discoveryGate = "closed";
        mqtt.haState = "blocked";
        ESP_LOGW("mqtt", "HA discovery skipped: internal free %u, largest %u (need 10240/4096)",
                 static_cast<unsigned>(heapFree()), static_cast<unsigned>(heapLargest()));
      } else {
        mqtt.discoveryGate = "open";
        mqtt.haCache = "valid";
        const size_t discovery = 8192;
        const uint64_t discoveryId = heapAllocTagged(discovery, "mqtt_ha_discovery", false, false);
        if (discoveryId == 0) {
          mqtt.haState = "failed";
          ESP_LOGE("mqtt", "Home Assistant discovery allocation failed; discovery disabled");
        } else {
          mqtt.haState = "ok";
          mqtt.haPeak = discovery;
          heapFreeTagged(discoveryId);
        }
      }
    }
    mqtt.heapFreeAfter = heapFree();
  }
}

void hwSetResetReason(uint32_t reason) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().resetReason = reason;
  state().resetReasonName = reasonName(reason);
}

uint32_t hwResetReason(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().resetReason;
}

bool hwAction(const std::string &name) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  hardware_state_t &hw = state();

  if (name == "power-single-click") {
    hw.power.clicks++;
    if (hw.power.state == power_state_t::OFF) {
      powerOnLocked(hw);
      ESP_LOGI(LOG_TAG, "power button single click: power on");
    } else if (hw.power.state == power_state_t::DOWNLOAD || hw.usb.mode != usb_mode_t::APP) {
      recoverToAppLocked(hw, "power button single click recovered ROM download mode");
    } else {
      hw.power.resets++;
      hw.resetReason = ESP_RST_SW;
      hw.resetReasonName = "software";
      ESP_LOGI(LOG_TAG, "power button single click: reset");
    }
    return true;
  }
  if (name == "power-double-click") {
    hw.power.doubleClicks++;
    if (hw.power.state == power_state_t::ON) {
      hw.power.state = power_state_t::OFF;
      hw.power.offs++;
      notePowerOff();
      ESP_LOGI(LOG_TAG, "power button double click: power off");
    } else {
      powerOnLocked(hw);
      ESP_LOGI(LOG_TAG, "power button double click: power on");
    }
    return true;
  }
  if (name == "power-long-press") {
    hw.power.longPresses++;
    if (hw.power.state == power_state_t::ON) {
      hw.power.downloads++;
      enterRomLocked(hw, "power button long press reached download mode");
    }
    return true;
  }
  if (name == "usb-open") {
    hw.usb.hostOpen = true;
    ESP_LOGI(LOG_TAG, "USB host opened the Serial/JTAG port");
    return true;
  }
  if (name == "usb-close") {
    hw.usb.hostOpen = false;
    ESP_LOGI(LOG_TAG, "USB host closed the Serial/JTAG port; mode stays %s",
             hw.usb.mode == usb_mode_t::APP ? "app" : "rom");
    return true;
  }
  if (name == "usb-dtr") {
    hw.usb.dtr = true;
    return true;
  }
  if (name == "usb-rts") {
    hw.usb.rts = true;
    if (hw.usb.hostOpen && hw.usb.dtr) {
      enterRomLocked(hw, "USB host open with DTR/RTS asserted");
    }
    return true;
  }
  if (name == "usb-reset") {
    if (hw.usb.mode == usb_mode_t::APP) {
      hw.resetReason = ESP_RST_USB;
      hw.resetReasonName = "usb_uart_chip_reset";
    } else {
      recoverToAppLocked(hw, "USB host reset recovered the application");
    }
    return true;
  }
  if (name == "usb-rom-entry") {
    enterRomLocked(hw, "ROM download mode forced by scenario");
    return true;
  }
  if (name == "usb-esptool-sync") {
    if (hw.usb.mode == usb_mode_t::ROM) {
      hw.usb.mode = usb_mode_t::ESPTOOL;
      hw.usb.syncs++;
      ESP_LOGI(LOG_TAG, "esptool sync accepted; application console and BLE remain silent");
    }
    return true;
  }
  if (name == "pmic-charge-on") {
    hw.pmic.chargeEnabled = true;
    hw.pmic.charging = true;
    hw.pmic.chargeCurrentMa = 100;
    return true;
  }
  if (name == "pmic-charge-off") {
    hw.pmic.chargeEnabled = false;
    hw.pmic.charging = false;
    hw.pmic.chargeCurrentMa = 0;
    return true;
  }
  if (name == "pmic-battery-present") {
    hw.pmic.batteryPresent = true;
    hw.pmic.batteryPercent = 80;
    hw.pmic.batteryMillivolts = 4000;
    hw.pmic.charging = false;
    return true;
  }
  if (name == "pmic-battery-absent") {
    hw.pmic.batteryPresent = false;
    hw.pmic.batteryPercent = 0;
    hw.pmic.batteryMillivolts = 0;
    hw.pmic.charging = false;
    return true;
  }
  if (name == "pmic-brownout") {
    hw.pmic.brownouts++;
    hw.resetReason = ESP_RST_BROWNOUT;
    hw.resetReasonName = "brownout";
    hw.power.state = power_state_t::OFF;
    ESP_LOGE(LOG_TAG, "PMIC brownout detected; rail dropped");
    crashLogInject(ESP_RST_BROWNOUT, "pmic", "PMIC brownout detected; rail dropped");
    return true;
  }
  if (name == "pmic-watchdog-on") {
    hw.pmic.watchdogEnabled = true;
    return true;
  }
  if (name == "pmic-watchdog-off") {
    hw.pmic.watchdogEnabled = false;
    return true;
  }
  if (name == "pmic-boost-keep-on") {
    // The keep-on bit that keeps the IP5306 boost alive under the 45 mA
    // light-load floor (docs/power/board-power.md PM-06).
    if (pmicWriteForLocked(hw, 0x00, Furble::IP5306::withBoostKeptOn(hw.pmic.reg[0x00]),
                           "ip5306-sys-ctl0")) {
      hw.pmic.boostKeepOn = true;
    }
    return true;
  }
  if (name == "pmic-boost-keep-off") {
    if (pmicWriteForLocked(
            hw, 0x00,
            static_cast<uint8_t>(hw.pmic.reg[0x00] & ~Furble::IP5306::SYS_CTL0_BOOST_KEEP_ON),
            "ip5306-sys-ctl0")) {
      hw.pmic.boostKeepOn = false;
    }
    return true;
  }
  if (name == "pmic-axp192-adc") {
    // The AXP192 ADC block M5Unified writes on every AXP192 board. On an AXP2101
    // part 0x82 is the DCDC1 voltage register, which is the CPU rail on a Core2
    // v1.1, so the block is refused there and counted. A scenario uses it as the
    // negative control for "0x82 is never written".
    (void)pmicWriteForLocked(hw, 0x82, 0xFF, "axp192-adc-enable-1");
    (void)pmicWriteForLocked(hw, 0x83, 0x80, "axp192-adc-enable-2");
    (void)pmicWriteForLocked(hw, 0x84, 0x32, "axp192-adc-rate");
    return true;
  }
  if (name == "wifi-connect") {
    hw.wifi.state = "connected";
    hw.wifi.reason = "none";
    hw.wifi.attempts = 0;
    hw.wifi.retryMs = 0;
    return true;
  }
  if (name == "wifi-disconnect") {
    hw.wifi.state = "idle";
    hw.wifi.reason = "user";
    return true;
  }
  if (name == "wifi-fail-auth" || name == "wifi-fail-notfound" || name == "wifi-fail-timeout") {
    hw.wifi.state = "failed";
    hw.wifi.reason = (name == "wifi-fail-auth")
                         ? "auth"
                         : ((name == "wifi-fail-notfound") ? "notfound" : "timeout");
    hw.wifi.attempts++;
    hw.wifi.retryMs = std::min<uint32_t>(30000, 1000U << std::min<uint32_t>(hw.wifi.attempts, 5));
    return true;
  }
  if (name == "wifi-ntp-sync") {
    hw.wifi.ntpSynced = true;
    hw.wifi.ntpOffsetUs = 0;
    return true;
  }
  if (name == "wifi-ntp-fail") {
    hw.wifi.ntpSynced = false;
    hw.wifi.ntpOffsetUs = 1200000;
    return true;
  }
  if (name == "ethernet-up") {
    hw.net.ethernet = "up";
    hw.net.linkUps++;
    return true;
  }
  if (name == "ethernet-down") {
    hw.net.ethernet = "down";
    hw.net.linkDowns++;
    return true;
  }
  if (name == "ble-auth-timeout") {
    hw.ble.auth = "timeout";
    hw.ble.companion = "reauth";
    hw.ble.authTimeouts++;
    ESP_LOGW(LOG_TAG, "companion auth timeout; link stays up for reauth");
    return true;
  }
  if (name == "ble-gatt-stale") {
    hw.ble.gattCache = "stale";
    return true;
  }
  if (name == "ble-gatt-fresh") {
    hw.ble.gattCache = "valid";
    return true;
  }
  if (name == "companion-reidentify") {
    hw.ble.reidentifyCount++;
    hw.ble.bond = "none";
    hw.ble.gattCache = "valid";
    hw.ble.companion = "advertising";
    ESP_LOGI(LOG_TAG, "companion reidentify: address rotated, bond dropped, pairing reopened");
    return true;
  }
  if (name == "companion-reject-central") {
    hw.ble.rejectedLinks++;
    hw.ble.companion = "rejected";
    ESP_LOGW(LOG_TAG, "companion rejected an unbonded central");
    return true;
  }
  if (name == "companion-reconnect") {
    hw.ble.reconnects++;
    hw.ble.companion = "connected";
    hw.ble.auth = "ok";
    return true;
  }
  if (name == "audio-overcurrent") {
    hw.peripheral.audio = "overcurrent";
    hw.peripheral.audioOvercurrents++;
    ESP_LOGE(LOG_TAG, "speaker overcurrent; output disabled");
    return true;
  }
  if (name == "ir-timeout") {
    hw.peripheral.ir = "timeout";
    hw.peripheral.irTimeouts++;
    ESP_LOGW(LOG_TAG, "IR RMT transmit timeout");
    return true;
  }
  // One IR transfer window. Both ends sit behind EXT_5V_EN, so the transfer
  // needs the rail on, holds it while it runs and fails when a cut took.
  if (name == "ir-transmit") {
    if (!irStartLocked(hw)) {
      return true;
    }
    ESP_LOGI(LOG_TAG, "IR transfer started; EXT_5V held by %s", hw.rail.holders.c_str());
    return true;
  }
  if (name == "ir-transmit-end") {
    (void)irEndLocked(hw);
    return true;
  }
  // Flip the modelled RTC capability so a scenario can prove the board check
  // detects a wrong answer instead of trusting it.
  if (name == "hw-rtc-mismatch") {
    hw.caps.rtc = !hw.caps.rtc;
    (void)rtcCapabilityMatchesLocked(hw);
    return true;
  }
  if (name == "sd-present") {
    hw.peripheral.sd = "mounted";
    return true;
  }
  if (name == "sd-absent") {
    hw.peripheral.sd = "absent";
    return true;
  }
  if (name == "sd-error") {
    hw.peripheral.sd = "error";
    hw.peripheral.sdErrors++;
    return true;
  }
  if (name == "companion-boot") {
    hw.companionResident = true;
    hw.companionEnabled = true;
    hw.ble.companion = "advertising";
    return true;
  }
  if (name == "companion-enable") {
    if (hw.companionEnabled) {
      return true;
    }
    if (!hw.companionResident) {
      // Mirrors CompanionGatt::enable(): refuse before the first allocation.
      const auto gate = CompanionMemory::startGate(heapFree(), heapLargest());
      if (!gate.open) {
        hw.companionGate = "memory";
        ESP_LOGE("companion", FURBLE_COMPANION_START_GATE_LOG,
                 static_cast<unsigned>(gate.freeBytes), static_cast<unsigned>(gate.largestBlock),
                 static_cast<unsigned>(gate.freeFloor), static_cast<unsigned>(gate.largestFloor));
        return true;
      }
      heapAllocTagged(CompanionMemory::TASK_STACK_BYTES, "companion", false, true);
      heapAllocTagged(CompanionMemory::GATT_BYTES, "companion_gatt", false, true);
      hw.companionResident = true;
    }
    hw.companionGate = "none";
    hw.companionEnabled = true;
    hw.ble.companion = "advertising";
    return true;
  }
  if (name == "companion-disable") {
    hw.companionEnabled = false;
    hw.ble.companion = "off";
    return true;
  }
  if (name == "boot-reboot") {
    hw.bootSeedAttempts++;
    if (hw.bootDefaultsMarker != "current" || hw.bootSeedFail) {
      hw.bootDefaultScans++;
      if (hw.bootSeedFail) {
        hw.bootMarkerWritten = false;
        ESP_LOGW(LOG_TAG, "settings default seed failed; marker not written, next boot rescans");
      } else {
        hw.bootMarkerWritten = true;
        hw.bootDefaultsMarker = "current";
      }
    }
    return true;
  }
  if (name == "boot-seed-fail") {
    hw.bootSeedFail = true;
    return true;
  }
  if (name == "wifi-fast-fail") {
    hw.wifiDirectFailures++;
    hw.wifiMovedAp = true;
    if (hw.wifiScans == 0) {
      hw.wifiScans++;
      ESP_LOGW(LOG_TAG, "fast connect failed; scanning before the next join");
    }
    return true;
  }
  if (name == "wifi-moved-ap") {
    hw.wifiMovedAp = true;
    hw.wifiFastConnect = "unusable";
    return true;
  }
  if (name == "mqtt-ha-toggle") {
    hw.mqtt.haCache = "invalid";
    return true;
  }
  if (name == "mqtt-discovery-payload") {
    hw.mqttDiscoveryPayload = 3005;
    hw.mqttDiscoveryFragments = static_cast<uint32_t>((hw.mqttDiscoveryPayload + 1535) / 1536);
    return true;
  }
  if (name == "ota-update") {
    hw.otaSlot = hw.otaSlot == "ota_0" ? "ota_1" : "ota_0";
    hw.otaRollback = true;
    hw.otaUpdates++;
    ESP_LOGI(LOG_TAG, "OTA image written to %s; rollback armed", hw.otaSlot.c_str());
    return true;
  }
  if (name == "ota-commit") {
    hw.otaRollback = false;
    hw.otaCommits++;
    ESP_LOGI(LOG_TAG, "OTA image in %s marked valid", hw.otaSlot.c_str());
    return true;
  }
  if (name == "ota-rollback") {
    if (hw.otaRollback) {
      hw.otaSlot = hw.otaSlot == "ota_0" ? "ota_1" : "ota_0";
      hw.otaRollback = false;
      ESP_LOGW(LOG_TAG, "OTA rollback selected %s", hw.otaSlot.c_str());
    }
    return true;
  }
  if (name == "alloc-icon") {
    const size_t bytes = 12291;
    if (heapPsramPresent()) {
      const uint64_t id = heapAllocTagged(bytes, "icon_decode", true, false);
      hw.iconState = id == 0 ? "failed" : "ok";
      if (id != 0) {
        heapFreeTagged(id);
      }
    } else {
      // Plan 31 recorded caps 0x1800 for this malloc: MALLOC_CAP_DEFAULT plus
      // MALLOC_CAP_INTERNAL. The model logs it and aborts like the device.
      const uint64_t id = heapAllocTagged(bytes, "icon_decode", false, false, 0x1800);
      hw.iconState = id == 0 ? "failed" : "ok";
      if (id != 0) {
        heapFreeTagged(id);
      }
    }
    return true;
  }
  if (name == "heap-leak-probe") {
    if (hw.leakProbe == 0) {
      hw.leakProbe = heapAllocTagged(512, "leak_probe", false, false);
    }
    return true;
  }
  if (name == "heap-leak-free") {
    if (hw.leakProbe != 0) {
      heapFreeTagged(hw.leakProbe);
      hw.leakProbe = 0;
    }
    return true;
  }
  // Runtime memory pressure: each squeeze holds one more 4 KiB internal block
  // under its own tag, so a scenario walks the heap down past the governor
  // thresholds while services run, then gives it back.
  if (name == "heap-squeeze") {
    const uint64_t id = heapAllocTagged(4096, "heap_squeeze", false, false);
    if (id != 0) {
      hw.squeeze.push_back(id);
    }
    return true;
  }
  if (name == "heap-release") {
    for (const uint64_t id : hw.squeeze) {
      heapFreeTagged(id);
    }
    hw.squeeze.clear();
    return true;
  }
  if (name == "psram-task-start") {
    if (std::strcmp(heapTaskStackRegion(), "psram") == 0 && heapPsramPresent()) {
      xTaskCreate(psramProbeTask, "psram_probe", 2048, nullptr, 1, nullptr);
    }
    return true;
  }
  if (name == "flash-write") {
    heapFlashWrite();
    if (heapFlashUnsafe()) {
      hw.failClosed = true;
      requestFailureExit();
    }
    return true;
  }
  return false;
}

std::string hwQuery(const std::string &key) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const hardware_state_t &hw = state();
  const auto prefixed = [&key](const char *prefix) {
    const size_t length = std::char_traits<char>::length(prefix);
    return key.size() >= length && key.compare(0, length, prefix) == 0;
  };

  if (prefixed("hw.")) {
    const std::string sub = key.substr(3);
    if (sub == "profile") {
      return hw.profile;
    }
    if (sub == "mcu") {
      return hw.caps.mcu;
    }
    if (sub == "flash_kb") {
      return std::to_string(hw.caps.flashKb);
    }
    if (sub == "battery") {
      return hw.caps.battery ? "present" : "absent";
    }
    if (sub == "psram") {
      return heapPsramMode();
    }
    if (sub == "psram_state") {
      return heapPsramState();
    }
    if (sub == "pmic") {
      return hw.pmic.variant;
    }
    if (sub == "gauge") {
      return hw.pmic.gauge;
    }
    if (sub == "usb") {
      return hw.caps.usb;
    }
    if (sub == "display") {
      return hw.caps.display;
    }
    if (sub == "touch") {
      return hw.caps.touch ? "yes" : "no";
    }
    if (sub == "wifi") {
      return hw.caps.wifi ? "yes" : "no";
    }
    if (sub == "ethernet") {
      return hw.caps.ethernet ? "yes" : "no";
    }
    if (sub == "ble") {
      return hw.caps.ble ? "yes" : "no";
    }
    if (sub == "ir") {
      return hw.caps.ir ? "yes" : "no";
    }
    if (sub == "sd") {
      return hw.caps.sd ? "yes" : "no";
    }
    if (sub == "audio") {
      return hw.caps.audio ? "yes" : "no";
    }
    if (sub == "rtc") {
      return hw.caps.rtc ? "yes" : "no";
    }
    if (sub == "can_power_off") {
      // The UI policy gate (`Profile::canPowerOff`), not the platform path.
      return hw.caps.canPowerOff ? "yes" : "no";
    }
    if (sub == "rtc_on_bus") {
      return hw.caps.rtcOnBus ? "yes" : "no";
    }
    if (sub == "led_active_low") {
      return hw.caps.ledActiveLow ? "yes" : "no";
    }
    if (sub == "ir_mark_low") {
      return hw.caps.irMarkLow ? "yes" : "no";
    }
    if (sub == "gps_pin_source") {
      return hw.caps.gpsPinSource == nullptr ? "none" : hw.caps.gpsPinSource;
    }
    if (sub == "gps_rx_pin") {
      return std::to_string(hw.caps.gpsRxPin);
    }
    if (sub == "gps_tx_pin") {
      return std::to_string(hw.caps.gpsTxPin);
    }
    if (sub == "rtc_backed") {
      // The profile column itself, not the runtime RTC mode `hw.rtc_battery`
      // seeds: a scenario compares this against Board::profile().
      return hw.caps.rtcBatteryBacked ? "yes" : "no";
    }
    if (sub == "rtc_capability") {
      const rtc_fact_t *fact = rtcFactFor(hw.profile);
      return (fact != nullptr && hw.caps.rtc == fact->rtc) ? "match" : "mismatch";
    }
    if (sub == "fail_closed") {
      return hw.failClosed ? "yes" : "no";
    }
    // The modelled calendar chip, separate from the board capability above.
    if (sub == "rtc_model") {
      return hw.rtc.mode == rtc_mode_t::BACKED     ? "backed"
             : hw.rtc.mode == rtc_mode_t::UNBACKED ? "unbacked"
                                                   : "off";
    }
    if (sub == "rtc_battery") {
      return hw.rtc.mode == rtc_mode_t::BACKED ? "yes" : "no";
    }
    if (sub == "rtc_clock") {
      if (hw.rtc.mode == rtc_mode_t::OFF || !hw.rtc.hasReading) {
        return "unset";
      }
      return hw.rtc.voltLow ? "lost" : "valid";
    }
    if (sub == "rtc_volt_low") {
      return hw.rtc.voltLow ? "yes" : "no";
    }
    if (sub == "rtc_epoch") {
      return std::to_string(hw.rtc.mode == rtc_mode_t::OFF ? 0 : rtcReadingLocked(hw.rtc));
    }
    if (sub == "timed_wake") {
      return hw.caps.timedWake;
    }
    // Supply sense. The Waveshare node has none: POE_5V and VBUS are one net,
    // so a scenario can assert that no later change invents a sense pin.
    if (sub == "vbus") {
      return hw.caps.vbus ? "yes" : "no";
    }
    if (sub == "poe") {
      return hw.caps.poe ? "yes" : "no";
    }
  }
  if (prefixed("pmic.")) {
    const std::string sub = key.substr(5);
    if (sub == "variant") {
      return hw.pmic.variant;
    }
    if (sub == "gauge") {
      return hw.pmic.gauge;
    }
    if (sub == "battery") {
      return hw.pmic.batteryPresent ? "present" : "absent";
    }
    if (sub == "charging") {
      return hw.pmic.charging ? "charging" : "idle";
    }
    if (sub == "charger") {
      return hw.pmic.chargeEnabled ? "on" : "off";
    }
    if (sub == "charge_current_ma") {
      return std::to_string(hw.pmic.chargeCurrentMa);
    }
    if (sub == "watchdog") {
      if (hw.pmic.variant == "m5pm1") {
        return hw.pmic.watchdogEnabled ? "armed" : "disabled";
      }
      return "unavailable";
    }
    if (sub == "watchdog_feeds") {
      return std::to_string(hw.pmic.watchdogFeeds);
    }
    if (sub == "download_lock") {
      return hw.pmic.downloadLocked ? "locked" : "unlocked";
    }
    if (sub == "power_latch") {
      return hw.pmic.powerLatch ? "latched" : "none";
    }
    if (sub == "brownouts") {
      return std::to_string(hw.pmic.brownouts);
    }
    if (sub == "battery_mv") {
      return std::to_string(hw.pmic.batteryMillivolts);
    }
    if (sub == "battery_percent") {
      return std::to_string(hw.pmic.batteryPercent);
    }
    if (sub == "violations") {
      return std::to_string(hw.pmic.violations);
    }
    if (sub == "boost_keep_on") {
      return hw.pmic.boostKeepOn ? "yes" : "no";
    }
    if (sub == "load_ma") {
      return std::to_string(hw.pmic.loadMa);
    }
    if (sub == "light_load_window_ms") {
      return std::to_string(hw.pmic.lightLoadWindowMs);
    }
    if (sub == "light_load_field") {
      // REG_SYS_CTL2 bits 3:2, the selector the window decodes from.
      return std::to_string(ip5306LightLoadField(hw.pmic.reg[0x02]));
    }
    if (sub == "light_load_source") {
      return hw.pmic.lightLoadWindowSeeded ? "seed" : "reg";
    }
    if (sub == "light_load_ms") {
      return std::to_string(hw.pmic.lightLoadElapsedMs);
    }
    if (sub == "light_load_cuts") {
      return std::to_string(hw.pmic.lightLoadCuts);
    }
    if (sub.compare(0, 4, "reg.") == 0) {
      // pmic.reg.<0xNN>, pmic.reg.<0xNN>.writes and pmic.reg.<0xNN>.meaning.
      std::string rest = sub.substr(4);
      std::string field;
      const size_t dot = rest.find('.');
      if (dot != std::string::npos) {
        field = rest.substr(dot + 1);
        rest.resize(dot);
      }
      char *end = nullptr;
      const unsigned long long reg = std::strtoull(rest.c_str(), &end, 0);
      if (end == rest.c_str() || *end != '\0' || reg > 0xFF) {
        return "invalid";
      }
      const uint8_t address = static_cast<uint8_t>(reg);
      if (field.empty()) {
        return std::to_string(hw.pmic.reg[address]);
      }
      if (field == "writes") {
        return std::to_string(hw.pmic.regWrites[address]);
      }
      if (field == "meaning") {
        return regMeaning(hw.pmic.variant, address);
      }
      return "invalid";
    }
    if (sub == "pwr_cfg") {
      return std::to_string(hw.pmic.pwrCfg);
    }
    if (sub == "pwr_cfg_lost") {
      return std::to_string(hw.pmic.pwrCfgLostUpdates);
    }
    if (sub == "watchdog_cycles") {
      return std::to_string(hw.pmic.watchdogCycles);
    }
  }
  if (prefixed("rail.")) {
    const std::string sub = key.substr(5);
    if (sub == "state") {
      return hw.rail.on ? "on" : "off";
    }
    if (sub == "holders") {
      return hw.rail.holders;
    }
    if (sub == "refusals") {
      return std::to_string(hw.rail.refusals);
    }
  }
  if (prefixed("power.")) {
    const std::string sub = key.substr(6);
    if (sub == "state") {
      return hw.power.state == power_state_t::ON    ? "on"
             : hw.power.state == power_state_t::OFF ? "off"
                                                    : "download";
    }
    if (sub == "clicks") {
      return std::to_string(hw.power.clicks);
    }
    if (sub == "double_clicks") {
      return std::to_string(hw.power.doubleClicks);
    }
    if (sub == "long_presses") {
      return std::to_string(hw.power.longPresses);
    }
    if (sub == "resets") {
      return std::to_string(hw.power.resets);
    }
    if (sub == "offs") {
      return std::to_string(hw.power.offs);
    }
    if (sub == "downloads") {
      return std::to_string(hw.power.downloads);
    }
    if (sub == "recoveries") {
      return std::to_string(hw.power.recoveries);
    }
    if (sub == "off_path") {
      return hw.caps.powerOff == nullptr ? "unknown" : hw.caps.powerOff;
    }
    if (sub == "can_off") {
      // The platform path (`Profile::powerOffPath`), not the UI policy gate
      // (`Profile::canPowerOff`, which `board.can_power_off` reports). The Core
      // is the board where they differ: its manual Off clears the IP5306
      // boost keep-on bit and the light-load cut removes the rail after the
      // 64 s window, so the path exists while the automatic policies stay off.
      return hw.caps.powerOffPath ? "yes" : "no";
    }
    if (sub == "off_attempts") {
      return std::to_string(hw.pmic.powerOffAttempts);
    }
    if (sub == "off_refused") {
      return std::to_string(hw.pmic.powerOffRefused);
    }
    if (sub == "rail.gps") {
      return hw.pmic.gpsRail ? "on" : "off";
    }
    if (sub == "rail.ext5v") {
      // Only Core2 and Tough switch the external 5 V net with the Port A rail.
      if (!hw.caps.ext5vShared) {
        return "n/a";
      }
      return hw.pmic.ext5v ? "on" : "off";
    }
    if (sub == "rail.ext5v_drops") {
      if (!hw.caps.ext5vShared) {
        return "n/a";
      }
      return std::to_string(hw.pmic.ext5vDrops);
    }
  }
  if (prefixed("usb.")) {
    const std::string sub = key.substr(4);
    if (sub == "transport") {
      return hw.caps.usb;
    }
    if (sub == "mode") {
      return hw.usb.mode == usb_mode_t::APP   ? "app"
             : hw.usb.mode == usb_mode_t::ROM ? "rom"
                                              : "esptool";
    }
    if (sub == "host") {
      return hw.usb.hostOpen ? "open" : "closed";
    }
    if (sub == "console") {
      return (hw.usb.mode == usb_mode_t::APP && hw.power.state != power_state_t::OFF) ? "yes"
                                                                                      : "no";
    }
    if (sub == "ble") {
      return (hw.usb.mode == usb_mode_t::APP && hw.power.state != power_state_t::OFF && hw.caps.ble)
                 ? "yes"
                 : "no";
    }
    if (sub == "rom_entries") {
      return std::to_string(hw.usb.romEntries);
    }
    if (sub == "syncs") {
      return std::to_string(hw.usb.syncs);
    }
    if (sub == "recoveries") {
      return std::to_string(hw.usb.recoveries);
    }
  }
  if (prefixed("wifi.")) {
    const std::string sub = key.substr(5);
    if (sub == "state") {
      return hw.wifi.state;
    }
    if (sub == "reason") {
      return hw.wifi.reason;
    }
    if (sub == "ip") {
      return hw.wifi.ip;
    }
    if (sub == "ssid") {
      return hw.wifi.ssid;
    }
    if (sub == "rssi") {
      return std::to_string(hw.wifi.rssi);
    }
    if (sub == "attempts") {
      return std::to_string(hw.wifi.attempts);
    }
    if (sub == "retry_ms") {
      return std::to_string(hw.wifi.retryMs);
    }
    if (sub == "ntp") {
      return hw.wifi.ntpSynced ? "synced" : "idle";
    }
    if (sub == "ntp_offset_us") {
      return std::to_string(hw.wifi.ntpOffsetUs);
    }
  }
  if (key == "net.ethernet") {
    return hw.net.ethernet;
  }
  if (key == "net.link_ups") {
    return std::to_string(hw.net.linkUps);
  }
  if (key == "net.link_downs") {
    return std::to_string(hw.net.linkDowns);
  }
  if (prefixed("ble.")) {
    const std::string sub = key.substr(4);
    if (sub == "bond") {
      return hw.ble.bond;
    }
    if (sub == "auth") {
      return hw.ble.auth;
    }
    if (sub == "gatt_cache") {
      return hw.ble.gattCache;
    }
    if (sub == "auth_timeouts") {
      return std::to_string(hw.ble.authTimeouts);
    }
    if (sub == "reconnects") {
      return std::to_string(hw.ble.reconnects);
    }
  }
  if (prefixed("companion.")) {
    const std::string sub = key.substr(10);
    if (sub == "enabled") {
      return hw.companionEnabled ? "yes" : "no";
    }
    if (sub == "gate") {
      return hw.companionGate;
    }
    if (sub == "state") {
      return hw.ble.companion;
    }
    if (sub == "reidentify") {
      return std::to_string(hw.ble.reidentifyCount);
    }
    if (sub == "rejected") {
      return std::to_string(hw.ble.rejectedLinks);
    }
  }
  if (prefixed("audio.")) {
    const std::string sub = key.substr(6);
    if (sub == "state") {
      return hw.peripheral.audio;
    }
    if (sub == "overcurrents") {
      return std::to_string(hw.peripheral.audioOvercurrents);
    }
  }
  if (prefixed("ir.")) {
    const std::string sub = key.substr(3);
    if (sub == "state") {
      return irStateLocked(hw);
    }
    if (sub == "timeouts") {
      return std::to_string(hw.peripheral.irTimeouts);
    }
    if (sub == "transmits") {
      return std::to_string(hw.rail.irTransmits);
    }
    if (sub == "failures") {
      return std::to_string(hw.rail.irFailures);
    }
  }
  if (prefixed("sd.")) {
    const std::string sub = key.substr(3);
    if (sub == "state") {
      return hw.peripheral.sd;
    }
    if (sub == "errors") {
      return std::to_string(hw.peripheral.sdErrors);
    }
  }
  if (key == "platform.reset_reason") {
    return hw.resetReasonName;
  }
  if (key == "boot.defaults_marker") {
    return hw.bootDefaultsMarker;
  }
  if (key == "boot.default_scans") {
    return std::to_string(hw.bootDefaultScans);
  }
  if (key == "boot.seed_attempts") {
    return std::to_string(hw.bootSeedAttempts);
  }
  if (key == "boot.marker_written") {
    return hw.bootMarkerWritten ? "yes" : "no";
  }
  if (key == "boot.psram_memtest") {
    return hw.psramMemtest ? "on" : "off";
  }
  if (key == "boot.profile") {
    return hw.bootProfile;
  }
  if (key == "boot.reset_to_app_ms") {
    return std::to_string(hw.resetToAppMs);
  }
  if (key == "wifi.fast_connect") {
    return hw.wifiFastConnect;
  }
  if (key == "wifi.auth") {
    return hw.wifiAuth;
  }
  if (key == "wifi.direct_failures") {
    return std::to_string(hw.wifiDirectFailures);
  }
  if (key == "wifi.scans") {
    return std::to_string(hw.wifiScans);
  }
  if (key == "wifi.moved_ap") {
    return hw.wifiMovedAp ? "yes" : "no";
  }
  if (key == "ota.slot") {
    return hw.otaSlot;
  }
  if (key == "ota.rollback") {
    return hw.otaRollback ? "yes" : "no";
  }
  if (key == "ota.updates") {
    return std::to_string(hw.otaUpdates);
  }
  if (key == "ota.commits") {
    return std::to_string(hw.otaCommits);
  }
  if (key == "alloc.icon") {
    return hw.iconState;
  }
  if (prefixed("mqttmodel.")) {
    const std::string sub = key.substr(10);
    if (sub == "state") {
      return hw.mqtt.state;
    }
    if (sub == "overflow") {
      return hw.mqtt.overflow ? "yes" : "no";
    }
    if (sub == "owner_stack") {
      return std::to_string(hw.mqtt.ownerStack);
    }
    if (sub == "worker_stack") {
      return std::to_string(hw.mqtt.workerStack);
    }
    if (sub == "tls") {
      return hw.mqtt.tls ? "yes" : "no";
    }
    if (sub == "ha") {
      return hw.mqtt.haState;
    }
    if (sub == "buffer") {
      return std::to_string(hw.mqtt.bufferBytes);
    }
    if (sub == "ha_peak") {
      return std::to_string(hw.mqtt.haPeak);
    }
    if (sub == "owner_gate") {
      return hw.mqtt.ownerGate;
    }
    if (sub == "start_gate") {
      return hw.mqtt.startGate;
    }
    if (sub == "discovery_gate") {
      return hw.mqtt.discoveryGate;
    }
    if (sub == "queue_bytes") {
      return std::to_string(hw.mqtt.queueBytes);
    }
    if (sub == "queue_region") {
      return hw.mqtt.queueRegion;
    }
    if (sub == "camera_bytes") {
      return std::to_string(hw.mqtt.cameraBytes);
    }
    if (sub == "main_stack") {
      return std::to_string(hw.mqtt.mainStackBytes);
    }
    if (sub == "main_high_water") {
      return std::to_string(hw.mqtt.mainHighWater);
    }
    if (sub == "main_stack_ok") {
      return hw.mqtt.mainStackBytes >= hw.mqtt.mainHighWater ? "yes" : "no";
    }
    if (sub == "ha_cache") {
      return hw.mqtt.haCache;
    }
    if (sub == "discovery_fragments") {
      return std::to_string(hw.mqtt.discoveryFragments);
    }
    if (sub == "heap_free_after") {
      return std::to_string(hw.mqtt.heapFreeAfter);
    }
  }
  return "";
}

bool hwFailClosed(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().failClosed;
}

const char *hwRtcCapability(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const rtc_fact_t *fact = rtcFactFor(state().profile);
  if (fact == nullptr) {
    return "unknown";
  }
  return state().caps.rtc == fact->rtc ? "match" : "mismatch";
}

bool hwRailFixed(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().rail.fixed;
}

bool hwRailOn(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().rail.on;
}

const char *hwRailState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().rail.on ? "on" : "off";
}

const char *hwRailHolders(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().rail.holders.c_str();
}

uint32_t hwRailRefusals(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().rail.refusals;
}

bool hwIrStartTransfer(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return irStartLocked(state());
}

bool hwIrEndTransfer(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return irEndLocked(state());
}

const char *hwIrState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return irStateLocked(state());
}

uint32_t hwIrTransmits(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().rail.irTransmits;
}

uint32_t hwIrFailures(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().rail.irFailures;
}

uint8_t hwPwrCfg(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.pwrCfg;
}

uint32_t hwPwrCfgLostUpdates(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.pwrCfgLostUpdates;
}

void hwSetDisplayOff(bool off) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  hardware_state_t &hw = state();
  hw.pmic.displayOff = off;
  if (!pwrCfgModelled(hw)) {
    return;
  }
  if (off) {
    if (hw.pmic.statusLedSaved) {
      return;
    }
    // Read the byte, remember the LED level, clear it. The EXT 5 V bit sits in
    // the same byte, so a read another holder has since invalidated puts that
    // holder's change back.
    hw.pmic.statusLedLevel = (hw.pmic.pwrCfg & PWR_CFG_LED_CTRL) != 0;
    const bool stale = hw.pmic.pwrCfgStale;
    hw.pmic.pwrCfgStale = false;
    (void)pwrCfgWriteMaskedLocked(hw, PWR_CFG_LED_CTRL, 0, stale);
    hw.pmic.statusLedSaved = true;
    return;
  }
  if (!hw.pmic.statusLedSaved) {
    return;
  }
  const uint8_t level = hw.pmic.statusLedLevel ? PWR_CFG_LED_CTRL : 0;
  (void)pwrCfgWriteMaskedLocked(hw, PWR_CFG_LED_CTRL, level, false);
  hw.pmic.statusLedSaved = false;
}

const char *hwDisplayOffState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const hardware_state_t &hw = state();
  if (pwrCfgModelled(hw)) {
    // The panel-off path is the status LED level bit, so a lost update that
    // clears another bit still reads back through this one.
    return hw.pmic.statusLedSaved ? "yes" : (hw.pmic.displayOff ? "yes" : "no");
  }
  return hw.pmic.displayOff ? "yes" : "no";
}

void hwNoteWatchdogCycle(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  hardware_state_t &hw = state();
  hw.pmic.watchdogCycles++;
  // A PMIC watchdog expiry power-cycles the system: the outputs drop and come
  // back, and the SoC restarts under the PMIC's reset. The host has no reset
  // line, so the cycle is recorded here instead of ending the process.
  hw.power.offs++;
  hw.power.state = power_state_t::OFF;
  hw.power.state = power_state_t::ON;
  hw.resetReason = ESP_RST_POWERON;
  hw.resetReasonName = "pmic_watchdog";
  // A power cycle returns PWR_CFG to the documented reset value with the 5 V
  // boost off, which is the state the firmware leaves it in at boot.
  hw.pmic.pwrCfg = PWR_CFG_RESET;
  applyRailFromPwrCfgLocked(hw);
  hw.pmic.pwrCfgSnapshot = hw.pmic.pwrCfg;
  ESP_LOGW(LOG_TAG, "PMIC watchdog expired; system power cycle %u",
           static_cast<unsigned>(hw.pmic.watchdogCycles));
}

uint32_t hwWatchdogCycles(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.watchdogCycles;
}

bool hwBatteryPresent(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.batteryPresent;
}

bool hwGaugeLevel(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.batteryPresent && state().pmic.gauge != "none";
}

bool hwGaugeVoltage(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const std::string &gauge = state().pmic.gauge;
  return state().pmic.batteryPresent
         && (gauge == "voltage" || gauge == "voltage_current" || gauge == "voltage_charge");
}

bool hwGaugeCurrent(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const std::string &gauge = state().pmic.gauge;
  return state().pmic.batteryPresent && (gauge == "voltage_current");
}

bool hwGaugeCharge(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const std::string &gauge = state().pmic.gauge;
  return state().pmic.batteryPresent
         && (gauge == "voltage_current" || gauge == "voltage_charge" || gauge == "four_level");
}

bool hwTouch(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().caps.touch;
}

bool hwPowerOff(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return pmicPowerOffLocked(state());
}

void hwSetPanelOff(bool off) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.panelOff = off;
  if (off) {
    return;
  }
  // A lit panel means the load is no longer in the light-load window.
  state().pmic.lightLoadElapsedMs = 0;
}

bool hwCanTurnDisplayOff(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const hardware_state_t &hw = state();
  // Only the IP5306 has a light-load cut that turns a panel-off state into a
  // power cut. Every other modelled part answers yes, so this gate stays open
  // on the boards it does not describe.
  return hw.pmic.variant != "ip5306" || hw.pmic.boostKeepOn;
}

void hwTick(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  pmicLightLoadTickLocked(state(), clockMillis());
}

int hwBatteryPercent(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.batteryPercent;
}

int hwBatteryMillivolts(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.batteryMillivolts;
}

int hwBatteryCurrentMa(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.chargeCurrentMa;
}

bool hwBatteryCharging(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.charging;
}

void hwWifiSetConnected(bool connected) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().wifi.state = connected ? "connected" : "disabled";
  state().wifi.reason = connected ? "none" : "user";
  if (!connected) {
    state().wifi.attempts = 0;
    state().wifi.retryMs = 0;
  }
}

bool hwWifiConnected(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.state == "connected";
}

const char *hwWifiState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.state.c_str();
}

const char *hwWifiReason(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.reason.c_str();
}

const char *hwWifiIp(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.ip.c_str();
}

const char *hwWifiSsid(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.ssid.c_str();
}

int hwWifiRssi(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.rssi;
}

uint32_t hwWifiAttempts(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.attempts;
}

uint32_t hwWifiRetryMs(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.retryMs;
}

bool hwNtpSynced(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.ntpSynced;
}

const char *hwMqttModelState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.state.c_str();
}

bool hwMqttModelOverflow(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.overflow;
}

size_t hwMqttOwnerStack(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.ownerStack;
}

size_t hwMqttWorkerStack(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.workerStack;
}

const char *hwMqttHaState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.haState.c_str();
}

size_t hwMqttBufferBytes(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.bufferBytes;
}

void hwSetWatchdogEnabled(bool enabled) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.watchdogEnabled = enabled;
  if (enabled) {
    state().pmic.watchdogFeeds = 0;
  }
}

void hwSetDownloadLocked(bool locked) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.downloadLocked = locked;
}

uint32_t hwWatchdogFeeds(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.watchdogFeeds;
}

void hwNoteWatchdogFeed(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  if (state().pmic.watchdogEnabled) {
    state().pmic.watchdogFeeds++;
  }
}

}  // namespace Furble::Sim

extern "C" uint32_t furble_sim_hw_reset_reason(void) {
  return Furble::Sim::hwResetReason();
}
