#ifndef FURBLE_SIM_WEBUI_MEMORY_MODEL_H
#define FURBLE_SIM_WEBUI_MEMORY_MODEL_H

#include <cstdint>

namespace Furble::Sim {

/*
 * Admission model for the optional HTTPS WebUI.
 *
 * The SDL build does not link the HTTPS server, mbedTLS or the esp_http_server
 * task, so the one behaviour a StickS3 scenario most needs to see is missing:
 * whether the internal heap lets the supervisor and server start at all. This
 * model runs the production gate functions from FurbleWebUIMemory.h against the
 * modelled internal heap and logs the production fail-closed sentence. It
 * models the two task allocations at the heap boundary; it does not serve HTTP,
 * generate an identity or claim TLS parity.
 *
 * `seed webui_gate off` skips the supervisor gate to reproduce the pre-#392
 * shape, where an 8 KiB supervisor allocation was attempted under pressure.
 */

/** Configure before heapActivate()/webuiMemoryStart(). */
void webuiMemoryConfigure(bool enabled, bool gateEnabled, bool credentialsReady);

/** Apply the supervisor gate and allocation. Called once after boot. */
void webuiMemoryStart(void);

/** Supervisor period, called from the UI task. Uses the virtual clock. */
void webuiMemoryTick(void);

/** Human-readable state for scenario assertions. */
const char *webuiSupervisorState(void);
const char *webuiServerState(void);
const char *webuiGateReason(void);
uint32_t webuiServerAttempts(void);
bool webuiFailClosed(void);

/** The last production-format gate sentence, or an empty string. */
const char *webuiGateMessage(void);

}  // namespace Furble::Sim

#endif
