#ifndef FURBLE_SIM_HARDWARE_MODEL_H
#define FURBLE_SIM_HARDWARE_MODEL_H

#include <cstddef>
#include <cstdint>
#include <string>

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

/** MQTT stack model observability. */
const char *hwMqttModelState(void);
bool hwMqttModelOverflow(void);
size_t hwMqttOwnerStack(void);
size_t hwMqttWorkerStack(void);
const char *hwMqttHaState(void);
size_t hwMqttBufferBytes(void);

/** Platform/P MIC state changed by production calls. */
void hwSetWatchdogEnabled(bool enabled);
void hwSetDownloadLocked(bool locked);
uint32_t hwWatchdogFeeds(void);
void hwNoteWatchdogFeed(void);

}  // namespace Furble::Sim

#endif
