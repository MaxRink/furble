#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "VirtualBleRuntime.h"
#include "nimble_boundary.h"

namespace {

using Furble::Sim::Ble::Address;
using Furble::Sim::Ble::AddressType;
using Furble::Sim::Ble::AttStatus;
using Furble::Sim::Ble::Characteristic;
using Furble::Sim::Ble::FaultOverlay;
using Furble::Sim::Ble::Operation;
using Furble::Sim::Ble::PeerProfile;
using Furble::Sim::Ble::Permission;
using Furble::Sim::Ble::PhysicalOutcome;
using Furble::Sim::Ble::PROPERTY_INDICATE;
using Furble::Sim::Ble::PROPERTY_NOTIFY;
using Furble::Sim::Ble::PROPERTY_READ;
using Furble::Sim::Ble::PROPERTY_WRITE;
using Furble::Sim::Ble::PROPERTY_WRITE_NO_RESPONSE;
using Furble::Sim::Ble::Provenance;
using Furble::Sim::Ble::Result;
using Furble::Sim::Ble::Service;
using Furble::Sim::Ble::SyntheticConformancePeer;
using Furble::Sim::Ble::VirtualBleRuntime;

std::atomic<size_t> callbackCount {0};

void countEvent(ble_npl_event *) {
  ++callbackCount;
}

void countCallout(ble_npl_callout *) {
  ++callbackCount;
}

Address address(AddressType type, uint8_t last) {
  Address value;
  value.type = type;
  value.bytes = {0x01, 0x02, 0x03, 0x04, 0x05, last};
  return value;
}

PeerProfile profile() {
  PeerProfile value;
  value.advertisement.identity.current = address(AddressType::RANDOM_PRIVATE_RESOLVABLE, 0x06);
  value.advertisement.identity.identity = address(AddressType::RANDOM_STATIC, 0x07);
  value.advertisement.identity.hasIdentity = true;
  value.advertisement.name = "Synthetic exact peer";
  value.advertisement.extended = true;
  value.advertisement.manufacturerData.assign(1600, 0xa5);
  value.preferredMtu = 100;
  Service service;
  service.uuid = "00001800-0000-1000-8000-00805f9b34fb";
  service.startHandle = 1;
  service.endHandle = 20;
  service.characteristics = {
      {"00002a00-0000-1000-8000-00805f9b34fb",
       2, 3,
       PROPERTY_READ,                                                             Permission::PERMISSION_READ,
       {'p', 'e', 'e', 'r'},
       {}                                                                                       },
      {"00002a01-0000-1000-8000-00805f9b34fb",
       4, 5,
       static_cast<uint8_t>(PROPERTY_WRITE | PROPERTY_WRITE_NO_RESPONSE),
       static_cast<uint8_t>(Permission::PERMISSION_READ | Permission::PERMISSION_WRITE),
       {},
       {}                                                                                       },
      {"00002a02-0000-1000-8000-00805f9b34fb",
       6, 7,
       static_cast<uint8_t>(PROPERTY_READ | PROPERTY_NOTIFY | PROPERTY_INDICATE),
       Permission::PERMISSION_READ,
       {},
       {{"00002902-0000-1000-8000-00805f9b34fb", 8, Permission::PERMISSION_WRITE, {0, 0}, true}}},
      {"00002a03-0000-1000-8000-00805f9b34fb",
       9, 10,
       PROPERTY_READ,                                                             Permission::PERMISSION_READ_ENCRYPTED,
       {'s', 'e', 'c', 'r', 'e', 't'},
       {}                                                                                       },
  };
  Characteristic longRead;
  longRead.uuid = "00002a04-0000-1000-8000-00805f9b34fb";
  longRead.declarationHandle = 11;
  longRead.valueHandle = 12;
  longRead.properties = PROPERTY_READ;
  longRead.permissions = Permission::PERMISSION_READ;
  longRead.value.assign(40, 0x42);
  service.characteristics.push_back(longRead);
  value.services.push_back(service);
  return value;
}

bool check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  bool ok = true;
  SyntheticConformancePeer peer(profile());
  peer.setPhysicalOutcome(5, PhysicalOutcome::SHUTTER);
  VirtualBleRuntime runtime;
  ok &= check(!runtime.certificationEligible(), "runtime cannot certify synthetic evidence");

