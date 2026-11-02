#include <cstdint>
#include <iostream>
#include <vector>

#include "FurbleOTAAdmission.h"
#include "FurbleOTAMQTT.h"

namespace OTA = Furble::OTA::MQTT;

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

class FakeSink final: public OTA::Sink {
 public:
  bool authenticate(const OTA::Manifest &) override {
    authenticateCalls++;
    return true;
  }
  bool begin(const OTA::Manifest &) override {
    beginCalls++;
    return true;
  }
  bool write(uint32_t, const uint8_t *, size_t) override { return true; }
  bool matches(uint32_t, const uint8_t *, size_t, uint32_t) override { return true; }
  bool finalize(const OTA::Manifest &) override { return true; }
  bool activate() override { return true; }
  void abort() override { abortCalls++; }

  int authenticateCalls = 0;
  int beginCalls = 0;
  int abortCalls = 0;
};

class FakeStore final: public OTA::ReplayStore {
 public:
  bool loadFloor(uint32_t &floor) override {
    floor = installedFloor;
    return true;
  }
  bool reserveFloor(uint32_t, uint32_t next, const OTA::SessionId &) override {
    installedFloor = next;
    return true;
  }
  bool markStaged(const OTA::SessionId &, uint32_t) override { return true; }
  bool completeReservation(const OTA::SessionId &, uint32_t) override { return true; }
  bool abandonReservation(const OTA::SessionId &, uint32_t) override { return true; }
  bool recoverAbandonedReservation() override { return true; }

  uint32_t installedFloor = 0;
};

class FakeGate final: public OTA::Gate {
 public:
  bool allowUpdate() override { return allowed; }
  bool allowed = false;
};

OTA::SessionId id(uint8_t seed) {
  OTA::SessionId value {};
  for (size_t i = 0; i < value.size(); i++) {
    value[i] = static_cast<uint8_t>(seed + i);
  }
  return value;
}

OTA::Manifest manifest(const OTA::SessionId &session) {
  OTA::Manifest value;
  value.sessionId = session;
  value.keyId[0] = 0x7a;
  value.imageSize = 10;
  value.partitionSize = 1034;
  value.rollbackCounter = session[0];
  value.digest[0] = 0x42;
  value.signatureAlgorithm = OTA::SignatureAlgorithm::Ed25519;
  value.signature.assign(OTA::SIGNATURE_BYTES, 0xa5);
  value.version = {'d', 'e', 'v', '-', '1'};
  return value;
}

OTA::Message beginMessage(const OTA::Manifest &value) {
  OTA::Message message;
  message.kind = OTA::Kind::Begin;
  message.sessionId = value.sessionId;
  message.sequence = 1;
  message.manifest = value;
  return message;
}

constexpr OTA::MessageMeta kQos1 {1, false, false};

}  // namespace

int main() {
  // The policy: an idle device with a known battery at or above the floor, or
  // an unknown battery, may update. Anything else is a hard refusal.
  expect(OTA::MIN_UPDATE_BATTERY_PERCENT == 30, "the update battery floor is 30 percent");
  expect(!OTA::updateAdmitted(false, true, 100), "a busy device is refused at full battery");
  expect(!OTA::updateAdmitted(false, false, 0), "a busy device is refused with no gauge");
  expect(OTA::updateAdmitted(true, true, 30), "an idle device at the floor is admitted");
  expect(OTA::updateAdmitted(true, true, 100), "an idle device at full battery is admitted");
  expect(!OTA::updateAdmitted(true, true, 29), "an idle device below the floor is refused");
  expect(OTA::updateAdmitted(true, false, 0), "an idle device without a gauge is admitted");

  const auto session = id(0x10);
  const auto update = manifest(session);

  // A closed gate refuses a BEGIN before the sink or the replay store is
  // touched: no slot preparation, no rollback reservation, no partial state.
  {
    FakeSink sink;
    FakeStore store;
    OTA::RecoveryCoordinator recovery(store);
    expect(recovery.recoverAtBoot(), "the recovery gate opens");
    FakeGate gate;
    OTA::Session sessionEngine(sink, store, recovery, &gate);

    const OTA::Outcome refused = sessionEngine.onMessage(kQos1, beginMessage(update));
    expect(!refused.accepted, "a closed gate refuses the BEGIN");
    expect(refused.error == OTA::Error::Busy, "the refusal reports Busy");
    expect(sink.authenticateCalls == 0, "a refused BEGIN does not authenticate");
    expect(sink.beginCalls == 0, "a refused BEGIN does not prepare a slot");
    expect(store.installedFloor == 0, "a refused BEGIN does not reserve the floor");
    expect(sessionEngine.state() == OTA::State::Idle, "a refused BEGIN leaves the session idle");

    // The same message is accepted once the gate opens.
    gate.allowed = true;
    const OTA::Outcome accepted = sessionEngine.onMessage(kQos1, beginMessage(update));
    expect(accepted.accepted, "an open gate accepts the BEGIN");
    expect(sink.authenticateCalls == 1, "the admitted BEGIN authenticates once");
    expect(sink.beginCalls == 1, "the admitted BEGIN prepares one slot");
  }

  // A gate that closes mid-transfer must not fail a retransmitted BEGIN: the
  // duplicate is answered before admission, so a camera connecting during an
  // update cannot turn a retry into a terminal error.
  {
    FakeSink sink;
    FakeStore store;
    OTA::RecoveryCoordinator recovery(store);
    expect(recovery.recoverAtBoot(), "the second recovery gate opens");
    FakeGate gate;
    gate.allowed = true;
    OTA::Session sessionEngine(sink, store, recovery, &gate);
    expect(sessionEngine.onMessage(kQos1, beginMessage(update)).accepted, "the BEGIN is accepted");

    gate.allowed = false;
    const OTA::Outcome duplicate = sessionEngine.onMessage(kQos1, beginMessage(update));
    expect(duplicate.accepted, "a duplicate BEGIN survives a closed gate");
    expect(duplicate.duplicate, "a duplicate BEGIN is reported as a duplicate");
    expect(sink.beginCalls == 1, "the duplicate does not prepare a second slot");
  }

  // No gate keeps the historical behaviour for transports that have not
  // adopted admission yet.
  {
    FakeSink sink;
    FakeStore store;
    OTA::RecoveryCoordinator recovery(store);
    expect(recovery.recoverAtBoot(), "the third recovery gate opens");
    OTA::Session sessionEngine(sink, store, recovery);
    expect(sessionEngine.onMessage(kQos1, beginMessage(update)).accepted,
           "a session without a gate still accepts");
  }

  if (failures != 0) {
    return 1;
  }
  std::cout << "OTA MQTT admission: PASS\n";
  return 0;
}
