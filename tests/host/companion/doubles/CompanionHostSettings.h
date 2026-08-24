#ifndef FURBLE_COMPANION_HOST_SETTINGS_H
#define FURBLE_COMPANION_HOST_SETTINGS_H

// Include through this distinct name because FurbleCompanionService.h lives in
// include/ and otherwise resolves its quoted FurbleSettings.h include there
// before the host target's shim include path is considered.
#include "FurbleSettings.h"

#endif