  ok &= check(runtime.advertise(peer).ok(), "advertisement is observable");
  ok &= check(runtime.trace().back().payload.size() == 1600, "trace payload is not truncated");
  ok &= check(runtime.trace().back().provenance == Provenance::SYNTHETIC
                  && !runtime.trace().back().certificationEligible,
              "advertisement evidence is immutable synthetic provenance");

  const auto connected = runtime.connect(peer, peer.profile().advertisement.identity.current);
  ok &= check(connected.ok(), "connect accepts current RPA");
  ok &= check(connected.adapterId != 0 && connected.generation != 0,
              "adapter and generation assigned");
  ok &= check(connected.provenance == Provenance::SYNTHETIC && !connected.certificationEligible,
              "operation evidence is immutable synthetic provenance");
  ok &= check(runtime.liveLinks() == 1, "connected link is retained");
  ok &= check(runtime.discover(connected.adapterId).ok(), "discover succeeds");

  ok &= check(
      runtime.read(connected.adapterId, 3).value == std::vector<uint8_t>({'p', 'e', 'e', 'r'}),
      "read returns exact value");
  ok &= check(runtime.read(connected.adapterId, 99).attStatus == AttStatus::INVALID_HANDLE,
              "unknown handle is rejected");
  const auto longReadFirst = runtime.read(connected.adapterId, 12);
  const auto longReadSecond = runtime.read(connected.adapterId, 12, 22);
  ok &= check(longReadFirst.ok() && longReadFirst.value.size() == 22 && longReadSecond.ok()
                  && longReadSecond.value.size() == 18,
              "long reads use MTU-bounded offset chunks");
  ok &= check(runtime.write(connected.adapterId, 5, {1}, true).ok()
                  && runtime.trace().back().physicalOutcome == PhysicalOutcome::UNKNOWN,
              "ATT completion precedes physical outcome");
  runtime.advance(1);
  ok &= check(runtime.trace().back().operation == Operation::PHYSICAL_OUTCOME
                  && runtime.trace().back().physicalOutcome == PhysicalOutcome::SHUTTER,
              "generation-matched physical outcome is scheduled");
  ok &= check(peer.physicalOutcome(5) == PhysicalOutcome::SHUTTER,
              "peer callback receives physical outcome after ATT completion");
  ok &= check(runtime.writeLong(connected.adapterId, 5, std::vector<uint8_t>(20, 1), 0, false).ok()
                  && runtime.writeLong(connected.adapterId, 5, {2, 3, 4}, 20, true).ok(),
              "prepare writes enforce packet chunks and offsets");
  ok &= check(runtime.write(connected.adapterId, 5, {1}, false).ok(),
              "write without response uses its own property");
  ok &= check(
      runtime.write(connected.adapterId, 3, {1}, true).attStatus == AttStatus::WRITE_NOT_PERMITTED,
      "read-only value rejects writes");

  ok &= check(runtime.subscribe(connected.adapterId, 7, false).ok(), "notification subscription");
  ok &= check(runtime.notify(connected.adapterId, 7, {1, 2}).ok(), "notification delivery");
  ok &= check(runtime.subscribe(connected.adapterId, 7, true).ok(), "indication subscription");
  ok &= check(runtime.indicate(connected.adapterId, 7, {3}).ok(), "indication delivery");
  ok &= check(runtime.indicate(connected.adapterId, 7, {4}).attStatus
                  == AttStatus::PROCEDURE_ALREADY_IN_PROGRESS,
              "indication requires confirmation");
  ok &= check(runtime.confirm(connected.adapterId, 7).ok(), "indication confirmation");
  ok &= check(peer.confirmations() == 1, "peer sees one confirmation");
  ok &= check(runtime.indicate(connected.adapterId, 7, {5}).ok(), "second indication starts");
  runtime.advance(1000);
  ok &= check(runtime.trace().back().operation == Operation::CONFIRM
                  && runtime.trace().back().result == Result::TIMEOUT,
              "unconfirmed indication times out");

