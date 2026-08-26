#ifndef FURBLE_HOST_POE_POWER_MODEL_H
#define FURBLE_HOST_POE_POWER_MODEL_H

namespace FurbleHost {

/** An explicit observation used by the host/simulator power fixture. */
enum class TriState {
  UNKNOWN,
  NO,
  YES,
};

enum class EthernetLink {
  UNKNOWN,
  DOWN,
  UP,
};

/**
 * Test-only power/source model for the optional Waveshare PoE add-on.
 *
 * These fields are independent observations. In particular, Ethernet link
 * state never implies PoE availability, and no field claims to be sensed by
 * the ESP32-S3 firmware.
 */
struct PoEPowerState {
  TriState hatPresent = TriState::NO;
  TriState hatCapable = TriState::NO;
  TriState poeAvailable = TriState::NO;
  EthernetLink ethernetLink = EthernetLink::DOWN;
  TriState usbExternalPower = TriState::NO;
};

class PoEPowerModel final {
 public:
  PoEPowerModel() = default;

  const PoEPowerState &state() const { return m_State; }

  void setHat(TriState present, TriState capable);
  void setPoEAvailable(TriState available);
  void setEthernetLink(EthernetLink link);
  void setUsbExternalPower(TriState present);

 private:
  PoEPowerState m_State;
};

}  // namespace FurbleHost

#endif
