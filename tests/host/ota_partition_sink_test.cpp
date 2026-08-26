#include "FurbleOTAPartitionSink.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

namespace OTA = Furble::OTA::MQTT;
using Furble::OTA::ManifestVerifier;
using Furble::OTA::PartitionSink;
using Furble::OTA::PartitionTarget;

namespace {

class FakeTarget final: public PartitionTarget {
 public:
  uint32_t actualPartitionSize = 1024;
  bool failBegin = false;
  bool failWrite = false;
  bool shortWrite = false;
  bool failEnd = false;
  bool failActivate = false;
  size_t beginCalls = 0;
  size_t writeCalls = 0;
  size_t matchesCalls = 0;
  size_t endCalls = 0;
  size_t activateCalls = 0;
  size_t abortCalls = 0;
  bool bootSelected = false;
  std::vector<uint8_t> image;

  bool begin(uint32_t imageSize, uint32_t partitionSize) override {
    beginCalls++;
    if (failBegin || (partitionSize != actualPartitionSize) || (imageSize > partitionSize)) {
      return false;
    }
    image.assign(imageSize, 0xff);
    started = true;
    ended = false;
    return true;
  }

  size_t write(uint32_t offset, const uint8_t *data, size_t length) override {
    writeCalls++;
    if (!started || ended || failWrite || (offset > image.size())
        || (length > image.size() - offset)) {
      return 0;
    }
    const size_t accepted = shortWrite && length != 0 ? length - 1 : length;
    std::copy(data, data + accepted, image.begin() + offset);
    return accepted;
  }

  bool matches(uint32_t offset, const uint8_t *data, size_t length) override {
    matchesCalls++;
    return started && !ended && (offset <= image.size()) && (length <= image.size() - offset)
           && std::equal(data, data + length, image.begin() + offset);
  }

  bool end() override {
    endCalls++;
    if (!started || ended || failEnd) {
      return false;
    }
    ended = true;
    return true;
  }

  bool activate() override {
    activateCalls++;
    if (!started || !ended || failActivate) {
      return false;
    }
    bootSelected = true;
    started = false;
    return true;
  }

  void abort() override {
    abortCalls++;
    if (!bootSelected) {
      image.clear();
    }
    started = false;
    ended = false;
  }

 private:
  bool started = false;
  bool ended = false;
};

class FakeVerifier final: public ManifestVerifier {
 public:
  bool authenticateResult = true;
  bool result = true;
  size_t calls = 0;
  size_t authenticateCalls = 0;

  bool authenticate(const OTA::Manifest &) override {
    authenticateCalls++;
    return authenticateResult;
  }

