#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

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

OTA::SessionId id(uint8_t seed) {
  OTA::SessionId value {};
  for (size_t i = 0; i < value.size(); i++) {
    value[i] = static_cast<uint8_t>(seed + i);
  }
  return value;
}

OTA::Manifest manifest(const OTA::SessionId &session, uint32_t imageSize = 10) {
  OTA::Manifest value;
  value.sessionId = session;
  value.keyId[0] = 0x7a;
  value.imageSize = imageSize;
  value.partitionSize = imageSize + 1024;
  value.version = {'d', 'e', 'v', '-', '1'};
  value.rollbackCounter = 17;
  value.digest[0] = 0x42;
  value.signatureAlgorithm = OTA::SignatureAlgorithm::Ed25519;
  value.signature.assign(OTA::SIGNATURE_BYTES, 0xa5);
  return value;
}

OTA::Message begin(const OTA::Manifest &value) {
  OTA::Message message;
  message.kind = OTA::Kind::Begin;
  message.sessionId = value.sessionId;
  message.sequence = 1;
  message.manifest = value;
  return message;
}

OTA::Message chunk(const OTA::SessionId &session,
                   uint32_t offset,
                   std::initializer_list<uint8_t> bytes,
                   uint32_t sequence) {
  OTA::Message message;
  message.kind = OTA::Kind::Chunk;
  message.sessionId = session;
  message.sequence = sequence;
  message.chunk.sessionId = session;
  message.chunk.offset = offset;
  message.chunk.data = bytes;
  message.chunk.checksum = OTA::crc32(message.chunk.data.data(), message.chunk.data.size());
  return message;
}

OTA::Message control(OTA::Kind kind, const OTA::SessionId &session, uint32_t sequence) {
  OTA::Message message;
  message.kind = kind;
  message.sessionId = session;
  message.sequence = sequence;
  return message;
}

class FakeSink final: public OTA::Sink {
 public:
  bool beginResult = true;
  bool writeResult = true;
  bool matchesResult = true;
  bool finalizeResult = true;
  size_t beginCalls = 0;
  size_t writeCalls = 0;
  size_t finalizeCalls = 0;
  size_t abortCalls = 0;
  size_t matchesCalls = 0;
  OTA::Manifest lastManifest;
  std::vector<uint8_t> image;

  bool begin(const OTA::Manifest &value) override {
    beginCalls++;
    lastManifest = value;
    image.assign(value.imageSize, 0);
    return beginResult;
  }

  bool write(uint32_t offset, const uint8_t *data, size_t length) override {
    writeCalls++;
    if (!writeResult) {
      return false;
    }
    if ((offset > image.size()) || (length > (image.size() - offset))) {
      return false;
    }
    std::copy(data, data + length, image.begin() + offset);
    return true;
  }

  bool matches(uint32_t offset, const uint8_t *data, size_t length, uint32_t checksum) override {
    matchesCalls++;
    if (!matchesResult) {
      return false;
    }
    if ((offset > image.size()) || (length > (image.size() - offset))) {
      return false;
    }
    return (OTA::crc32(data, length) == checksum)
           && std::equal(data, data + length, image.begin() + offset);
  }

  bool finalize(const OTA::Manifest &) override {
    finalizeCalls++;
    return finalizeResult;
  }

  void abort() override { abortCalls++; }
};

OTA::Message roundTrip(const OTA::Message &message) {
  std::vector<uint8_t> wire;
  OTA::ErrorInfo error;
  expect(OTA::encode(message, wire, &error), "message encodes");
  OTA::Message decoded;
  expect(OTA::decode(wire.data(), wire.size(), decoded, &error), "message decodes");
  expect(decoded.kind == message.kind, "round trip preserves kind");
  expect(decoded.sessionId == message.sessionId, "round trip preserves session id");
  expect(decoded.sequence == message.sequence, "round trip preserves sequence");
  return decoded;
}

