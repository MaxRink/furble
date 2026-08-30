#ifndef FURBLE_SIM_DRIVER_H
#define FURBLE_SIM_DRIVER_H

#include <cstdint>
#include "scenario_action.h"

namespace Furble {
class UI;
}

namespace Furble::Sim {

struct battery_reading_t {
  uint8_t level;
  uint16_t voltage;
  int32_t current;
  bool charging;
};

void configure(int argc, char **argv);
void startProfiler(void);
void preparePreferences(void);
void applyScenarioSettings(void);
bool scenarioSettingIsTrue(const char *name);
void registerUI(Furble::UI *ui);
void setBackTarget(Furble::UI *ui);
void driverTick(void);
/** Notify the fuzzer after the UI task completes its real LVGL cycle. */
void fuzzCycleComplete(Furble::UI *ui);

/** Request an orderly simulator shutdown with the supplied process result. */
void requestExit(int result);

/** Request failure shutdown, upgrading an earlier success result if needed. */
void requestFailureExit(void);

/** Return true once a scenario, fuzzer, or panel close requested shutdown. */
bool exitRequested(void);

/** Return the first requested simulator process result, or zero if unset. */
int exitResult(void);

/** Return how many times the continuous Connected-state liveness check fired. */
uint32_t livenessViolationCount(void);

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
/** Stop and join rig socket workers without destroying timer callback state. */
void quiesceRig(void);
/** Destroy the quiesced rig after the timer dispatcher has joined. */
void stopRig(void);
bool rigRequested(void);
bool rigIsEnabled(void);
bool rigHasPendingPairing(void);
uint32_t rigPendingPairingPin(void);
void rigConfirmPairing(bool accept);
void rigReloadSetting(bool pairingWindow);

/** Read the deterministic battery sample used by the simulator. */
battery_reading_t batteryReading(void);

/** Record a simulated power-off request without terminating the test process. */
void notePowerOff(void);

// Force a pending companion pairing without a rig TCP peer. The UI pairing
// timer then raises the real modal, so the input-after-approve regression
// (task #32) can be reproduced headlessly. rigConfirmPairing clears it.
void rigInjectPendingPairing(uint32_t pin);

// Injected IMU state for the host build. The firmware reads the sensor through
// M5.Imu, which the simulator has no hardware for, so these mirror the same
// read surface (enabled, update, getAccel, getGyro). A scenario drives the
// device flat, tilted or on its side, and the firmware level, gesture and
// motion code paths read it exactly as they read the real IMU. General enough
// for the spirit level today and the motion features (gestures #45, wake on
// motion #48, gps motion #65) later.
void imuSetEnabled(bool enabled);
bool imuEnabled(void);
void imuUpdate(void);
void imuSetAccel(float x, float y, float z);
bool imuGetAccel(float *x, float *y, float *z);
void imuSetGyro(float x, float y, float z);
bool imuGetGyro(float *x, float *y, float *z);
void imuSetAccelAvailable(bool available);
void imuSetGyroAvailable(bool available);

// Set the gravity vector from a roll and pitch orientation in degrees, using
// the same convention the spirit level derives from the accelerometer
// (roll = atan2(ay, az), pitch = atan2(-ax, hypot(ay, az))).
void imuSetOrientation(float rollDeg, float pitchDeg);

}  // namespace Furble::Sim

#endif