  bool verify(const OTA::Manifest &) override {
    calls++;
    return result;
  }
};

OTA::Manifest manifest() {
  OTA::Manifest value;
  value.sessionId[0] = 1;
  value.keyId[0] = 2;
  value.version = {'d', 'e', 'v'};
  value.signature.assign(OTA::SIGNATURE_BYTES, 3);
  value.imageSize = 3;
  value.partitionSize = 1024;
  value.digest = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                  0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                  0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  return value;
}

void rejectsMalformedManifestBeforeTargetMutation() {
  const OTA::Manifest valid = manifest();
  const std::pair<const char *, OTA::Manifest> cases[] = {
      {"zero session",
       [] {
         OTA::Manifest value = manifest();
         value.sessionId = {};
         return value;
       }()},
      {"empty version",
       [] {
         OTA::Manifest value = manifest();
         value.version.clear();
         return value;
       }()},
      {"embedded nul version",
       [] {
         OTA::Manifest value = manifest();
         value.version = {'d', '\0', 'v'};
         return value;
       }()},
      {"zero key id",
       [] {
         OTA::Manifest value = manifest();
         value.keyId = {};
         return value;
       }()},
      {"invalid signature size",
       [] {
         OTA::Manifest value = manifest();
         value.signature.pop_back();
         return value;
       }()},
  };
  for (const auto &entry : cases) {
    FakeTarget target;
    FakeVerifier verifier;
    PartitionSink sink(target, verifier);
    assert(!sink.begin(entry.second));
    assert(target.beginCalls == 0);
    assert(verifier.authenticateCalls == 0);
  }

  FakeTarget target;
  FakeVerifier verifier;
  PartitionSink sink(target, verifier);
  assert(sink.begin(valid));
  sink.abort();
}

void writeImage(PartitionSink &sink) {
  const uint8_t first[] = {'a'};
  const uint8_t rest[] = {'b', 'c'};
  assert(sink.write(0, first, sizeof(first)));
  assert(sink.matches(0, first, sizeof(first), OTA::crc32(first, sizeof(first))));
  assert(sink.write(1, rest, sizeof(rest)));
}

void cleanUpdate() {
  FakeTarget target;
  FakeVerifier verifier;
  PartitionSink sink(target, verifier);
  const OTA::Manifest value = manifest();
  assert(sink.begin(value));
  writeImage(sink);
  assert(sink.finalize(value));
  assert(verifier.calls == 1);
  assert(sink.activate());
  assert(target.bootSelected);
  assert(target.activateCalls == 1);
  assert(sink.activate());
  assert(target.activateCalls == 1);
  sink.abort();
  assert(target.abortCalls == 0);
}

void rejectsInvalidRangesAndRetries() {
  FakeTarget target;
  FakeVerifier verifier;
  PartitionSink sink(target, verifier);
  const OTA::Manifest value = manifest();
  const uint8_t bytes[] = {'a'};
  assert(sink.begin(value));
  assert(!sink.write(1, bytes, sizeof(bytes)));
  assert(target.abortCalls == 1);
  assert(target.image.empty());

  assert(sink.begin(value));
  assert(!sink.write(0, bytes, 0));
  assert(target.abortCalls == 2);
  assert(sink.begin(value));
  assert(!sink.write(0, bytes, 4));
  assert(target.abortCalls == 3);
  assert(sink.begin(value));
  assert(sink.write(0, bytes, sizeof(bytes)));
  assert(!sink.matches(0, bytes, sizeof(bytes), 0));
  const uint8_t different[] = {'z'};
  assert(!sink.matches(0, different, sizeof(different), OTA::crc32(different, 1)));
  sink.abort();
}

void rejectsBeginAndWriteFaults() {
  const OTA::Manifest value = manifest();
  {
    FakeTarget target;
    target.failBegin = true;
    FakeVerifier verifier;
    PartitionSink sink(target, verifier);
    assert(!sink.begin(value));
    assert(target.beginCalls == 1);
    assert(target.abortCalls == 1);
  }
  {
    FakeTarget target;
    target.failWrite = true;
    FakeVerifier verifier;
    PartitionSink sink(target, verifier);
    assert(sink.begin(value));
    const uint8_t bytes[] = {'a'};
    assert(!sink.write(0, bytes, sizeof(bytes)));
    assert(target.abortCalls == 1);
  }
  {
    FakeTarget target;
    target.shortWrite = true;
    FakeVerifier verifier;
    PartitionSink sink(target, verifier);
    assert(sink.begin(value));
    const uint8_t bytes[] = {'a'};
    assert(!sink.write(0, bytes, sizeof(bytes)));
    assert(target.abortCalls == 1);
  }
}

void rejectsUnauthenticatedManifestBeforeTargetMutation() {
  FakeTarget target;
  FakeVerifier verifier;
  verifier.authenticateResult = false;
  PartitionSink sink(target, verifier);
  const OTA::Manifest value = manifest();
  assert(!sink.authenticate(value));
  assert(!sink.begin(value));
  assert(target.beginCalls == 0);
  assert(target.abortCalls == 0);
}

void rejectsDigestVerifierAndEndFaults() {
  const OTA::Manifest value = manifest();
  {
    FakeTarget target;
    FakeVerifier verifier;
    PartitionSink sink(target, verifier);
    assert(sink.begin(value));
    const uint8_t bytes[] = {'a', 'b'};
    assert(sink.write(0, bytes, sizeof(bytes)));
    OTA::Manifest truncated = value;
    truncated.imageSize = 3;
    assert(!sink.finalize(truncated));
    assert(target.abortCalls == 1);
  }
  {
    FakeTarget target;
    FakeVerifier verifier;
    PartitionSink sink(target, verifier);
    assert(sink.begin(value));
    writeImage(sink);
    OTA::Manifest wrongDigest = value;
    wrongDigest.digest[0] ^= 1;
    assert(!sink.finalize(wrongDigest));
    assert(verifier.calls == 0);
    assert(target.endCalls == 0);
    assert(target.abortCalls == 1);
  }
  {
    FakeTarget target;
    FakeVerifier verifier;
    verifier.result = false;
    PartitionSink sink(target, verifier);
    assert(sink.begin(value));
    writeImage(sink);
    assert(!sink.finalize(value));
    assert(target.endCalls == 0);
    assert(target.abortCalls == 1);
  }
  {
    FakeTarget target;
    target.failEnd = true;
    FakeVerifier verifier;
    PartitionSink sink(target, verifier);
    assert(sink.begin(value));
    writeImage(sink);
    assert(!sink.finalize(value));
    assert(target.endCalls == 1);
    assert(target.abortCalls == 1);
  }
}

void rejectsActivationAndRebootLeavesBootUnchanged() {
  const OTA::Manifest value = manifest();
  FakeTarget target;
  FakeVerifier verifier;
  PartitionSink interrupted(target, verifier);
  assert(interrupted.begin(value));
  const uint8_t partial[] = {'a'};
  assert(interrupted.write(0, partial, sizeof(partial)));
  PartitionSink rebootedPartial(target, verifier);
  assert(rebootedPartial.begin(value));
  assert(!target.bootSelected);
  rebootedPartial.abort();

  PartitionSink first(target, verifier);
  assert(first.begin(value));
  writeImage(first);
  assert(first.finalize(value));
  target.failActivate = true;
  assert(!first.activate());
  assert(!target.bootSelected);
  assert(target.abortCalls == 2);

  target.failActivate = false;
  PartitionSink rebooted(target, verifier);
  assert(rebooted.begin(value));
  writeImage(rebooted);
  assert(rebooted.finalize(value));
  assert(rebooted.activate());
  assert(target.bootSelected);
}

void rejectsCapacityAndTruncation() {
  FakeVerifier verifier;
  {
    FakeTarget target;
    target.actualPartitionSize = 1024;
    PartitionSink sink(target, verifier);
    OTA::Manifest value = manifest();
    value.partitionSize = 2048;
    assert(!sink.begin(value));
    assert(target.beginCalls == 1);
    assert(target.abortCalls == 1);
  }
  {
    FakeTarget target;
    PartitionSink sink(target, verifier);
    OTA::Manifest value = manifest();
    value.partitionSize = 2;
    assert(!sink.begin(value));
    assert(target.beginCalls == 0);
  }
  {
    FakeTarget target;
    PartitionSink sink(target, verifier);
    OTA::Manifest value = manifest();
    value.digest.fill(0);
    assert(!sink.begin(value));
    assert(target.beginCalls == 0);
  }
}

void hashesAcrossBlockBoundaries() {
  static constexpr char TEXT[] = "The quick brown fox jumps over the lazy dog";
  const std::array<uint8_t, OTA::DIGEST_BYTES> digest = {
      0xd7, 0xa8, 0xfb, 0xb3, 0x07, 0xd7, 0x80, 0x94, 0x69, 0xca, 0x9a,
      0xbc, 0xb0, 0x08, 0x2e, 0x4f, 0x8d, 0x56, 0x51, 0xe4, 0x6d, 0x3c,
      0xdb, 0x76, 0x2d, 0x02, 0xd0, 0xbf, 0x37, 0xc9, 0xe5, 0x92};
  FakeTarget target;
  FakeVerifier verifier;
  PartitionSink sink(target, verifier);
  OTA::Manifest value = manifest();
  value.imageSize = static_cast<uint32_t>(sizeof(TEXT) - 1);
  value.digest = digest;
  assert(sink.begin(value));
  assert(sink.write(0, reinterpret_cast<const uint8_t *>(TEXT), 17));
  assert(sink.write(17, reinterpret_cast<const uint8_t *>(TEXT) + 17, sizeof(TEXT) - 1 - 17));
  assert(sink.finalize(value));
  assert(sink.activate());
}

}  // namespace

int main() {
  cleanUpdate();
  rejectsMalformedManifestBeforeTargetMutation();
  rejectsInvalidRangesAndRetries();
  rejectsBeginAndWriteFaults();
  rejectsUnauthenticatedManifestBeforeTargetMutation();
  rejectsDigestVerifierAndEndFaults();
  rejectsActivationAndRebootLeavesBootUnchanged();
  rejectsCapacityAndTruncation();
  hashesAcrossBlockBoundaries();
  std::cout << "ota partition sink: ordered writes, digest, retries, faults, and reboot passed\n";
  return 0;
}
