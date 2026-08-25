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

/** OTA boot state reported by the ESP-IDF rollback API. */
enum class RollbackState : uint8_t {
  ReadError,
  Undefined,
  New,
  PendingVerify,
  Valid,
  Invalid,
  Aborted,
};

/** Reset causes that make a pending image unsafe to confirm. */
enum class ResetReason : uint8_t {
  Unknown,
  PowerOn,
  ExternalPin,
  Software,
  DeepSleep,
  Brownout,
  Panic,
  TaskWatchdog,
  InterruptWatchdog,
  OtherWatchdog,
  Sdio,
  Usb,
  Jtag,
  Efuse,
  PowerGlitch,
  CpuLockup,
};

/** Convert the raw ESP-IDF reset reason without exposing ESP headers to host tests. */
ResetReason resetReasonFromIdf(int reasonValue);

/** Convert esp_ota_get_state_partition() output; false means the read failed. */
RollbackState rollbackStateFromIdf(bool readSucceeded, uint32_t stateValue);

/** Side effect requested after evaluating a pending image. */
enum class BootAction : uint8_t {
  NoAction,
  WaitForHealth,
  MarkValid,
  MarkInvalidRollbackAndReboot,
};

/** First failed gate in the rollback health contract. */
enum class BootFailure : uint8_t {
  None,
  NotPending,
  StateRead,
  UnexpectedState,
  Nvs,
  Platform,
  Ble,
  Control,
  ServiceLoop,
  Heap,
  UnsafeReset,
};

/**
 * Hardware-independent observations needed before confirming an OTA image.
 *
 * The firmware caller supplies these observations from its existing init and
 * task seams. Keeping the policy value-only makes the rollback decision
 * deterministic in host tests and prevents a premature mark-valid call from
 * being hidden in app_main().
 */
struct BootHealth {
  static constexpr uint32_t MIN_SERVICE_TICKS = 100;

  RollbackState rollbackState = RollbackState::ReadError;
  bool nvsReady = false;
  bool platformReady = false;
  bool bleReady = false;
  bool controlReady = false;
  uint32_t serviceTicks = 0;
  size_t freeHeap = 0;
  size_t heapFloor = 0;
  ResetReason resetReason = ResetReason::Unknown;
  bool validationDeadlineReached = false;
};

struct BootDecision {
  BootAction action = BootAction::NoAction;
  BootFailure failure = BootFailure::None;
};

/** Evaluate the rollback health contract without performing hardware actions. */
BootDecision evaluateBootHealth(const BootHealth &health);

/** Stable diagnostic label for logging the decision before any reboot action. */
const char *bootFailureName(BootFailure failure);

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

}  // namespace OTA
}  // namespace Furble

#endif