  ok &= check(runtime.read(connected.adapterId, 10).result == Result::SECURITY_REQUIRED,
              "encrypted read is denied before security");
  ok &= check(runtime.secure(connected.adapterId, true, 16).ok(), "security negotiates");
  ok &= check(runtime.read(connected.adapterId, 10).ok(), "encrypted read succeeds after security");
  ok &= check(runtime.bonds().size() == 1 && runtime.registrations().size() == 0,
              "bond and registration stores are separate");
  ok &= check(runtime.registerApplication(connected.adapterId, "furble", {9, 8, 7}).ok(),
              "application registration is independently persisted");
  ok &= check(runtime.bonds().size() == 1 && runtime.registrations().size() == 1,
              "registration does not mutate bond store");

  ok &= check(runtime.exchangeMtu(connected.adapterId, 200).ok(), "MTU exchange is queued");
  runtime.advance(1);
  ok &= check(runtime.link(connected.adapterId)->mtu() == 100, "MTU is bounded by peer preference");
  ok &= check(runtime.exchangeMtu(connected.adapterId, 22).attStatus
                  == AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH,
              "invalid MTU is rejected");
  ok &= check(runtime.updatePhy(connected.adapterId, Furble::Sim::Ble::Phy::LE_2M).ok(),
              "PHY update is observable");
  ok &= check(
      runtime.resolveRpa(connected.adapterId, address(AddressType::RANDOM_PRIVATE_RESOLVABLE, 0x09))
              .result
          == Result::NOT_FOUND,
      "wrong RPA is rejected");
  ok &= check(
      runtime.resolveRpa(connected.adapterId, peer.profile().advertisement.identity.current).ok(),
      "current RPA resolves");

  FaultOverlay fault;
  fault.operation = Operation::READ;
  fault.handle = 3;
  fault.generation = connected.generation;
  fault.unsupported = true;
  ok &= check(runtime.addFault(fault), "fault overlay is accepted");
  ok &= check(runtime.read(connected.adapterId, 3).result == Result::UNSUPPORTED,
              "unsupported operation remains explicit");
  ok &= check(runtime.read(connected.adapterId, 3).ok(), "one-shot fault is consumed");
  FaultOverlay peerFault;
  peerFault.layer = Furble::Sim::Ble::FaultLayer::PEER;
  peerFault.operation = Operation::READ;
  peerFault.handle = 3;
  peerFault.generation = connected.generation;
  peerFault.result = Result::FAULT;
  peerFault.status = AttStatus::UNLIKELY_ERROR;
  ok &= check(runtime.addFault(peerFault), "layer-specific peer fault is accepted");
  ok &= check(runtime.read(connected.adapterId, 3).result == Result::FAULT,
              "peer fault does not alias ATT fault");

  ok &= check(runtime.disconnect(connected.adapterId).ok(), "disconnect succeeds");
  ok &= check(runtime.liveLinks() == 0 && peer.leakFree(), "disconnect releases peer link");
  ok &= check(runtime.retiredIdentityCount() == 1, "retired identity is bounded and observable");
  ok &= check(runtime.disconnect(connected.adapterId).result == Result::LINK_DOWN,
              "second disconnect is not silently successful");
  const auto reconnected = runtime.connect(peer, peer.profile().advertisement.identity.current);
  ok &= check(reconnected.ok() && reconnected.adapterId != connected.adapterId
                  && reconnected.generation > connected.generation,
              "reconnect receives a fresh adapter and generation");
  ok &= check(runtime.link(reconnected.adapterId)->security().bonded
                  && runtime.link(reconnected.adapterId)->security().encrypted
                  && runtime.link(reconnected.adapterId)->security().applicationRegistered,
              "bonded security is restored per link");
  ok &= check(runtime.write(reconnected.adapterId, 5, {2}, true).ok(),
              "reconnect queues physical event");
  ok &= check(runtime.disconnect(reconnected.adapterId).ok(), "disconnect fences queued event");
  runtime.advance(1);
  bool stalePhysical = false;
  for (const auto &event : runtime.trace()) {
    stalePhysical =
        stalePhysical
        || (event.operation == Operation::PHYSICAL_OUTCOME && event.result == Result::INVALID_STATE
            && event.detail == "stale generation event discarded");
  }
  ok &= check(stalePhysical, "stale physical event is discarded");
  ok &= check(peer.leakFree(), "reconnect lifecycle is leak free");