void testCodecAndMalformedInputs() {
  std::cout << "test: OTA MQTT envelope round trips and rejects malformed input\n";
  const OTA::SessionId session = id(1);
  const OTA::Manifest value = manifest(session);
  const OTA::Message decodedBegin = roundTrip(begin(value));
  expect(decodedBegin.manifest == value, "manifest round trip preserves signed metadata");

  const OTA::Message decodedChunk = roundTrip(chunk(session, 4, {4, 5, 6}, 9));
  expect(decodedChunk.chunk.data == std::vector<uint8_t>({4, 5, 6}),
         "chunk round trip preserves bytes");

  std::vector<uint8_t> wire;
  OTA::ErrorInfo error;
  OTA::Message message = begin(value);
  expect(OTA::encode(message, wire, &error), "begin encodes before corruption");
  OTA::Message unchanged = decodedBegin;
  // Signature bytes are opaque here. Corrupt the declared signature length,
  // which must fail structurally before a crypto verifier is reached.
  wire[29 + 54] = 63;
  expect(!OTA::decode(wire.data(), wire.size(), unchanged, &error),
         "signature length corruption is rejected");
  expect(unchanged.manifest == value, "failed decode leaves output unchanged");
  expect(error.code == OTA::Error::Malformed, "corrupt metadata reports a protocol error");

  wire.clear();
  expect(OTA::encode(chunk(session, 0, {1, 2, 3}, 2), wire, &error),
         "chunk encodes before checksum corruption");
  wire.back() ^= 0xff;
  expect(!OTA::decode(wire.data(), wire.size(), unchanged, &error),
         "chunk checksum corruption is rejected");
  expect(error.code == OTA::Error::ChunkChecksum, "checksum error is explicit");

  expect(!OTA::decode(nullptr, 0, unchanged, &error), "null payload is rejected");
  expect(error.code == OTA::Error::NullInput, "null payload reports null input");
  OTA::Message unsupported = message;
  unsupported.kind = static_cast<OTA::Kind>(99);
  expect(!OTA::encode(unsupported, wire, &error), "unknown kind is rejected on encode");

  OTA::Manifest invalid = value;
  invalid.digest.fill(0);
  expect(!OTA::encode(begin(invalid), wire, &error), "zero digest is rejected");
  expect(error.code == OTA::Error::InvalidDigest, "zero digest reports invalid digest");
  invalid = value;
  invalid.partitionSize = invalid.imageSize - 1;
  expect(!OTA::encode(begin(invalid), wire, &error), "undersized partition is rejected");
  expect(error.code == OTA::Error::InvalidPartitionSize,
         "undersized partition reports invalid partition");
  invalid = value;
  invalid.signatureAlgorithm = static_cast<OTA::SignatureAlgorithm>(99);
  expect(!OTA::encode(begin(invalid), wire, &error), "unknown signature is rejected");
  expect(error.code == OTA::Error::UnsupportedSignature,
         "unknown signature reports unsupported signature");
}

void testDeliveryPolicyAndChunkLedger() {
  std::cout << "test: QoS, retained replay, ordering, duplicates, and bounds\n";
  FakeSink sink;
  OTA::Session session(sink);
  const OTA::SessionId sessionId = id(20);
  const OTA::Manifest value = manifest(sessionId);
  const OTA::MessageMeta good {1, false, false};

  expect(session.onMessage({0, false, false}, begin(value)).error == OTA::Error::InvalidQoS,
         "QoS 0 is rejected");
  expect(session.onMessage({3, false, false}, begin(value)).error == OTA::Error::InvalidQoS,
         "QoS above 2 is rejected");
  expect(session.onMessage({1, true, false}, begin(value)).error == OTA::Error::RetainedMessage,
         "retained begin is rejected");
  expect(session.onMessage(good, begin(value)).accepted, "signed begin is accepted");
  expect(sink.beginCalls == 1, "begin is handed to the sink exactly once");

  const OTA::Message duplicateBegin = begin(value);
  expect(session.onMessage({1, false, true}, duplicateBegin).duplicate,
         "duplicate QoS 1 begin is idempotent");
  expect(sink.beginCalls == 1, "duplicate begin does not restart sink");

  const OTA::Message second = chunk(sessionId, 5, {5, 6, 7, 8, 9}, 3);
  const OTA::Message first = chunk(sessionId, 0, {0, 1, 2, 3, 4}, 2);
  expect(session.onMessage(good, second).accepted, "out of order second chunk is accepted");
  expect(session.onMessage(good, first).accepted, "first chunk completes the range");
  expect(session.receivedBytes() == 10, "received byte total is exact");
  expect(session.onMessage({1, false, true}, first).duplicate,
         "exact duplicate chunk is idempotent");
  expect(sink.writeCalls == 2, "duplicate chunk is not written twice");
  expect(sink.matchesCalls == 1, "duplicate bytes are checked by the sink");

  const OTA::Message overlap = chunk(sessionId, 4, {4, 5}, 4);
  expect(session.onMessage(good, overlap).error == OTA::Error::ChunkOverlap,
         "partial overlapping chunk is rejected");
  expect(sink.writeCalls == 2, "overlap does not reach sink");

  const OTA::SessionId foreignId = id(30);
  expect(session.onMessage(good, chunk(foreignId, 0, {1}, 4)).error == OTA::Error::WrongSession,
         "a chunk from another session is rejected");

  const OTA::Message commit = control(OTA::Kind::Commit, sessionId, 5);
  expect(session.onMessage(good, commit).accepted, "complete image commits");
  expect(session.state() == OTA::State::Done, "commit reaches done");
  expect(sink.finalizeCalls == 1, "commit hands digest and signature metadata to sink");
  expect(session.onMessage({1, false, true}, commit).duplicate, "duplicate commit is idempotent");
  expect(session.onMessage(good, begin(value)).error == OTA::Error::Replay,
         "terminal session cannot be replayed");

  const OTA::SessionId secondTerminal = id(61);
  OTA::Manifest secondManifest = manifest(secondTerminal, 1);
  expect(session.onMessage(good, begin(secondManifest)).accepted,
         "second session can begin after the first terminal session");
  expect(session.onMessage(good, control(OTA::Kind::Abort, secondTerminal, 2)).error
             == OTA::Error::Aborted,
         "second session aborts");
  expect(session.onMessage(good, begin(value)).error == OTA::Error::Replay,
         "older terminal session remains replay protected");

  const OTA::SessionId newId = id(60);
  OTA::Manifest newManifest = manifest(newId, 4);
  expect(session.onMessage(good, begin(newManifest)).accepted,
         "new session id starts after terminal state");
  const OTA::Message outOfBounds = chunk(newId, 3, {1, 2}, 2);
  expect(session.onMessage(good, outOfBounds).error == OTA::Error::ChunkOutOfBounds,
         "chunk beyond image size is rejected");
  expect(session.onMessage(good, control(OTA::Kind::Abort, newId, 3)).error == OTA::Error::Aborted,
         "abort is accepted and reported explicitly");
  expect(session.onMessage({1, false, true}, control(OTA::Kind::Abort, newId, 3)).duplicate,
         "duplicate abort is idempotent");
}

