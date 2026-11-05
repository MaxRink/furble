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
void hwSetBootDefaultsMarker(const std::string &marker);
void hwSetBootProfile(const std::string &profile);
void hwSetPsramMemtest(bool enabled);
void hwSetWifiFastConnect(const std::string &state);
void hwSetWifiAuth(const std::string &auth);
void hwSetMqttDiscoveryPayload(size_t bytes);

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