  PeerProfile invalid = profile();
  invalid.services.front().characteristics.front().valueHandle = 2;
  SyntheticConformancePeer invalidPeer(invalid);
  ok &= check(runtime.connect(invalidPeer, invalid.advertisement.identity.current).result
                  == Result::INVALID_ARGUMENT,
              "duplicate declaration and value handles are rejected");

  furble_ble_npl_reset();
  callbackCount = 0;
  ble_npl_event first {countEvent, nullptr, 0, 0, 0};
  ble_npl_event second {countEvent, nullptr, 0, 0, 0};
  ok &= check(furble_ble_npl_eventq_put(&first, 20) == 0, "NPL event is queued");
  ok &= check(furble_ble_npl_eventq_put(&second, 10) == 0, "second NPL event is queued");
  furble_ble_npl_advance(10);
  ok &= check(furble_ble_npl_run_due() == 1 && callbackCount == 1, "NPL deadline dispatches once");
  ok &= check(furble_ble_npl_eventq_remove(&first) == 0, "NPL cancellation is explicit");
  ok &= check(furble_ble_npl_pending() == 0, "NPL cancellation leaves no events");

  furble_ble_npl_reset();
  callbackCount = 0;
  ble_npl_callout callout {countCallout, nullptr, 0, 0, 0};
  furble_ble_npl_advance(0xfffffffeULL);
  ok &= check(furble_ble_npl_callout_reset(&callout, 5) == 0, "wrap-safe callout is queued");
  furble_ble_npl_advance(4);
  ok &= check(furble_ble_npl_run_due() == 0, "callout respects wrap-safe deadline");
  furble_ble_npl_advance(1);
  ok &= check(furble_ble_npl_run_due() == 1 && callbackCount == 1,
              "callout dispatches across uint32 wrap");

  furble_ble_npl_reset();
  callbackCount = 0;
  std::array<ble_npl_event, 8> concurrentEvents {};
  std::array<std::thread, 8> producers;
  for (size_t index = 0; index < concurrentEvents.size(); ++index) {
    concurrentEvents[index].fn = countEvent;
    producers[index] =
        std::thread([&, index]() { furble_ble_npl_eventq_put(&concurrentEvents[index], 0); });
  }
  for (std::thread &producer : producers) {
    producer.join();
  }
  ok &= check(furble_ble_npl_pending() == concurrentEvents.size(),
              "NPL queue serializes concurrent producers");
  ok &= check(furble_ble_npl_run_due() == concurrentEvents.size()
                  && callbackCount == concurrentEvents.size(),
              "NPL FIFO drains all concurrent events");

  bool monotonicTrace = true;
  for (size_t index = 1; index < runtime.trace().size(); ++index) {
    monotonicTrace =
        monotonicTrace && runtime.trace()[index - 1].index < runtime.trace()[index].index;
  }
  ok &= check(monotonicTrace, "trace sequence remains monotonic");
  for (const auto &event : runtime.trace()) {
    ok &= check(event.provenance == Provenance::SYNTHETIC && !event.certificationEligible,
                "all trace records remain uncertifiable");
  }

  return ok ? 0 : 1;
}