void testSinkAndVerificationFailures() {
  std::cout << "test: sink failures abort safely and never imply unsigned OTA\n";
  const OTA::SessionId sessionId = id(90);
  OTA::Manifest value = manifest(sessionId);
  FakeSink sink;
  sink.finalizeResult = false;
  OTA::Session session(sink);
  const OTA::MessageMeta good {2, false, false};
  expect(session.onMessage(good, begin(value)).accepted, "verification case begins");
  expect(session.onMessage(good, chunk(sessionId, 0, {0, 1, 2, 3, 4}, 2)).accepted,
         "verification case accepts first chunk");
  expect(session.onMessage(good, chunk(sessionId, 5, {5, 6, 7, 8, 9}, 3)).accepted,
         "verification case accepts second chunk");
  expect(session.onMessage(good, control(OTA::Kind::Commit, sessionId, 4)).error
             == OTA::Error::VerificationFailed,
         "failed digest or signature verification is explicit");
  expect(session.state() == OTA::State::Error, "verification failure is terminal");
  expect(sink.abortCalls == 1, "verification failure aborts sink");

  FakeSink rejected;
  rejected.beginResult = false;
  OTA::Session rejectedSession(rejected);
  expect(
      rejectedSession.onMessage(good, begin(manifest(id(100)))).error == OTA::Error::SinkRejected,
      "sink can reject a manifest before writes");
  expect(rejectedSession.state() == OTA::State::Error, "begin failure is terminal");
  expect(rejected.abortCalls == 1, "begin failure aborts a partially initialized sink");
  expect(rejectedSession.onMessage(good, begin(manifest(id(100)))).error == OTA::Error::Replay,
         "failed begin cannot be retried with the same session id");

  OTA::Manifest unsignedManifest = manifest(id(110));
  unsignedManifest.signature.clear();
  OTA::Message unsignedBegin = begin(unsignedManifest);
  OTA::ErrorInfo error;
  std::vector<uint8_t> wire;
  expect(!OTA::encode(unsignedBegin, wire, &error), "unsigned manifest cannot be encoded");
  expect(error.code == OTA::Error::InvalidSignature, "unsigned manifest reports invalid signature");
}

