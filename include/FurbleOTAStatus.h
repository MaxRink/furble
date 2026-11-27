#ifndef FURBLE_OTA_STATUS_H
#define FURBLE_OTA_STATUS_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "FurbleOTA.h"
#include "FurbleOTAAdmission.h"

namespace Furble {
namespace OTA {

/**
 * Heap owner for the status store.
 *
 * The store is created at console start and in MQTT::init(), not as a static
 * object. It is small (188 bytes) and does not need to sit in .bss on every
 * console build. The type exists only to reach the private constructor.
 */
struct StatusStoreInstance;

/** Bytes reserved for an installed or available version in a status record. */
constexpr size_t STATUS_VERSION_BYTES = 64;

/** Bytes reserved for a transport detail label. */
constexpr size_t STATUS_DETAIL_BYTES = 32;

/** The command that produced the current status. */
enum class Action : uint8_t {
  None,
  Status,
  Check,
  Update,
  Abort,
};

const char *actionName(Action action);

/** How the last command was handled. */
enum class Outcome : uint8_t {
  None,
  Accepted,
  Duplicate,
  Refused,
};

const char *outcomeName(Outcome outcome);

const char *rollbackStateName(RollbackState state);
const char *bootActionName(BootAction action);

/**
 * The boot health verdict the device last recorded.
 *
 * `slot` stays RollbackState::Undefined until a composition reads the running
 * partition and evaluates the pending image. Reporting the value it read,
 * rather than assuming healthy, is what makes a rollback visible.
 */
struct BootReport {
  RollbackState slot = RollbackState::Undefined;
  BootAction action = BootAction::NoAction;
  BootFailure failure = BootFailure::None;
};

/**
 * One user-facing OTA status record.
 *
 * The state and error come from the transport-independent engine, the reason
 * from the delivery adapter or the refusal policy, and the boot report from
 * the rollback evaluator. Keeping them separate lets a rollback be reported
 * loudly even after the update itself reported done.
 */
struct Status {
  State state = State::Idle;
  Error error = Error::None;
  uint8_t progress = 0;
  size_t bytesDownloaded = 0;
  size_t totalBytes = 0;
  Refusal reason = Refusal::None;
  /** Specific transport detail behind the engine error, for example
   * `digest_mismatch`. `none` when no transport reported one. */
  char detail[STATUS_DETAIL_BYTES] = {};
  Action command = Action::None;
  Outcome outcome = Outcome::None;
  char installedVersion[STATUS_VERSION_BYTES] = {};
  char availableVersion[STATUS_VERSION_BYTES] = {};
  bool updateAvailable = false;
  bool checked = false;
  bool busy = false;
  BootReport boot {};
};

/** True when the engine has finished and the record is a final result. */
bool statusTerminal(const Status &status);

/** True when the record reports a pending image that has not been confirmed. */
bool statusPending(const Status &status);

/**
 * JSON object published on `<root>/state/ota`.
 *
 * The payload is a flat object of stable keys, so a dashboard does not depend
 * on a nested schema. Version strings are escaped; no other field is a string
 * that can contain a quote.
 */
std::string statusJson(const Status &status);

/** Console report, one `key: value` fact per line. */
std::string formatStatus(const Status &status);

/**
 * Last reported status, shared by the MQTT publisher and the console.
 *
 * The MQTT owner task writes it and a console command reads it on another
 * task, so every access takes the mutex. The store holds a value copy and
 * never a pointer into the engine.
 */
class StatusStore {
 public:
  static StatusStore &getInstance(void);

  void set(const Status &status);
  Status get(void) const;

 private:
  StatusStore() = default;
  friend struct StatusStoreInstance;

  mutable std::mutex m_Mutex;
  Status m_Status {};
};

}  // namespace OTA
}  // namespace Furble

#endif  // FURBLE_OTA_STATUS_H
