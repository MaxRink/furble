#ifndef FURBLE_SIM_HARDWARE_MODEL_H
#define FURBLE_SIM_HARDWARE_MODEL_H

#include <cstddef>
#include <cstdint>
#include <string>

// Declared, not included: the row is only dereferenced by callers that already
// include FurbleBoard.h, and this header is reachable from simulator
// translation units that must not pull M5Unified in.
namespace Furble {
namespace Board {
struct Profile;
}  // namespace Board
}  // namespace Furble

namespace Furble::Sim {

/*
 * Board, PMIC, power-button, USB and radio state model.
 *
 * The SDL build compiles one panel profile at a time. This model carries the
 * rest of the board identity so a scenario can select a supported profile and
 * exercise the power path, PMIC class, USB transport, reset reason and radio
 * gate that production code reads. It models those boundaries; it does not
 * replace production drivers for modules the simulator does not compile
 * (Ethernet, IR, SD writer, audio, full Companion GATT).
 */

/** Select a profile. Unknown names are rejected at scenario parse time. */
void hwSetProfile(const std::string &profile);

/**
 * The `Board::Profile` row the seeded profile models.
 *
 * The simulator compiles one panel, but a scenario may model any supported
 * board. Firmware facts that live in `Board::Profile` are read through this
 * row, so a scenario cannot assert a value the firmware does not own.
 */
const Board::Profile &hwBoardProfile(void);

/** Battery capacity of the modelled row, 0 when unknown. */
const char *hwProfile(void);
const char *hwMcu(void);
const char *hwPmicVariant(void);
const char *hwUsbTransport(void);
const char *hwGauge(void);
const char *hwDisplayClass(void);

/** True when the selected profile's RTC capability matches its verified row. */
const char *hwRtcCapability(void);

/**
 * A consumer of the board's switched EXT 5 V output.
 *
 * The M5PM1 EXT 5 V (PWR_CFG BOOST_EN) feeds Port A and the IR LED and
 * receiver, so the rail is one shared output with more than one consumer.
 */
enum class rail_holder_t { GPS, IR };

/**
 * Ask for the rail on behalf of one holder and return the read-back.
 *
 * The hardware model owns the rail because the board has one switch and more
 * than one holder. A cut is refused while another holder still needs the rail,
 * and on a board or PMIC that cannot switch it (`seed gps_rail_fixed`,
 * `seed gps_rail_refused`). The caller compares the read-back with the request,
 * which is the contract Platform::setRail returns to the GPS driver.
 */
bool hwSetRail(rail_holder_t holder, bool wanted);
bool hwRailFixed(void);
bool hwRailOn(void);
const char *hwRailState(void);
const char *hwRailHolders(void);
uint32_t hwRailRefusals(void);

/** Model an IR transfer window. Both ends need the 5 V rail. */
bool hwIrStartTransfer(void);
bool hwIrEndTransfer(void);
const char *hwIrState(void);
uint32_t hwIrTransmits(void);
uint32_t hwIrFailures(void);

/**
 * One PWR_CFG byte (M5PM1 v1.0.6 register 0x06), shared by the charge enable,
 * the DCDC and LDO enables, the EXT 5 V boost and the status LED level.
 *
 * The panel-off path saves and clears the LED level in that byte, so a write
 * built from a read taken before another holder changed the rail loses that
 * change. `seed pmic_pwr_cfg_stale` makes the next LED read-modify-write use the
 * snapshot taken at boot so the loss is representable and counted.
 */
uint8_t hwPwrCfg(void);
uint32_t hwPwrCfgLostUpdates(void);
void hwSetDisplayOff(bool off);
const char *hwDisplayOffState(void);

/** Record a PMIC watchdog expiry, which power-cycles the system. */
void hwNoteWatchdogCycle(void);
uint32_t hwWatchdogCycles(void);

void hwSetPsramMode(const std::string &mode);
void hwSetPmicVariant(const std::string &variant);
void hwSetBatteryGauge(const std::string &gauge);

/** Seed the modelled load current in milliamps. */
void hwSetLoadMa(int loadMa);

/**
 * Seed the modelled IP5306 light-load window in milliseconds, overriding the
 * value REG_SYS_CTL2 bits 3:2 decode to.
 */
void hwSetLightLoadWindowMs(uint32_t windowMs);

/**
 * Seed the modelled IP5306 REG_SYS_CTL2 byte. The light-load window follows its
 * bits 3:2, so a scenario can decode a field value the vendor block does not
 * select.
 */
void hwSetIp5306SysCtl2(uint8_t value);
void hwSetResetReasonName(const std::string &name);
void hwSetBatteryState(const std::string &state);
void hwSetMqttStackModel(bool enabled,
                         size_t ownerStack,
                         size_t workerStack,
                         bool tls,
                         bool expectOverflow);
void hwSetMqttHa(bool enabled);
void hwSetMqttBuffer(size_t bytes);
void hwSetMqttMainStack(size_t bytes, size_t highWater);
void hwSetBootDefaultsMarker(const std::string &marker);
void hwSetBootProfile(const std::string &profile);
void hwSetPsramMemtest(bool enabled);
void hwSetWifiFastConnect(const std::string &state);
void hwSetWifiAuth(const std::string &auth);
void hwSetMqttDiscoveryPayload(size_t bytes);

/** Rail switch capability, seeded from the scenario before the boot path runs. */
void hwSetRailFixed(bool fixed);
void hwSetRailRefused(bool refused);

/** Snapshot the PWR_CFG byte so the next LED write can lose a later change. */
void hwSetPwrCfgStale(bool stale);

/** Apply defaults and emit the boot diagnostics. Called after seeds apply. */
void hwStart(void);

/** Reset the current boot's reason without a reboot. */
void hwSetResetReason(uint32_t reason);
uint32_t hwResetReason(void);

/** Apply one hardware action. Returns false when the action is unknown. */
bool hwAction(const std::string &name);

/** Resolve a `hw.*`, `pmic.*`, `power.*`, `usb.*`, `wifi.*`, `companion.*`,
 * `audio.*`, `ir.*`, `sd.*`, `net.*` or `mqttmodel.*` query. */
std::string hwQuery(const std::string &key);

/** True when a modelled quirk violation has been recorded. */
bool hwFailClosed(void);

/** Touch panel capability of the modelled board, used by the SDL platform. */
bool hwTouch(void);

/**
 * Run the modelled board's power-off path.
 *
 * Returns what Platform::powerOff() must report. A board whose modelled rail
 * cannot drop returns false and stays powered: the IP5306 has no off command,
 * only a low-load boost cutoff.
 */
bool hwPowerOff(void);

/** Record the modelled panel state so the IP5306 light-load timer can run. */
void hwSetPanelOff(bool off);

/**
 * True when the modelled display may be switched off.
 *
 * `Platform::canTurnDisplayOff()` reports this. A board without an IP5306
 * answers yes. On an IP5306 board the answer is the boost keep-on bit: with the
 * bit clear a panel-off load falls below the 45 mA light-load floor and the
 * charger would remove the boost, so the firmware keeps the panel dim rather
 * than off (plan 354).
 */
bool hwCanTurnDisplayOff(void);

/** Advance the modelled board's time-dependent power path by one tick. */
void hwTick(void);

/** Battery availability and gauge capability used by Platform. */
bool hwBatteryPresent(void);
bool hwGaugeLevel(void);
bool hwGaugeVoltage(void);
bool hwGaugeCurrent(void);
bool hwGaugeCharge(void);
int hwBatteryPercent(void);
int hwBatteryMillivolts(void);
int hwBatteryCurrentMa(void);
bool hwBatteryCharging(void);

/** WiFi state consumed by the simulator WiFi shim and the WebUI gate. */
void hwWifiSetConnected(bool connected);
bool hwWifiConnected(void);
const char *hwWifiState(void);
const char *hwWifiReason(void);
const char *hwWifiIp(void);
const char *hwWifiSsid(void);
int hwWifiRssi(void);
uint32_t hwWifiAttempts(void);
uint32_t hwWifiRetryMs(void);
bool hwNtpSynced(void);

/*
 * Battery-backed RTC model.
 *
 * The modelled chip is the calendar the firmware reads through M5.Rtc on
 * device. The host has no I2C bus, so the simulator supplies the sample
 * through TimeKeeper's simulator hooks instead; production logic is unchanged.
 * A scenario installs the chip, sets its calendar and decides whether a cell
 * keeps it through a power-off. `hw.rtc`, the static capability, stays the
 * board-profile answer and is independent of this model.
 */
enum class rtc_mode_t { OFF, UNBACKED, BACKED };

/** Install the chip: off, present without a cell, or battery-backed. */
void hwSetRtcModel(const std::string &mode);
/** Set the calendar reading, in seconds since the Unix epoch. */
void hwSetRtcEpoch(uint64_t seconds);
/** Raise or clear the voltage-low flag. The registers keep their last value;
 *  production refuses a cell-backed reading while the flag is set. */
void hwSetRtcClockLost(bool lost);

/** True when the model has a chip installed. */
bool hwRtcPresent(void);
bool hwRtcBatteryBacked(void);
/** The register reading, valid calendar or not. */
uint64_t hwRtcEpochSeconds(void);
/** A calendar sample for TimeKeeper. False when there is none. */
bool hwRtcRead(uint64_t &epoch_us, bool &volt_low);
/** Record a calendar write, as M5.Rtc.setDateTime() would. */
void hwRtcWrite(uint64_t epoch_us);
/** True when the modelled profile wakes through the RTC alarm. */
bool hwRtcTimedAlarmBoard(void);
/** The chip kept counting through a simulated power-off. Persists the
 *  reading so the next process boots from the same chip state. */
void hwRtcPowerOff(uint32_t seconds);
/** Persist the chip state before a simulated reset. */
void hwRtcPersist(void);

/*
 * The RTC-domain running clock.
 *
 * On device, settimeofday() writes the RTC_HRT backed system clock and
 * M5.begin() seeds it from the calendar chip. `std::time()` and TimeKeeper's
 * simulator hooks read this model, so a scenario can assert the clock a timed
 * wake or a stored checkpoint recovered instead of only the chip registers.
 * Installing or seeding a chip with a reading seeds it too. It is boot-scoped:
 * a restart re-execs the binary and the next boot seeds it again from the chip
 * or the stored checkpoint, as a power-on does.
 */
void hwSetSystemTime(uint64_t epoch_us);
/** The modelled running clock. 0 when nothing has set it this boot. */
uint64_t hwSystemTimeUs(void);
bool hwSystemTimeSet(void);

/** MQTT stack model observability. */
const char *hwMqttModelState(void);
bool hwMqttModelOverflow(void);
size_t hwMqttOwnerStack(void);
size_t hwMqttWorkerStack(void);
const char *hwMqttHaState(void);
size_t hwMqttBufferBytes(void);

/**
 * What the modelled M5PM1 PWR_SRC register reports.
 *
 * `battery` (the default), `usb`, `port` (an external 5 V source driving the
 * bidirectional 5 V port) or `unknown` (no supply reported); `unreadable`
 * makes the register read fail. It drives the EXT_5V output-mode interlock in
 * the simulated Platform, so a scenario can prove a refusal.
 */
void hwSetRailSupply(const std::string &supply);
/** The modelled PWR_SRC answer, by value: the caller owns its copy. */
std::string hwRailSupplyName(void);

/**
 * Window the simulated IR fake holds the EXT_5V rail for one burst.
 *
 * Virtual milliseconds, seeded by `ir_burst_ms` (default 100). It models the
 * ownership overlap only: the production bound on the transmit itself is
 * RMT_TX_TIMEOUT_MS in src/FurbleIR.cpp, not this value.
 */
void hwSetIrBurstMs(uint32_t milliseconds);
uint32_t hwIrBurstMs(void);

/**
 * The modelled calendar RTC (BM8563) countdown.
 *
 * `hwRtcProgram()` returns the seconds the driver would have programmed, the
 * same value `M5.Rtc.setAlarmIRQ(int)` returns, so a caller can apply the
 * firmware acceptance rule from FurbleRtcCountdown.h. The register arithmetic
 * is UNVERIFIED against the BM8563 datasheet; see that header.
 */
bool hwRtcEnabled(void);
int hwRtcProgram(int seconds);
/**
 * Record the firmware verdict for a request in the `rtc.plan.<seconds>` journal.
 *
 * `maxEarlySeconds` is `Platform::TIMED_WAKE_MAX_EARLY_S` from the caller, so
 * the model applies the same window the firmware does rather than a copy.
 */
void hwRtcNotePlan(int seconds, uint32_t maxEarlySeconds);
void hwRtcDisableIrq(void);
void hwRtcClearIrq(void);
void hwRtcClearTimer(void);
bool hwRtcIrq(void);
/** Latch the alarm flag, as a wake from the RTC's own power domain would. */
void hwRtcLatchIrq(void);

/**
 * The Plus2 GPIO HOLD power latch, separate from any PMIC latch.
 *
 * Production drives the pin high immediately after `M5.begin()` and drops it
 * to power off for a timed wake. `hwMarkPlatformReady()` records the end of
 * `Platform::getInstance()`, so `pmic.hold_order` can distinguish a latch
 * asserted inside that window from one asserted later.
 */
void hwAssertHoldLatch(void);
void hwReleaseHoldLatch(void);
void hwMarkPlatformReady(void);

/** IR and feedback LED pin model, including the Plus2 shared G19. */
void hwIrFrame(void);
bool hwIrBusy(void);
void hwLightPulse(void);

/** True when the modelled board's Port A 5 V rail is on at boot. */
bool hwRailBootOn(void);

/** Platform/P MIC state changed by production calls. */
void hwSetWatchdogEnabled(bool enabled);
void hwSetDownloadLocked(bool locked);
uint32_t hwWatchdogFeeds(void);
void hwNoteWatchdogFeed(void);

}  // namespace Furble::Sim

#endif
