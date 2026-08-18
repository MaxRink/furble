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

}  // namespace Furble::Sim

#endif