void testSequenceAndBoundedResourceRules() {
  std::cout << "test: sequence uniqueness, sink-backed retry, and bounded ledger\n";
  const OTA::MessageMeta good {1, false, false};
  const OTA::SessionId sessionId = id(150);
  FakeSink sink;
  OTA::Session session(sink);
  OTA::Manifest value = manifest(sessionId, 3);
  OTA::Message zeroBegin = begin(value);
  zeroBegin.sequence = 0;
  expect(session.onMessage(good, zeroBegin).error == OTA::Error::InvalidSequence,
         "zero begin sequence is rejected");
  expect(session.onMessage(good, begin(value)).accepted, "sequence case begins");
  expect(session.onMessage(good, chunk(sessionId, 0, {1}, 2)).accepted,
         "first sequence is accepted");
  expect(session.onMessage(good, chunk(sessionId, 1, {2}, 2)).error == OTA::Error::Replay,
         "a sequence cannot be reused for another range");
  expect(session.onMessage(good, chunk(sessionId, 0, {1}, 3)).error == OTA::Error::ChunkOverlap,
         "same range with another sequence is not an idempotent retry");
  expect(
      session.onMessage(good, control(OTA::Kind::Commit, sessionId, 2)).error == OTA::Error::Replay,
      "a chunk sequence cannot be reused by commit");
  expect(
      session.onMessage(good, control(OTA::Kind::Abort, sessionId, 4)).error == OTA::Error::Aborted,
      "sequence case aborts");

  FakeSink noReadback;
  noReadback.matchesResult = false;
  OTA::Session noReadbackSession(noReadback);
  const OTA::SessionId noReadbackId = id(151);
  expect(noReadbackSession.onMessage(good, begin(manifest(noReadbackId, 1))).accepted,
         "readback case begins");
  expect(noReadbackSession.onMessage(good, chunk(noReadbackId, 0, {9}, 2)).accepted,
         "readback case writes");
  expect(noReadbackSession.onMessage({1, false, true}, chunk(noReadbackId, 0, {9}, 2)).error
             == OTA::Error::ChunkOverlap,
         "duplicate fails closed without sink readback");

  FakeSink bounded;
  OTA::Session boundedSession(bounded);
  const OTA::SessionId boundedId = id(152);
  expect(boundedSession.onMessage(good, begin(manifest(boundedId, OTA::MAX_CHUNKS + 1))).accepted,
         "bounded ledger case begins");
  for (size_t offset = 0; offset < OTA::MAX_CHUNKS; offset++) {
    expect(boundedSession
               .onMessage(good,
                          chunk(boundedId, static_cast<uint32_t>(offset),
                                {static_cast<uint8_t>(offset)}, static_cast<uint32_t>(offset + 2)))
               .accepted,
           "bounded ledger accepts a range within its cap");
  }
  expect(boundedSession.onMessage(good, chunk(boundedId, OTA::MAX_CHUNKS, {0}, OTA::MAX_CHUNKS + 2))
                 .error
             == OTA::Error::ChunkLedgerFull,
         "bounded ledger rejects the range beyond its cap");
}

void testTerminalReplayWindow() {
  std::cout << "test: bounded multi-session terminal replay window\n";
  FakeSink sink;
  OTA::Session session(sink);
  const OTA::MessageMeta good {2, false, false};
  std::array<OTA::SessionId, OTA::MAX_TERMINAL_SESSIONS + 1> ids {};
  for (size_t index = 0; index < ids.size(); index++) {
    ids[index] = id(static_cast<uint8_t>(170 + index));
    expect(session.onMessage(good, begin(manifest(ids[index], 1))).accepted,
           "terminal window session begins");
    expect(session.onMessage(good, control(OTA::Kind::Abort, ids[index], 2)).error
               == OTA::Error::Aborted,
           "terminal window session closes");
  }
  for (size_t index = 1; index < ids.size(); index++) {
    expect(session.onMessage(good, begin(manifest(ids[index], 1))).error == OTA::Error::Replay,
           "recent terminal session is replay protected");
  }
  expect(session.onMessage(good, begin(manifest(ids[0], 1))).accepted,
         "bounded window explicitly permits an evicted oldest session");
}

void testCanonicalSignatureBytes() {
  std::cout << "test: canonical signature bytes and nested session binding\n";
  const OTA::Manifest value = manifest(id(130));
  std::vector<uint8_t> first;
  std::vector<uint8_t> second;
  OTA::ErrorInfo error;
  expect(OTA::canonicalSignedBytes(value, first, &error), "canonical bytes are available");
  OTA::Manifest changed = value;
  changed.rollbackCounter++;
  expect(OTA::canonicalSignedBytes(changed, second, &error),
         "changed manifest has canonical bytes");
  expect(first != second, "rollback counter changes signed bytes");

  FakeSink sink;
  OTA::Session session(sink);
  const OTA::MessageMeta good {1, false, false};
  expect(session.onMessage(good, begin(value)).accepted, "canonical case begins");
  OTA::Message nested = chunk(value.sessionId, 0, {1}, 2);
  nested.chunk.sessionId = id(131);
  expect(session.onMessage(good, nested).error == OTA::Error::WrongSession,
         "nested chunk session cannot differ from envelope session");
}

}  // namespace

int main() {
  testCodecAndMalformedInputs();
  testDeliveryPolicyAndChunkLedger();
  testSinkAndVerificationFailures();
  testCanonicalSignatureBytes();
  testSequenceAndBoundedResourceRules();
  testTerminalReplayWindow();
  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "ota_mqtt_protocol_test: all checks passed\n";
  return 0;
}
