#include "PoEPowerModel.h"

namespace FurbleHost {

void PoEPowerModel::setHat(TriState present, TriState capable) {
  m_State.hatPresent = present;
  m_State.hatCapable = capable;
}

void PoEPowerModel::setPoEAvailable(TriState available) {
  // Deliberately do not derive this from HAT, USB, or Ethernet state. A
  // present/capable module still needs a PoE source and successful negotiation.
  m_State.poeAvailable = available;
}

void PoEPowerModel::setEthernetLink(EthernetLink link) {
  m_State.ethernetLink = link;
}

void PoEPowerModel::setUsbExternalPower(TriState present) {
  m_State.usbExternalPower = present;
}

}  // namespace FurbleHost
