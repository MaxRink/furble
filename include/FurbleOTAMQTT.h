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
constexpr size_t KEY_ID_BYTES = 8;
constexpr size_t DIGEST_BYTES = 32;
constexpr size_t SIGNATURE_BYTES = 64;
constexpr size_t MAX_VERSION_BYTES = 63;
constexpr size_t MAX_IMAGE_BYTES = 16 * 1024 * 1024;
constexpr size_t MAX_CHUNK_BYTES = 4096;
constexpr size_t OTA_LEDGER_BUDGET_BYTES = 1024;
#ifndef FURBLE_OTA_LEDGER_ENTRIES
#define FURBLE_OTA_LEDGER_ENTRIES 64
#endif
constexpr size_t OTA_LEDGER_ENTRIES = FURBLE_OTA_LEDGER_ENTRIES;
constexpr size_t OTA_SESSION_BUDGET_BYTES = 2048;
static_assert((OTA_LEDGER_ENTRIES > 0) && ((OTA_LEDGER_ENTRIES * 16) <= OTA_LEDGER_BUDGET_BYTES),
              "OTA ledger exceeds the small-target memory budget");
constexpr uint8_t MIN_QOS = 1;
constexpr uint8_t MAX_QOS = 2;

using SessionId = std::array<uint8_t, SESSION_ID_BYTES>;
using KeyId = std::array<uint8_t, KEY_ID_BYTES>;
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
  ReplayStoreFailed,
  InvalidQoS,
  RetainedMessage,
  Busy,
  Replay,
  WrongSession,
  InvalidSequence,
  InvalidChunk,
  ChunkTooLarge,
  ChunkOutOfBounds,
  ChunkChecksum,
  ChunkOverlap,
  ChunkLedgerFull,
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
  KeyId keyId {};
  uint32_t imageSize = 0;
  uint32_t partitionSize = 0;
  uint32_t rollbackCounter = 0;
  Digest digest {};
  SignatureAlgorithm signatureAlgorithm = SignatureAlgorithm::EcdsaP256Sha256;
  std::vector<uint8_t> signature;
  std::vector<uint8_t> version;

  bool operator==(const Manifest &other) const;
};

struct ErrorInfo {
  Error code = Error::None;
  size_t offset = 0;
};

/**
 * Build the canonical bytes covered by Manifest::signature.
 *
 * The result excludes the signature itself. Key IDs identify a trust anchor,
 * rollbackCounter is monotonic metadata enforced by the sink, and an ECDSA
 * signature is the fixed-width raw r || s encoding, not a DER sequence.
 */
bool canonicalSignedBytes(const Manifest &manifest,
                          std::vector<uint8_t> &out,
                          ErrorInfo *error = nullptr);

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
  /**
   * Confirm that bytes already persisted at this range match this retry.
   * Implementations should read from the flash-backed sink, not retain the
   * complete image in RAM. Returning false makes a same-range retry fail
   * closed as an overlap.
   */
  virtual bool matches(uint32_t offset, const uint8_t *data, size_t length, uint32_t checksum) = 0;
  /** Verify and stage the complete digest/signature without activating it. */
  virtual bool finalize(const Manifest &manifest) = 0;
  /** Activate the verified image after the replay floor is durably committed. */
  virtual bool activate() = 0;
  virtual void abort() = 0;
};

/**
 * Durable anti-rollback storage owned by the platform adapter.
 *
 * loadFloor() must return the installed or reserved signed-update counter.
 * reserveFloor() is an atomic compare-and-swap persistence operation: it
 * succeeds only when expectedFloor is still stored and nextFloor is strictly
 * greater and no other reservation is outstanding. Reservation happens at
 * BEGIN, after the sink accepts the manifest, so an aborted or failed update
 * cannot reuse its signed counter while sink setup failures cannot burn one. The
 * owner token prevents another session from completing this reservation.
 * markStaged(), completeReservation(), and abandonReservation() are atomic
 * owner-and-counter CAS transitions. completeReservation() consumes the
 * reservation before irreversible boot-partition activation; activation
 * failure therefore leaves the old image selected but never blocks the floor.
 * recoverAbandonedReservation() is called
 * by the platform after reboot when staged data is known to be abandoned; it
 * clears the owner while preserving the consumed floor.
 * Flash/NVS adapters must journal this record before acknowledging it and
 * recover the last complete record after reboot.
 */
class ReplayStore {
 public:
  virtual ~ReplayStore() = default;
  virtual bool loadFloor(uint32_t &floor) = 0;
  virtual bool reserveFloor(uint32_t expectedFloor, uint32_t nextFloor, const SessionId &owner) = 0;
  virtual bool markStaged(const SessionId &owner, uint32_t counter) = 0;
  virtual bool completeReservation(const SessionId &owner, uint32_t counter) = 0;
  virtual bool abandonReservation(const SessionId &owner, uint32_t counter) = 0;
  virtual bool recoverAbandonedReservation() = 0;
};

struct Outcome {
  bool accepted = false;
  bool duplicate = false;
  State state = State::Idle;
  Error error = Error::None;
};

/**
 * MQTT delivery policy and bounded chunk ledger shared by firmware and host/sim.
 *
 * All OTA messages require QoS 1 or 2 and must be non-retained. A duplicate
 * begin or exact duplicate chunk is idempotent. Retained messages and signed
 * updates at or below the durable replay floor are rejected. Chunks may arrive
 * out of order, but overlapping ranges are rejected so the sink never sees
 * ambiguous bytes.
 */
class Session {
 public:
  /**
   * The owner must keep this object in persistent task/static storage and
   * serialize all onMessage calls. The implementation rejects reentrant
   * dispatch from sink callbacks; platform adapters must serialize concurrent
   * MQTT callbacks before entering this API.
   */
  Session(Sink &sink, ReplayStore &replayStore);

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
    uint32_t sequence = 0;
  };

  Outcome reject(Error error);
  bool sameSession(const SessionId &id) const;
  bool validateManifest(const Manifest &manifest, Error &error) const;
  bool validateChunk(const Chunk &chunk, Error &error) const;
  bool hasCompleteImage() const;
  bool sequenceUsed(uint32_t sequence) const;
  Outcome terminalFailure(Error error);
  void abandonReservation();
  Outcome dispatchMessage(const MessageMeta &meta, const Message &message);

  Sink &m_Sink;
  ReplayStore &m_ReplayStore;
  State m_State = State::Idle;
  Error m_LastError = Error::None;
  Manifest m_Manifest {};
  static_assert(sizeof(Range) == 16, "OTA range metadata must remain compact");
  std::array<Range, OTA_LEDGER_ENTRIES> m_Ranges {};
  size_t m_RangeCount = 0;
  size_t m_ReceivedBytes = 0;
  uint32_t m_BeginSequence = 0;
  uint32_t m_TerminalSequence = 0;
  bool m_Dispatching = false;
  bool m_ReservationActive = false;
  bool m_ReplayRecoveryChecked = false;
};

static_assert(sizeof(Session) <= OTA_SESSION_BUDGET_BYTES,
              "OTA Session exceeds the small-target object budget");

uint32_t crc32(const uint8_t *data, size_t length);

}  // namespace MQTT
}  // namespace OTA
}  // namespace Furble

#endif
