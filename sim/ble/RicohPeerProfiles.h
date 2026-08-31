#ifndef FURBLE_SIM_RICOH_PEER_PROFILES_H
#define FURBLE_SIM_RICOH_PEER_PROFILES_H

#include "VirtualBleRuntime.h"

namespace Furble {
namespace Sim {
namespace Ble {

enum class RicohProfileKind : uint8_t {
  GR_IV,
  GR_IV_HDF,
  PENTAX_K3_III,
  PENTAX_K3_III_MONO,
  UNKNOWN,
};

// Profiles are immutable synthetic discovery fixtures. No profile below
// claims hardware ATT handles or command semantics for any Ricoh-family body.
const PeerProfile &ricohProfile(RicohProfileKind kind);

}  // namespace Ble
}  // namespace Sim
}  // namespace Furble

#endif
