#ifndef FURBLE_BATTERY_SAVER_H
#define FURBLE_BATTERY_SAVER_H

#include <cstdint>

namespace Furble {

// Battery Saver is an opt-in power profile. When it is on, the effective value
// of a bundle of individual power settings is forced to its battery optimal
// value. The stored individual settings are never touched, so turning the
// profile off restores the user's own choices with no bookkeeping. This module
// holds only the pure override arithmetic so a host test can link it directly,
// mirroring include/FurbleReconnectBackoff.h.
//
// The forced values and their audit rationale are in plans/77-battery-saver.
namespace BatterySaver {

// Forced bundle values. Encodings match the settings they override:
//   kScanMode 1     == Scan::Mode::BALANCED (25 percent duty)
//   kInactivity 2   == 60 seconds, per UI::m_InactivityValues / m_InactivityTimeouts
//   kDisplayOff 1   == "Off" mode, per UI::m_DisplayOffOptions ("Dim\nOff\n...")
static constexpr bool kSleepConn = true;     // light sleep while connected (StickS3 only)
static constexpr bool kConnSaver = true;     // idle connection parameters
static constexpr bool kReconBackoff = true;  // exponential reconnect backoff
static constexpr uint8_t kScanMode = 1;      // balanced scan duty
static constexpr uint8_t kInactivity = 2;    // screen off after 60 seconds
static constexpr uint8_t kDisplayOff = 1;    // panel off, not dim

// Given the profile flag and the user's stored value, return the effective
// value. When the profile is off, the stored value passes through unchanged.

// sleepConn is gated on board capability. Light sleep while connected only
// helps on the StickS3, and on the other boards it would drop the UART clock
// during GPS NMEA bursts because the GPS burst lock is StickS3 only. On an
// unsupported board the profile leaves sleep-while-connected at its stored
// value, which the UI keeps off there anyway.
constexpr bool sleepConn(bool active, bool stored, bool supported) {
  return (active && supported) ? kSleepConn : stored;
}

constexpr bool connSaver(bool active, bool stored) {
  return active ? kConnSaver : stored;
}

constexpr bool reconBackoff(bool active, bool stored) {
  return active ? kReconBackoff : stored;
}

constexpr uint8_t scanMode(bool active, uint8_t stored) {
  return active ? kScanMode : stored;
}

constexpr uint8_t inactivity(bool active, uint8_t stored) {
  return active ? kInactivity : stored;
}

constexpr uint8_t displayOff(bool active, uint8_t stored) {
  return active ? kDisplayOff : stored;
}

}  // namespace BatterySaver
}  // namespace Furble

#endif
