#ifndef FURBLE_HEADLESS_STATE_H
#define FURBLE_HEADLESS_STATE_H

#include <cstdint>
#include <string>
#include <vector>

namespace HeadlessState {

inline std::vector<std::string> boot;
inline uint32_t platformUpdates = 0;
inline uint32_t crashUpdates = 0;
inline uint32_t gpsUpdates = 0;
inline uint32_t scanProcessed = 0;
inline uint32_t feedbackSignals = 0;
inline uint32_t feedbackReloads = 0;
inline uint32_t gpsReloads = 0;
inline uint32_t irInit = 0;
inline uint32_t sdInit = 0;
inline uint32_t wifiInit = 0;
inline uint32_t gpsInit = 0;
inline uint32_t bleInit = 0;
inline uint32_t webuiEarly = 0;
inline uint32_t webuiInit = 0;
inline uint32_t mqttInit = 0;
inline uint32_t ethernetInit = 0;
inline uint32_t consoleInit = 0;
inline uint32_t bootSteps = 0;
inline uint32_t bootFinish = 0;
inline uint32_t cpuFreqSets = 0;
inline uint32_t watchdogEnables = 0;
inline uint32_t controlTasks = 0;
inline uint32_t controlConnects = 0;
inline uint32_t controlDisconnects = 0;
inline bool consoleQueued = false;
inline int64_t nowUs = 0;

}  // namespace HeadlessState

#endif
