#ifndef FURBLE_HEADLESS_CRASH_LOG_H
#define FURBLE_HEADLESS_CRASH_LOG_H

namespace Furble {
namespace CrashLog {
void init(void);
void update(void);
bool recovered(void);
}  // namespace CrashLog
}  // namespace Furble

#endif
