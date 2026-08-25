#ifndef FURBLE_HOST_PREFERENCES_STUB_H
#define FURBLE_HOST_PREFERENCES_STUB_H

#include <cstddef>

namespace Furble {

// Test-only deterministic NVS fault injection. A value of zero disables
// injection; otherwise the Nth mutating operation fails without changing the
// in-memory store.
void hostPreferencesFailAfter(std::size_t operation);
void hostPreferencesResetFaults(void);
void hostPreferencesClearStorage(void);
std::size_t hostPreferencesMutationCount(void);
bool hostPreferencesHasKey(const char *key);

}  // namespace Furble

#endif
