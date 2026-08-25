#ifndef FURBLE_OTA_MQTT_H
#define FURBLE_OTA_MQTT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Furble {
namespace OTA {
namespace MQTT {

constexpr size_t SESSION_ID_BYTES = 16;
constexpr size_t DIGEST_BYTES = 32;
constexpr size_t SIGNATURE_BYTES = 64;
constexpr size_t MAX_VERSION_BYTES = 63;
constexpr size_t MAX_IMAGE_BYTES = 16 * 1024 * 1024;
constexpr size_t MAX_CHUNK_BYTES = 4096;
constexpr uint8_t MIN_QOS = 1;

using SessionId = std::array<uint8_t, SESSION_ID_BYTES>;
using Digest = std::array<uint8_t, DIGEST_BYTES>;

enum class Kind : uint8_t {
  Begin = 1,
  Chunk = 2,
  Commit = 3,
  Abort = 4,
};

enum class SignatureAlgorithm : uint8_t {
  EcdsaP256Sha256 = 1,
  Ed25519 = 2,
};

enum class State : uint8_t {
  Idle,
  Receiving,
  Verifying,
  Done,
  Aborted,
  Error,
};

enum class Error : uint8_t {
  None,
  NullInput,
  Truncated,
  Malformed,
  UnsupportedKind,
  UnsupportedSignature,
  InvalidManifest,
  InvalidVersion,
  InvalidImageSize,
  InvalidPartitionSize,
  InvalidSignature,
  InvalidDigest,
  InvalidQoS,
  RetainedMessage,
  Busy,
  Replay,
  WrongSession,
  InvalidChunk,
  ChunkTooLarge,
  ChunkOutOfBounds,
  ChunkChecksum,
  ChunkOverlap,
  SinkRejected,
  Incomplete,
  VerificationFailed,
  Aborted,
};

struct MessageMeta {
  uint8_t qos = 0;
  bool retained = false;
  bool duplicate = false;
};

struct Manifest {
  SessionId sessionId {};
  uint32_t imageSize = 0;
  uint32_t partitionSize = 0;
  Digest digest {};
  SignatureAlgorithm signatureAlgorithm = SignatureAlgorithm::EcdsaP256Sha256;
  std::vector<uint8_t> signature;
  std::vector<uint8_t> version;

  bool operator==(const Manifest &other) const;
};

struct Chunk {
  SessionId sessionId {};
  uint32_t offset = 0;
  std::vector<uint8_t> data;
  uint32_t checksum = 0;

  bool operator==(const Chunk &other) const;
};

struct Message {
  Kind kind = Kind::Begin;
  SessionId sessionId {};
  uint32_t sequence = 0;
  Manifest manifest {};
  Chunk chunk {};
};

struct ErrorInfo {
  Error code = Error::None;
  size_t offset = 0;
};

/** Decode one complete MQTT payload. Output is unchanged when decoding fails. */
bool decode(const uint8_t *data, size_t length, Message &out, ErrorInfo *error = nullptr);

/** Encode one complete MQTT payload. */
bool encode(const Message &message, std::vector<uint8_t> &out, ErrorInfo *error = nullptr);

/** Stable diagnostic label for a protocol error. */
const char *errorString(Error error);

/** Sink invoked only after protocol checks. finalize must verify digest and signature. */
class Sink {
 public:
  virtual ~Sink() = default;

  virtual bool begin(const Manifest &manifest) = 0;
  virtual bool write(uint32_t offset, const uint8_t *data, size_t length) = 0;
  /** Verify the complete digest and signature, then make the image bootable. */
  virtual bool finalize(const Manifest &manifest) = 0;
  virtual void abort() = 0;
};

struct Outcome {
  bool accepted = false;
  bool duplicate = false;
  State state = State::Idle;
  Error error = Error::None;
};

/**
 * MQTT delivery policy and chunk ledger shared by firmware and host/sim.
 *
 * All OTA messages require QoS 1 or 2 and must be non-retained. A duplicate
 * begin or exact duplicate chunk is idempotent. Retained messages and replay
 * of a terminal session are rejected. Chunks may arrive out of order, but
 * overlapping ranges are rejected so the sink never sees ambiguous bytes.
 */
class Session {
 public:
  explicit Session(Sink &sink);

  Outcome onMessage(const MessageMeta &meta, const uint8_t *payload, size_t length);
  Outcome onMessage(const MessageMeta &meta, const Message &message);

  State state() const;
  const Manifest &manifest() const;
  size_t receivedBytes() const;
  Error lastError() const;

 private:
  struct Range {
    uint32_t offset = 0;
    uint32_t length = 0;
    uint32_t checksum = 0;
    std::vector<uint8_t> data;
  };

  Outcome reject(Error error);
  bool sameSession(const SessionId &id) const;
  bool validateManifest(const Manifest &manifest, Error &error) const;
  bool validateChunk(const Chunk &chunk, Error &error) const;
  bool hasCompleteImage() const;

  Sink &m_Sink;
  State m_State = State::Idle;
  Error m_LastError = Error::None;
  Manifest m_Manifest {};
  std::vector<Range> m_Ranges;
  size_t m_ReceivedBytes = 0;
  SessionId m_LastSession {};
  bool m_HasLastSession = false;
};

uint32_t crc32(const uint8_t *data, size_t length);

}  // namespace MQTT
}  // namespace OTA
}  // namespace Furble

#endif
