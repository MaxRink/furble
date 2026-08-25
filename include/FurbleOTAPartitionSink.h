#ifndef FURBLE_OTA_PARTITION_SINK_H
#define FURBLE_OTA_PARTITION_SINK_H

#include <cstddef>
#include <cstdint>

#include "FurbleOTAMQTT.h"

namespace Furble {
namespace OTA {

/**
 * Platform-neutral staging target for one inactive firmware partition.
 *
 * The target owns flash or block-device details. write() must return the
 * number of bytes it accepted, and end() must leave the staged image
 * unselected. activate() is the only operation that may change the boot
 * partition. A target must make abort() safe after every failed operation and
 * across a reboot with no successful activate().
 */
class PartitionTarget {
 public:
  virtual ~PartitionTarget() = default;
  virtual bool begin(uint32_t imageSize, uint32_t partitionSize) = 0;
  virtual size_t write(uint32_t offset, const uint8_t *data, size_t length) = 0;
  virtual bool matches(uint32_t offset, const uint8_t *data, size_t length) = 0;
  virtual bool end() = 0;
  virtual bool activate() = 0;
  virtual void abort() = 0;
};

/** Signature verifier supplied by the platform trust store. */
class ManifestVerifier {
 public:
  virtual ~ManifestVerifier() = default;
  /** Authenticate signed manifest metadata before any sink mutation. */
  virtual bool authenticate(const MQTT::Manifest &manifest) = 0;
  virtual bool verify(const MQTT::Manifest &manifest) = 0;
};

/**
 * Ordered, digest-checking implementation of MQTT::Sink.
 *
 * MQTT::Session may accept out-of-order chunks for generic sinks. This
 * partition sink deliberately requires contiguous writes because OTA flash
 * APIs are streams. Same-range retries are checked by the target without
 * copying the complete image into RAM. Signature verification remains an
 * injected trust-store operation; the image digest is always checked here
 * before end() or activate().
 */
class PartitionSink final: public MQTT::Sink {
 public:
  PartitionSink(PartitionTarget &target, ManifestVerifier &verifier);

  bool authenticate(const MQTT::Manifest &manifest) override;
  bool begin(const MQTT::Manifest &manifest) override;
  bool write(uint32_t offset, const uint8_t *data, size_t length) override;
  bool matches(uint32_t offset, const uint8_t *data, size_t length, uint32_t checksum) override;
  bool finalize(const MQTT::Manifest &manifest) override;
  bool activate() override;
  void abort() override;

 private:
  struct Sha256State {
    uint32_t words[8] = {};
    uint64_t bitLength = 0;
    uint8_t buffer[64] = {};
    size_t bufferLength = 0;
  };

  bool validManifest(const MQTT::Manifest &manifest) const;
  void resetState();
  static void hashInit(Sha256State &state);
  static void hashUpdate(Sha256State &state, const uint8_t *data, size_t length);
  static MQTT::Digest hashFinal(const Sha256State &state);

  PartitionTarget &m_Target;
  ManifestVerifier &m_Verifier;
  Sha256State m_Hash;
  uint32_t m_ImageSize = 0;
  uint32_t m_PartitionSize = 0;
  uint32_t m_NextOffset = 0;
  bool m_Started = false;
  bool m_Finalized = false;
  bool m_Activated = false;
  bool m_Authenticated = false;
  MQTT::Manifest m_AuthenticatedManifest {};
};

}  // namespace OTA
}  // namespace Furble

#endif
