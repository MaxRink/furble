#ifndef FURBLE_COMPANION_IDENTITY_H
#define FURBLE_COMPANION_IDENTITY_H

#include <cstdint>

namespace Furble {
namespace CompanionIdentity {

/**
 * Apply the persisted companion random-static LE identity, if one exists.
 *
 * Companion reidentify rotates the controller address so a host rebuilds its
 * cached GATT handle map. The address must be restored before advertising on
 * the next boot, or the host loses the bond it created after the rotation.
 * Returns false when no valid identity is stored or the controller rejects it.
 */
bool applyPersisted(void);

/**
 * Generate, persist and apply a new random-static LE identity.
 *
 * Only the controller identity and its NVS key change. The shared NimBLE bond
 * store, including every camera bond, is untouched.
 */
bool rotate(void);

#if defined(FURBLE_HOST_TEST)
/** Remove the persisted identity, modelling a factory wipe. */
void clearPersisted(void);
#endif

}  // namespace CompanionIdentity
}  // namespace Furble

#endif
