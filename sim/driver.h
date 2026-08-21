#ifndef FURBLE_SIM_DRIVER_H
#define FURBLE_SIM_DRIVER_H

#include <cstdint>

namespace Furble {
class UI;
}

namespace Furble::Sim {

void configure(int argc, char **argv);
void startProfiler(void);
void preparePreferences(void);
void applyScenarioSettings(void);
bool scenarioSettingIsTrue(const char *name);
void registerUI(Furble::UI *ui);
void setBackTarget(Furble::UI *ui);
void driverTick(void);

// End-to-end scenario support. connectShouldFail() lets a scenario model a
// camera that never establishes a link, mirroring the stale-connected hardware
// bug at the UI layer. The shutter and focus counters expose how many of each
// command the fake camera received so scenarios can assert the control path
// fired, including which one the one-button dispatch chose.
bool connectShouldFail(void);
uint32_t cameraShutterPresses(void);
uint32_t cameraShutterReleases(void);
uint32_t cameraFocusPresses(void);
uint32_t cameraFocusReleases(void);

void rigConfigure(bool requested,
                  uint16_t port,
                  bool ignoreUuidMismatch,
                  bool dropNotify,
                  uint32_t delayMs);
void startRig(void);
bool rigRequested(void);
bool rigIsEnabled(void);
bool rigHasPendingPairing(void);
uint32_t rigPendingPairingPin(void);
void rigConfirmPairing(bool accept);
void rigReloadSetting(bool pairingWindow);

// Force a pending companion pairing without a rig TCP peer. The UI pairing
// timer then raises the real modal, so the input-after-approve regression
// (task #32) can be reproduced headlessly. rigConfirmPairing clears it.
void rigInjectPendingPairing(uint32_t pin);

}  // namespace Furble::Sim

#endif
