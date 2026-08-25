#include "FurbleOTAMQTT.h"

#include <cstring>
#include <iterator>

namespace Furble {
namespace OTA {
namespace MQTT {

namespace {

constexpr uint8_t MAGIC[] = {0x46, 0x4f, 0x41, 0x31};         // "FOA1"
constexpr uint8_t SIGNED_MAGIC[] = {0x46, 0x4f, 0x4d, 0x31};  // "FOM1"
constexpr size_t HEADER_BYTES = 4 + 1 + SESSION_ID_BYTES + 4 + 4;
constexpr size_t BEGIN_FIXED_BYTES = 1 + 4 + 4 + DIGEST_BYTES + 1 + KEY_ID_BYTES + 4 + 2;
constexpr size_t CHUNK_FIXED_BYTES = 4 + 2 + 4;

void setError(ErrorInfo *error, Error code, size_t offset) {
  if (error != nullptr) {
    error->code = code;
    error->offset = offset;
  }
}

bool isKind(uint8_t value) {
  return (value >= static_cast<uint8_t>(Kind::Begin))
         && (value <= static_cast<uint8_t>(Kind::Abort));
}

bool isSignatureAlgorithm(SignatureAlgorithm algorithm) {
  return (algorithm == SignatureAlgorithm::EcdsaP256Sha256)
         || (algorithm == SignatureAlgorithm::Ed25519);
}

bool allZero(const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (data[i] != 0) {
      return false;
    }
  }
  return true;
}

bool isZero(const KeyId &keyId) {
  return allZero(keyId.data(), keyId.size());
}

uint32_t read32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t read16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

void append16(std::vector<uint8_t> &out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void append32(std::vector<uint8_t> &out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 24));
}

bool validVersion(const std::vector<uint8_t> &version) {
  if (version.empty() || version.size() > MAX_VERSION_BYTES) {
    return false;
  }
  return std::memchr(version.data(), '\0', version.size()) == nullptr;
}

bool validDigest(const Digest &digest) {
  return !allZero(digest.data(), digest.size());
}

bool validateManifestWire(const Manifest &manifest, Error &error) {
  if (!validVersion(manifest.version)) {
    error = Error::InvalidVersion;
    return false;
  }
  if ((manifest.imageSize == 0) || (manifest.imageSize > MAX_IMAGE_BYTES)) {
    error = Error::InvalidImageSize;
    return false;
  }
  if ((manifest.partitionSize < manifest.imageSize) || (manifest.partitionSize > MAX_IMAGE_BYTES)) {
    error = Error::InvalidPartitionSize;
    return false;
  }
  if (!validDigest(manifest.digest)) {
    error = Error::InvalidDigest;
    return false;
  }
  if (isZero(manifest.keyId)) {
    error = Error::InvalidManifest;
    return false;
  }
  if (!isSignatureAlgorithm(manifest.signatureAlgorithm)) {
    error = Error::UnsupportedSignature;
    return false;
  }
  if (manifest.signature.size() != SIGNATURE_BYTES) {
    error = Error::InvalidSignature;
    return false;
  }
  return true;
}

void appendCanonical(std::vector<uint8_t> &out, const Manifest &manifest) {
  out.insert(out.end(), std::begin(SIGNED_MAGIC), std::end(SIGNED_MAGIC));
  out.insert(out.end(), manifest.sessionId.begin(), manifest.sessionId.end());
  append32(out, manifest.imageSize);
  append32(out, manifest.partitionSize);
  append32(out, manifest.rollbackCounter);
  out.insert(out.end(), manifest.digest.begin(), manifest.digest.end());
  out.push_back(static_cast<uint8_t>(manifest.signatureAlgorithm));
  out.insert(out.end(), manifest.keyId.begin(), manifest.keyId.end());
  out.push_back(static_cast<uint8_t>(manifest.version.size()));
  out.insert(out.end(), manifest.version.begin(), manifest.version.end());
}

bool appendHeader(const Message &message,
                  uint32_t bodyLength,
                  std::vector<uint8_t> &out,
                  ErrorInfo *error) {
  if (!isKind(static_cast<uint8_t>(message.kind))) {
    setError(error, Error::UnsupportedKind, 4);
    return false;
  }
  if (message.sequence == 0) {
    setError(error, Error::InvalidSequence, 9 + SESSION_ID_BYTES);
    return false;
  }
  if (bodyLength > UINT32_MAX - HEADER_BYTES) {
    setError(error, Error::Malformed, HEADER_BYTES);
    return false;
  }
  out.insert(out.end(), std::begin(MAGIC), std::end(MAGIC));
  out.push_back(static_cast<uint8_t>(message.kind));
  out.insert(out.end(), message.sessionId.begin(), message.sessionId.end());
  append32(out, message.sequence);
  append32(out, bodyLength);
  return true;
}

}  // namespace

bool Manifest::operator==(const Manifest &other) const {
  return (sessionId == other.sessionId) && (keyId == other.keyId) && (imageSize == other.imageSize)
         && (partitionSize == other.partitionSize) && (digest == other.digest)
         && (rollbackCounter == other.rollbackCounter)
         && (signatureAlgorithm == other.signatureAlgorithm) && (signature == other.signature)
         && (version == other.version);
}

bool Chunk::operator==(const Chunk &other) const {
  return (sessionId == other.sessionId) && (offset == other.offset) && (data == other.data)
         && (checksum == other.checksum);
}

bool canonicalSignedBytes(const Manifest &manifest, std::vector<uint8_t> &out, ErrorInfo *error) {
  Error manifestError = Error::None;
  if (!validateManifestWire(manifest, manifestError)) {
    setError(error, manifestError, 0);
    return false;
  }
  std::vector<uint8_t> canonical;
  canonical.reserve(4 + SESSION_ID_BYTES + 4 + 4 + 4 + DIGEST_BYTES + 1 + KEY_ID_BYTES + 1
                    + manifest.version.size());
  appendCanonical(canonical, manifest);
  out = std::move(canonical);
  setError(error, Error::None, 0);
  return true;
}

uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

bool decode(const uint8_t *data, size_t length, Message &out, ErrorInfo *error) {
  if (data == nullptr) {
    setError(error, Error::NullInput, 0);
    return false;
  }
  if (length < HEADER_BYTES) {
    setError(error, Error::Truncated, length);
    return false;
  }
  if (std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0) {
    setError(error, Error::Malformed, 0);
    return false;
  }
  if (!isKind(data[4])) {
    setError(error, Error::UnsupportedKind, 4);
    return false;
  }

  const size_t bodyLength = read32(data + HEADER_BYTES - 4);
  if (bodyLength != (length - HEADER_BYTES)) {
    setError(error, bodyLength > (length - HEADER_BYTES) ? Error::Truncated : Error::Malformed,
             HEADER_BYTES - 4);
    return false;
  }

  Message decoded;
  decoded.kind = static_cast<Kind>(data[4]);
  std::memcpy(decoded.sessionId.data(), data + 5, SESSION_ID_BYTES);
  decoded.sequence = read32(data + 5 + SESSION_ID_BYTES);
  const uint8_t *body = data + HEADER_BYTES;

  if (decoded.kind == Kind::Begin) {
    if (bodyLength < BEGIN_FIXED_BYTES) {
      setError(error, Error::Truncated, HEADER_BYTES);
      return false;
    }
    const size_t versionLength = body[0];
    const size_t signatureLength = read16(body + 1 + 4 + 4 + DIGEST_BYTES + 1 + KEY_ID_BYTES + 4);
    const size_t expected = BEGIN_FIXED_BYTES + versionLength + signatureLength;
    if (expected != bodyLength) {
      setError(error, expected > bodyLength ? Error::Truncated : Error::Malformed,
               HEADER_BYTES + bodyLength);
      return false;
    }
    decoded.manifest.sessionId = decoded.sessionId;
    decoded.manifest.imageSize = read32(body + 1);
    decoded.manifest.partitionSize = read32(body + 5);
    std::memcpy(decoded.manifest.digest.data(), body + 9, DIGEST_BYTES);
    decoded.manifest.signatureAlgorithm = static_cast<SignatureAlgorithm>(body[41]);
    std::memcpy(decoded.manifest.keyId.data(), body + 42, KEY_ID_BYTES);
    decoded.manifest.rollbackCounter = read32(body + 42 + KEY_ID_BYTES);
    decoded.manifest.version.assign(body + BEGIN_FIXED_BYTES,
                                    body + BEGIN_FIXED_BYTES + versionLength);
    decoded.manifest.signature.assign(body + BEGIN_FIXED_BYTES + versionLength, body + expected);
    Error manifestError = Error::None;
    if (!validateManifestWire(decoded.manifest, manifestError)) {
      setError(error, manifestError, HEADER_BYTES);
      return false;
    }
  } else if (decoded.kind == Kind::Chunk) {
    if ((bodyLength < CHUNK_FIXED_BYTES) || (read16(body + 4) == 0)) {
      setError(error, bodyLength < CHUNK_FIXED_BYTES ? Error::Truncated : Error::InvalidChunk,
               HEADER_BYTES);
      return false;
    }
    const size_t chunkLength = read16(body + 4);
    if (chunkLength > MAX_CHUNK_BYTES) {
      setError(error, Error::ChunkTooLarge, HEADER_BYTES + 4);
      return false;
    }
    if (bodyLength != CHUNK_FIXED_BYTES + chunkLength) {
      setError(error,
               bodyLength < CHUNK_FIXED_BYTES + chunkLength ? Error::Truncated : Error::Malformed,
               HEADER_BYTES + bodyLength);
      return false;
    }
    decoded.chunk.sessionId = decoded.sessionId;
    decoded.chunk.offset = read32(body);
    decoded.chunk.data.assign(body + CHUNK_FIXED_BYTES, body + bodyLength);
    decoded.chunk.checksum = read32(body + 6);
    if (crc32(decoded.chunk.data.data(), decoded.chunk.data.size()) != decoded.chunk.checksum) {
      setError(error, Error::ChunkChecksum, HEADER_BYTES + 6);
      return false;
    }
  } else if (bodyLength != 0) {
    setError(error, Error::Malformed, HEADER_BYTES);
    return false;
  }

  out = std::move(decoded);
  setError(error, Error::None, 0);
  return true;
}

bool encode(const Message &message, std::vector<uint8_t> &out, ErrorInfo *error) {
  std::vector<uint8_t> encoded;
  if (message.kind == Kind::Begin) {
    if (message.sequence == 0) {
      setError(error, Error::InvalidSequence, 9 + SESSION_ID_BYTES);
      return false;
    }
    Manifest manifest = message.manifest;
    if (manifest.sessionId != message.sessionId) {
      setError(error, Error::WrongSession, 5);
      return false;
    }
    Error manifestError = Error::None;
    if (!validateManifestWire(manifest, manifestError)) {
      setError(error, manifestError, HEADER_BYTES);
      return false;
    }
    if (manifest.version.size() > UINT8_MAX || manifest.signature.size() > UINT16_MAX) {
      setError(error, Error::Malformed, HEADER_BYTES);
      return false;
    }
    const uint32_t bodyLength = static_cast<uint32_t>(BEGIN_FIXED_BYTES + manifest.version.size()
                                                      + manifest.signature.size());
    if (!appendHeader(message, bodyLength, encoded, error)) {
      return false;
    }
    encoded.push_back(static_cast<uint8_t>(manifest.version.size()));
    append32(encoded, manifest.imageSize);
    append32(encoded, manifest.partitionSize);
    encoded.insert(encoded.end(), manifest.digest.begin(), manifest.digest.end());
    encoded.push_back(static_cast<uint8_t>(manifest.signatureAlgorithm));
    encoded.insert(encoded.end(), manifest.keyId.begin(), manifest.keyId.end());
    append32(encoded, manifest.rollbackCounter);
    append16(encoded, static_cast<uint16_t>(manifest.signature.size()));
    encoded.insert(encoded.end(), manifest.version.begin(), manifest.version.end());
    encoded.insert(encoded.end(), manifest.signature.begin(), manifest.signature.end());
  } else if (message.kind == Kind::Chunk) {
    if (message.chunk.sessionId != message.sessionId) {
      setError(error, Error::WrongSession, 5);
      return false;
    }
    if (message.chunk.data.empty()) {
      setError(error, Error::InvalidChunk, HEADER_BYTES);
      return false;
    }
    if (message.chunk.data.size() > MAX_CHUNK_BYTES || message.chunk.data.size() > UINT16_MAX) {
      setError(error, Error::ChunkTooLarge, HEADER_BYTES + 4);
      return false;
    }
    if (crc32(message.chunk.data.data(), message.chunk.data.size()) != message.chunk.checksum) {
      setError(error, Error::ChunkChecksum, HEADER_BYTES + 6);
      return false;
    }
    const uint32_t bodyLength =
        static_cast<uint32_t>(CHUNK_FIXED_BYTES + message.chunk.data.size());
    if (!appendHeader(message, bodyLength, encoded, error)) {
      return false;
    }
    append32(encoded, message.chunk.offset);
    append16(encoded, static_cast<uint16_t>(message.chunk.data.size()));
    append32(encoded, message.chunk.checksum);
    encoded.insert(encoded.end(), message.chunk.data.begin(), message.chunk.data.end());
  } else {
    if (!appendHeader(message, 0, encoded, error)) {
      return false;
    }
  }
  if (encoded.empty()) {
    return false;
  }
  out = std::move(encoded);
  setError(error, Error::None, 0);
  return true;
}

const char *errorString(Error error) {
  switch (error) {
    case Error::None:
      return "none";
    case Error::NullInput:
      return "null_input";
    case Error::Truncated:
      return "truncated";
    case Error::Malformed:
      return "malformed";
    case Error::UnsupportedKind:
      return "unsupported_kind";
    case Error::UnsupportedSignature:
      return "unsupported_signature";
    case Error::InvalidManifest:
      return "invalid_manifest";
    case Error::InvalidVersion:
      return "invalid_version";
    case Error::InvalidImageSize:
      return "invalid_image_size";
    case Error::InvalidPartitionSize:
      return "invalid_partition_size";
    case Error::InvalidSignature:
      return "invalid_signature";
    case Error::InvalidDigest:
      return "invalid_digest";
    case Error::InvalidQoS:
      return "invalid_qos";
    case Error::RetainedMessage:
      return "retained_message";
    case Error::Busy:
      return "busy";
    case Error::Replay:
      return "replay";
    case Error::WrongSession:
      return "wrong_session";
    case Error::InvalidSequence:
      return "invalid_sequence";
    case Error::InvalidChunk:
      return "invalid_chunk";
    case Error::ChunkTooLarge:
      return "chunk_too_large";
    case Error::ChunkOutOfBounds:
      return "chunk_out_of_bounds";
    case Error::ChunkChecksum:
      return "chunk_checksum";
    case Error::ChunkOverlap:
      return "chunk_overlap";
    case Error::ChunkLedgerFull:
      return "chunk_ledger_full";
    case Error::SinkRejected:
      return "sink_rejected";
    case Error::Incomplete:
      return "incomplete";
    case Error::VerificationFailed:
      return "verification_failed";
    case Error::Aborted:
      return "aborted";
  }
  return "unknown";
}

Session::Session(Sink &sink) : m_Sink(sink) {}

Outcome Session::onMessage(const MessageMeta &meta, const uint8_t *payload, size_t length) {
  if ((meta.qos < MIN_QOS) || (meta.qos > MAX_QOS)) {
    return reject(Error::InvalidQoS);
  }
  if (meta.retained) {
    return reject(Error::RetainedMessage);
  }
  Message message;
  ErrorInfo error;
  if (!decode(payload, length, message, &error)) {
    return reject(error.code);
  }
  return onMessage(meta, message);
}

Outcome Session::onMessage(const MessageMeta &meta, const Message &message) {
  if ((meta.qos < MIN_QOS) || (meta.qos > MAX_QOS)) {
    return reject(Error::InvalidQoS);
  }
  if (meta.retained) {
    return reject(Error::RetainedMessage);
  }

  if (message.kind == Kind::Begin) {
    if (message.sequence == 0) {
      return reject(Error::InvalidSequence);
    }
    Error manifestError = Error::None;
    if (!validateManifest(message.manifest, manifestError)
        || (message.manifest.sessionId != message.sessionId)) {
      return reject(message.manifest.sessionId != message.sessionId ? Error::WrongSession
                                                                    : manifestError);
    }
    if (m_State == State::Receiving || m_State == State::Verifying) {
      if (sameSession(message.sessionId) && (message.manifest == m_Manifest)
          && (message.sequence == m_BeginSequence)) {
        return {true, true, m_State, Error::None};
      }
      return reject(Error::Busy);
    }
    if (terminalSession(message.sessionId)) {
      return reject(Error::Replay);
    }
    m_Manifest = message.manifest;
    m_BeginSequence = message.sequence;
    if (!m_Sink.begin(message.manifest)) {
      return terminalFailure(Error::SinkRejected);
    }
    m_RangeCount = 0;
    m_ReceivedBytes = 0;
    m_State = State::Receiving;
    m_LastError = Error::None;
    return {true, false, m_State, Error::None};
  }

  if (message.kind == Kind::Chunk) {
    if (message.chunk.sessionId != message.sessionId) {
      return reject(Error::WrongSession);
    }
    Error chunkError = Error::None;
    if (!validateChunk(message.chunk, chunkError)) {
      return reject(chunkError);
    }
    if (message.sequence == 0) {
      return reject(Error::InvalidSequence);
    }
    if (m_State != State::Receiving) {
      return reject(m_State == State::Idle ? Error::Busy : Error::Replay);
    }
    if (!sameSession(message.sessionId)) {
      return reject(Error::WrongSession);
    }
    const uint32_t length = static_cast<uint32_t>(message.chunk.data.size());
    for (size_t index = 0; index < m_RangeCount; index++) {
      const Range &range = m_Ranges[index];
      const uint64_t oldEnd = static_cast<uint64_t>(range.offset) + range.length;
      const uint64_t newEnd = static_cast<uint64_t>(message.chunk.offset) + length;
      if ((range.offset == message.chunk.offset) && (range.length == length)) {
        if ((range.sequence == message.sequence) && (range.checksum == message.chunk.checksum)
            && m_Sink.matches(message.chunk.offset, message.chunk.data.data(), length,
                              message.chunk.checksum)) {
          return {true, true, m_State, Error::None};
        }
        return reject(Error::ChunkOverlap);
      }
      if ((message.chunk.offset < oldEnd) && (range.offset < newEnd)) {
        return reject(Error::ChunkOverlap);
      }
    }
    if (sequenceUsed(message.sequence)) {
      return reject(Error::Replay);
    }
    if (m_RangeCount == m_Ranges.size()) {
      return reject(Error::ChunkLedgerFull);
    }
    if (!m_Sink.write(message.chunk.offset, message.chunk.data.data(), length)) {
      return terminalFailure(Error::SinkRejected);
    }
    m_Ranges[m_RangeCount++] = {message.chunk.offset, length, message.chunk.checksum,
                                message.sequence};
    m_ReceivedBytes += length;
    m_LastError = Error::None;
    return {true, false, m_State, Error::None};
  }

  if (!sameSession(message.sessionId)) {
    return reject(m_State == State::Idle ? Error::Busy : Error::WrongSession);
  }
  if (message.kind == Kind::Commit) {
    if (message.sequence == 0) {
      return reject(Error::InvalidSequence);
    }
    if (m_State == State::Done) {
      return (message.sequence == m_TerminalSequence) ? Outcome {true, true, m_State, Error::None}
                                                      : reject(Error::Replay);
    }
    if (m_State != State::Receiving) {
      return reject((m_State == State::Aborted) || (m_State == State::Error) ? Error::Replay
                                                                             : Error::Busy);
    }
    if (sequenceUsed(message.sequence)) {
      return reject(Error::Replay);
    }
    if (!hasCompleteImage()) {
      return reject(Error::Incomplete);
    }
    m_State = State::Verifying;
    if (!m_Sink.finalize(m_Manifest)) {
      return terminalFailure(Error::VerificationFailed);
    }
    m_State = State::Done;
    m_TerminalKind = Kind::Commit;
    m_TerminalSequence = message.sequence;
    rememberTerminalSession(m_Manifest.sessionId);
    m_LastError = Error::None;
    return {true, false, m_State, Error::None};
  }

  if (message.kind == Kind::Abort) {
    if (message.sequence == 0) {
      return reject(Error::InvalidSequence);
    }
    if (m_State == State::Aborted) {
      return (message.sequence == m_TerminalSequence) ? Outcome {true, true, m_State, Error::None}
                                                      : reject(Error::Replay);
    }
    if ((m_State != State::Receiving) && (m_State != State::Verifying)) {
      return reject((m_State == State::Done) || (m_State == State::Error) ? Error::Replay
                                                                          : Error::Busy);
    }
    if (sequenceUsed(message.sequence)) {
      return reject(Error::Replay);
    }
    m_Sink.abort();
    m_State = State::Aborted;
    m_TerminalKind = Kind::Abort;
    m_TerminalSequence = message.sequence;
    rememberTerminalSession(m_Manifest.sessionId);
    m_LastError = Error::Aborted;
    return {true, false, m_State, Error::Aborted};
  }

  return reject(Error::UnsupportedKind);
}

State Session::state() const {
  return m_State;
}

const Manifest &Session::manifest() const {
  return m_Manifest;
}

size_t Session::receivedBytes() const {
  return m_ReceivedBytes;
}

Error Session::lastError() const {
  return m_LastError;
}

Outcome Session::reject(Error error) {
  m_LastError = error;
  return {false, false, m_State, error};
}

bool Session::sameSession(const SessionId &id) const {
  return (m_State != State::Idle) && (id == m_Manifest.sessionId);
}

bool Session::sequenceUsed(uint32_t sequence) const {
  if (sequence == m_BeginSequence) {
    return true;
  }
  for (size_t index = 0; index < m_RangeCount; index++) {
    if (m_Ranges[index].sequence == sequence) {
      return true;
    }
  }
  return false;
}

bool Session::terminalSession(const SessionId &id) const {
  for (size_t index = 0; index < m_TerminalSessionCount; index++) {
    if (m_TerminalSessions[index] == id) {
      return true;
    }
  }
  return false;
}

void Session::rememberTerminalSession(const SessionId &id) {
  if (terminalSession(id)) {
    return;
  }
  if (m_TerminalSessionCount < m_TerminalSessions.size()) {
    m_TerminalSessions[m_TerminalSessionCount++] = id;
    return;
  }
  m_TerminalSessions[m_TerminalSessionCursor] = id;
  m_TerminalSessionCursor = (m_TerminalSessionCursor + 1) % m_TerminalSessions.size();
}

Outcome Session::terminalFailure(Error error) {
  m_Sink.abort();
  m_State = State::Error;
  m_TerminalKind = Kind::Abort;
  m_TerminalSequence = 0;
  rememberTerminalSession(m_Manifest.sessionId);
  return reject(error);
}

bool Session::validateManifest(const Manifest &manifest, Error &error) const {
  if (!validateManifestWire(manifest, error)) {
    return false;
  }
  if (manifest.sessionId == SessionId {}) {
    error = Error::InvalidManifest;
    return false;
  }
  return true;
}

bool Session::validateChunk(const Chunk &chunk, Error &error) const {
  if (chunk.sessionId == SessionId {}) {
    error = Error::WrongSession;
    return false;
  }
  if (chunk.data.empty()) {
    error = Error::InvalidChunk;
    return false;
  }
  if (chunk.data.size() > MAX_CHUNK_BYTES) {
    error = Error::ChunkTooLarge;
    return false;
  }
  if (crc32(chunk.data.data(), chunk.data.size()) != chunk.checksum) {
    error = Error::ChunkChecksum;
    return false;
  }
  const uint64_t end = static_cast<uint64_t>(chunk.offset) + chunk.data.size();
  if ((end > UINT32_MAX) || (end > m_Manifest.imageSize)) {
    error = Error::ChunkOutOfBounds;
    return false;
  }
  return true;
}

bool Session::hasCompleteImage() const {
  if (m_ReceivedBytes != m_Manifest.imageSize) {
    return false;
  }
  uint32_t next = 0;
  for (size_t position = 0; position < m_RangeCount; position++) {
    size_t match = m_RangeCount;
    for (size_t index = 0; index < m_RangeCount; index++) {
      if (m_Ranges[index].offset == next) {
        match = index;
        break;
      }
    }
    if (match == m_RangeCount) {
      return false;
    }
    next += m_Ranges[match].length;
  }
  return next == m_Manifest.imageSize;
}

}  // namespace MQTT
}  // namespace OTA
}  // namespace Furble
