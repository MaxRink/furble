#ifndef FURBLE_HOST_PEER_STALL_H
#define FURBLE_HOST_PEER_STALL_H

#include <chrono>
#include <cstdint>
#include <thread>

namespace Furble {
namespace Host {

/**
 * Which clock a virtual peer's modelled block runs on.
 *
 * Some NimBLE calls are opaque: they take no cancel token and return only when
 * the controller finishes the procedure or the link goes away.
 * `NimBLEClient::secureConnection()` is the one that matters, because
 * `Camera::connect()` holds `Camera::m_Mutex` across it. A peer that answers it
 * instantly cannot reproduce anything that depends on a connect being in
 * flight, which is why every certified simulator scenario used to cancel into
 * an empty window.
 *
 * Nothing is installed by default, and a peer reads that as "the host clock is
 * the only clock here": it parks on its own condition variable, which is what
 * the host harness wants and what a terminate releases directly. The simulator
 * runs a virtual clock no host condition variable can wait on, so it installs a
 * virtual-time delay and the peer spends its deadline in slices on it,
 * re-reading the terminate between them. Same semantics, different clock.
 *
 * The hook is a plain function pointer with no state of its own, so installing
 * it cannot change any peer's behaviour beyond where the time comes from.
 */
using peer_stall_fn_t = void (*)(uint32_t ms);

inline peer_stall_fn_t &peerStallFunction(void) {
  static peer_stall_fn_t function = nullptr;
  return function;
}

/**
 * Install the clock a modelled block runs on. nullptr restores the default,
 * which a peer reads as "park on the host clock".
 */
inline void setPeerStallFunction(peer_stall_fn_t function) {
  peerStallFunction() = function;
}

/** Hold the calling task for `ms` on whichever clock is installed. */
inline void peerStall(uint32_t ms) {
  if (ms == 0) {
    return;
  }
  const peer_stall_fn_t function = peerStallFunction();
  if (function != nullptr) {
    function(ms);
    return;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace Host
}  // namespace Furble

#endif
