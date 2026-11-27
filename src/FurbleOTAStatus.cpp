#include "FurbleOTAStatus.h"

#include <cstring>
#include <memory>

namespace Furble {
namespace OTA {

struct StatusStoreInstance final: StatusStore {
  StatusStoreInstance() = default;
};

namespace {

void appendJsonString(std::string &out, const char *value) {
  out += '"';
  if (value != nullptr) {
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
      const unsigned char character = static_cast<unsigned char>(*cursor);
      switch (character) {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          if (character < 0x20) {
            static constexpr char HEX[] = "0123456789abcdef";
            out += "\\u00";
            out += HEX[(character >> 4) & 0x0f];
            out += HEX[character & 0x0f];
          } else {
            out += static_cast<char>(character);
          }
          break;
      }
    }
  }
  out += '"';
}

void appendKey(std::string &out, const char *key, bool first) {
  if (!first) {
    out += ',';
  }
  out += '"';
  out += key;
  out += "\":";
}

void appendNumber(std::string &out, const char *key, size_t value, bool &first) {
  appendKey(out, key, first);
  first = false;
  out += std::to_string(value);
}

void appendBool(std::string &out, const char *key, bool value, bool &first) {
  appendKey(out, key, first);
  first = false;
  out += value ? "true" : "false";
}

void appendString(std::string &out, const char *key, const char *value, bool &first) {
  appendKey(out, key, first);
  first = false;
  appendJsonString(out, value);
}

}  // namespace

const char *actionName(Action action) {
  switch (action) {
    case Action::None:
      return "none";
    case Action::Status:
      return "status";
    case Action::Check:
      return "check";
    case Action::Update:
      return "update";
    case Action::Abort:
      return "abort";
  }
  return "unknown";
}

const char *outcomeName(Outcome outcome) {
  switch (outcome) {
    case Outcome::None:
      return "none";
    case Outcome::Accepted:
      return "accepted";
    case Outcome::Duplicate:
      return "duplicate";
    case Outcome::Refused:
      return "refused";
  }
  return "unknown";
}

const char *rollbackStateName(RollbackState state) {
  switch (state) {
    case RollbackState::ReadError:
      return "read_error";
    case RollbackState::Undefined:
      return "undefined";
    case RollbackState::New:
      return "new";
    case RollbackState::PendingVerify:
      return "pending_verify";
    case RollbackState::Valid:
      return "valid";
    case RollbackState::Invalid:
      return "invalid";
    case RollbackState::Aborted:
      return "aborted";
  }
  return "unknown";
}

const char *bootActionName(BootAction action) {
  switch (action) {
    case BootAction::NoAction:
      return "no_action";
    case BootAction::WaitForHealth:
      return "wait_for_health";
    case BootAction::MarkValid:
      return "mark_valid";
    case BootAction::MarkInvalidRollbackAndReboot:
      return "mark_invalid_rollback_and_reboot";
  }
  return "unknown";
}

bool statusTerminal(const Status &status) {
  return (status.state == State::Done) || (status.state == State::Error);
}

bool statusPending(const Status &status) {
  return status.boot.slot == RollbackState::PendingVerify;
}

std::string statusJson(const Status &status) {
  std::string out;
  out.reserve(320);
  out += '{';
  bool first = true;

  appendString(out, "state", stateName(status.state), first);
  appendString(out, "error", errorName(status.error), first);
  appendNumber(out, "progress", status.progress, first);
  appendNumber(out, "bytes", status.bytesDownloaded, first);
  appendNumber(out, "total", status.totalBytes, first);
  appendString(out, "reason", refusalName(status.reason), first);
  appendString(out, "detail", (status.detail[0] == '\0') ? "none" : status.detail, first);
  appendString(out, "command", actionName(status.command), first);
  appendString(out, "result", outcomeName(status.outcome), first);
  appendString(out, "installed",
               (status.installedVersion[0] == '\0') ? "unknown" : status.installedVersion, first);
  appendString(out, "available",
               (status.availableVersion[0] == '\0') ? "unknown" : status.availableVersion, first);
  appendBool(out, "update", status.updateAvailable, first);
  appendBool(out, "checked", status.checked, first);
  appendBool(out, "busy", status.busy, first);
  appendString(out, "slot", rollbackStateName(status.boot.slot), first);
  appendString(out, "boot", bootActionName(status.boot.action), first);
  appendString(out, "failure", bootFailureName(status.boot.failure), first);

  out += '}';
  return out;
}

std::string formatStatus(const Status &status) {
  std::string out;
  out += "ota: ";
  out += stateName(status.state);
  out += ", error ";
  out += errorName(status.error);
  out += '\n';
  out += "progress: ";
  out += std::to_string(status.progress);
  out += " percent, ";
  out += std::to_string(status.bytesDownloaded);
  out += " of ";
  out += std::to_string(status.totalBytes);
  out += " bytes\n";
  out += "command: ";
  out += actionName(status.command);
  out += ", result ";
  out += outcomeName(status.outcome);
  out += ", reason ";
  out += refusalName(status.reason);
  out += ", detail ";
  out += (status.detail[0] == '\0') ? "none" : status.detail;
  out += '\n';
  out += "installed: ";
  out += (status.installedVersion[0] == '\0') ? "unknown" : status.installedVersion;
  out += '\n';
  out += "available: ";
  out += (status.availableVersion[0] == '\0') ? "unknown" : status.availableVersion;
  out += ", update ";
  out += status.checked ? (status.updateAvailable ? "yes" : "no") : "unchecked";
  out += '\n';
  out += "slot: ";
  out += rollbackStateName(status.boot.slot);
  out += ", boot ";
  out += bootActionName(status.boot.action);
  out += ", failure ";
  out += bootFailureName(status.boot.failure);
  out += '\n';
  return out;
}

StatusStore &StatusStore::getInstance(void) {
  // Created at console start and in MQTT::init(), so no command or publish
  // path allocates. A failed allocation aborts under the image-wide heap
  // policy.
  static std::unique_ptr<StatusStoreInstance> instance = std::make_unique<StatusStoreInstance>();
  return *instance;
}

void StatusStore::set(const Status &status) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_Status = status;
}

Status StatusStore::get(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Status;
}

}  // namespace OTA
}  // namespace Furble
