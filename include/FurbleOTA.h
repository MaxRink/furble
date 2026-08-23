#ifndef FURBLE_OTA_H
#define FURBLE_OTA_H

#include <cstddef>
#include <cstdint>

namespace Furble {
namespace OTA {

/** Lifecycle of one explicit OTA update request. */
enum class State : uint8_t {
  Idle,
  Checking,
  Downloading,
  Verifying,
  Applying,
  Done,
  Error,
};

/** Failure reported by the state machine. */
enum class Error : uint8_t {
  None,
  Busy,
  InvalidRequest,
  InvalidResume,
  CheckFailed,
  DownloadFailed,
  VerificationFailed,
  ApplyFailed,
  Aborted,
};

/** Result of one non-blocking transport download poll. */
enum class DownloadResult : uint8_t {
  InProgress,
  Complete,
  Failed,
};

struct DownloadProgress {
  DownloadResult result = DownloadResult::InProgress;
  size_t bytesDownloaded = 0;
  size_t totalBytes = 0;
};

/**
 * Narrow seam between the OTA state machine and a delivery implementation.
 *
 * check() opens the update source and returns its image size when known. The
 * resume offset is a byte checkpoint from a previous attempt. download() is
 * polled until it returns Complete. verify() authenticates and closes the
 * staged image, while apply() makes it the next boot image. A transport must
 * make abort() safe before and after a failed operation.
 */
class Transport {
 public:
  virtual ~Transport() = default;

  virtual bool check(const char *url, size_t resumeOffset, size_t &totalBytes) = 0;
  virtual DownloadProgress download() = 0;
  virtual bool verify() = 0;
  virtual bool apply() = 0;
  virtual void abort() = 0;
};

struct Snapshot {
  State state = State::Idle;
  Error error = Error::None;
  uint8_t progress = 0;
  size_t bytesDownloaded = 0;
  size_t totalBytes = 0;
  size_t resumeOffset = 0;
};

/**
 * Transport-independent OTA lifecycle controller.
 *
 * The caller starts an update with begin() and calls step() from its own task
 * until busy() is false. The engine has no task, delay, mutex, or network
 * dependency, which keeps the lifecycle directly host-testable.
 */
class Engine {
 public:
  static constexpr size_t MAX_URL_LENGTH = 256;

  explicit Engine(Transport &transport);

  /** Start a new update, optionally from a transport checkpoint. */
  bool begin(const char *url, size_t resumeOffset = 0);

  /** Advance one lifecycle stage or one transport download poll. */
  bool step();

  /** Cancel a running update and leave it resumable at its last checkpoint. */
  void abort();

  bool busy() const;
  const Snapshot &snapshot() const;
  Error lastError() const;

 private:
  bool fail(Error error);
  void updateProgress(size_t bytesDownloaded, size_t totalBytes);

  Transport &m_Transport;
  Snapshot m_Snapshot;
  Error m_LastError = Error::None;
  char m_Url[MAX_URL_LENGTH] = {};
};

const char *stateName(State state);
const char *errorName(Error error);

/** The firmware's default esp_https_ota-backed engine. */
Engine &getEngine();

}  // namespace OTA
}  // namespace Furble

#endif
