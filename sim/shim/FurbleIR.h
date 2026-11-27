#ifndef FURBLE_SIM_FURBLE_IR_H
#define FURBLE_SIM_FURBLE_IR_H

#include <cstdint>

#include "FurblePlatform.h"
#include "FurbleRail.h"
#include "FurbleSimCaps.h"

#include "clock.h"
#include "hardware_model.h"

namespace Furble {

// The simulator has no RMT peripheral. The fake reports unsupported so the
// IR trigger and its settings menu stay hidden, which keeps the scripted
// menu routes at their existing positions. A capture run can set
// FURBLE_SIM_IR to report an IR LED present so the Infrared submenu renders.
//
// With the emitter present, fire() models the production rail ownership: the
// firmware's IR task takes the same EXT_5V hold when it dequeues a request and
// releases it when the burst is over (src/FurbleIR.cpp). The simulator has no
// task or queue, so fire() is the burst start here: it takes the hold, and
// poll() releases it when the modelled window ends. The window is virtual
// milliseconds, seeded by `ir_burst_ms`, so a scenario can overlap it with a GPS
// probe failure or a duty-cycle RAIL_OFF.
// It models the ownership rule, not the RMT waveform: the production wait bound
// is RMT_TX_TIMEOUT_MS (1000 ms).
class IR {
 public:
  enum class protocol_t : uint8_t {
    NIKON = 0,
    SONY = 1,
    CANON = 2,
    CANON_DELAYED = 3,
  };

  static IR &getInstance(void) {
    static IR instance;
    return instance;
  }

  static protocol_t clampProtocol(uint8_t value) {
    if (value > static_cast<uint8_t>(protocol_t::CANON_DELAYED)) {
      return protocol_t::NIKON;
    }
    return static_cast<protocol_t>(value);
  }

  static void init(void) {}

  bool isSupported(void) const { return Furble::Sim::capEnabled("FURBLE_SIM_IR"); }

  void fire(void) {
    // The stored protocol reaches the RMT encoder only, which the host does
    // not model, so every modelled burst has the same rail behaviour.
    fire(protocol_t::NIKON);
  }

  void fire(protocol_t protocol) {
    (void)protocol;
    if (!isSupported()) {
      return;
    }

    // The hardware model records the frame so a scenario can assert the mark
    // level of the modelled board and what a light pulse on a shared pin does
    // while the frame is in flight.
    Sim::hwIrFrame();

    // Same order as firmware: take the hold before the request is queued, so a
    // GPS RAIL_OFF between the two cannot cut the burst.
    m_RailHeld = Platform::getInstance().railAcquire(PowerBackend::Rail::EXT_5V, RailHolder::IR);
    m_BurstEndMs = Furble::Sim::clockMillis() + Furble::Sim::hwIrBurstMs();
    m_Bursts++;
  }

  /** Release the hold once the modelled burst window has elapsed. */
  void poll(void) {
    if (!m_RailHeld || (Furble::Sim::clockMillis() < m_BurstEndMs)) {
      return;
    }

    m_RailHeld = false;
    (void)Platform::getInstance().railRelease(PowerBackend::Rail::EXT_5V, RailHolder::IR);
  }

  /** True while a modelled burst holds the EXT_5V rail. */
  bool railHeld(void) const { return m_RailHeld; }

  /** Count of modelled bursts, for scenario assertions. */
  uint32_t bursts(void) const { return m_Bursts; }

 private:
  IR() = default;

  bool m_RailHeld = false;
  uint32_t m_BurstEndMs = 0;
  uint32_t m_Bursts = 0;
};

}  // namespace Furble

#endif
