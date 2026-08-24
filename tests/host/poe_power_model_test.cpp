#include <cstdlib>
#include <iostream>

#include "eth/PoEPowerModel.h"

namespace {

using FurbleHost::EthernetLink;
using FurbleHost::PoEPowerModel;
using FurbleHost::TriState;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void checkNoHatDefault(void) {
  const PoEPowerModel model;
  const auto &state = model.state();
  check(state.hatPresent == TriState::NO, "default fixture has no HAT");
  check(state.hatCapable == TriState::NO, "default fixture has no HAT capability");
  check(state.poeAvailable == TriState::NO, "default fixture has no PoE power");
  check(state.ethernetLink == EthernetLink::DOWN, "default fixture link is down");
  check(state.usbExternalPower == TriState::NO, "default fixture has no USB power");
}

void checkUsbWithoutHat(void) {
  PoEPowerModel model;
  model.setUsbExternalPower(TriState::YES);
  model.setEthernetLink(EthernetLink::UP);
  const auto &state = model.state();
  check(state.hatPresent == TriState::NO, "USB-only fixture still has no HAT");
  check(state.poeAvailable == TriState::NO, "USB power does not become PoE");
  check(state.ethernetLink == EthernetLink::UP, "USB-only fixture may have Ethernet link");
  check(state.usbExternalPower == TriState::YES, "USB power is explicit");
}

void checkHatWithoutNegotiatedPower(void) {
  PoEPowerModel model;
  model.setHat(TriState::YES, TriState::YES);
  model.setEthernetLink(EthernetLink::UP);
  const auto &state = model.state();
  check(state.hatPresent == TriState::YES, "HAT presence is explicit");
  check(state.hatCapable == TriState::YES, "HAT capability is explicit");
  check(state.poeAvailable == TriState::NO,
        "a capable HAT without negotiation has no PoE power");
  check(state.ethernetLink == EthernetLink::UP,
        "link state does not imply PoE availability");
}

void checkNegotiatedPoe(void) {
  PoEPowerModel model;
  model.setHat(TriState::YES, TriState::YES);
  model.setPoEAvailable(TriState::YES);
  model.setEthernetLink(EthernetLink::UP);
  const auto &state = model.state();
  check(state.hatPresent == TriState::YES && state.hatCapable == TriState::YES,
        "negotiated fixture retains HAT facts");
  check(state.poeAvailable == TriState::YES, "negotiated PoE is available");
  check(state.ethernetLink == EthernetLink::UP, "negotiated fixture has link");
}

void checkPowerLossAndRecovery(void) {
  PoEPowerModel model;
  model.setHat(TriState::YES, TriState::YES);
  model.setPoEAvailable(TriState::YES);
  model.setEthernetLink(EthernetLink::UP);

  model.setPoEAvailable(TriState::NO);
  model.setEthernetLink(EthernetLink::DOWN);
  check(model.state().hatPresent == TriState::YES,
        "power loss does not erase physical HAT presence");
  check(model.state().hatCapable == TriState::YES,
        "power loss does not erase HAT capability");
  check(model.state().poeAvailable == TriState::NO, "power loss is observable in fixture");
  check(model.state().ethernetLink == EthernetLink::DOWN, "power loss can take link down");

  model.setPoEAvailable(TriState::YES);
  model.setEthernetLink(EthernetLink::UP);
  check(model.state().poeAvailable == TriState::YES, "PoE recovers explicitly");
  check(model.state().ethernetLink == EthernetLink::UP, "Ethernet link recovers explicitly");
  check(model.state().hatPresent == TriState::YES && model.state().hatCapable == TriState::YES,
        "recovery retains HAT facts");
}

void checkUnknownIsNotInferred(void) {
  PoEPowerModel model;
  model.setHat(TriState::UNKNOWN, TriState::UNKNOWN);
  model.setPoEAvailable(TriState::UNKNOWN);
  model.setEthernetLink(EthernetLink::UP);
  model.setUsbExternalPower(TriState::UNKNOWN);
  check(model.state().poeAvailable == TriState::UNKNOWN,
        "unknown PoE remains unknown despite link-up");
  check(model.state().hatPresent == TriState::UNKNOWN,
        "unobservable HAT presence remains unknown");
  check(model.state().hatCapable == TriState::UNKNOWN,
        "unobservable HAT capability remains unknown");
}

}  // namespace

int main(void) {
  checkNoHatDefault();
  checkUsbWithoutHat();
  checkHatWithoutNegotiatedPower();
  checkNegotiatedPoe();
  checkPowerLossAndRecovery();
  checkUnknownIsNotInferred();
  std::cout << "PoE power model: PASS\n";
  return 0;
}
