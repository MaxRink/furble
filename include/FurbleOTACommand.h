#ifndef FURBLE_OTA_COMMAND_H
#define FURBLE_OTA_COMMAND_H

#include <cstddef>
#include <cstdint>

#include "FurbleOTA.h"
#include "FurbleOTAAdmission.h"
#include "FurbleOTAStatus.h"

namespace Furble {
namespace OTA {
namespace Command {

/** Maximum accepted `cmd/ota` payload, excluding the terminator. */
constexpr size_t MAX_PAYLOAD_LENGTH = 512;

/** One parsed `cmd/ota` payload. */
struct Request {
  Action action = Action::None;
  /** Update URL, or the URL a `check <url>` names. Empty otherwise. */
  char url[Engine::MAX_URL_LENGTH] = {};
};

/**
 * Parse one `cmd/ota` payload.
 *
 * The documented grammar is the bare payload form, so a dashboard, a shell
 * script and `mosquitto_pub` all use the same bytes:
 *
 *   status              report the last known status
 *   check               re-check the last URL this device was given
 *   check <https-url>   look up the version and slot for that URL
 *   abort               cancel the running update
 *   <https-url>         start an update from that URL
 *
 * Surrounding ASCII whitespace is ignored. Keywords are lower case. Anything
 * else, an embedded NUL, or a payload longer than MAX_PAYLOAD_LENGTH is
 * invalid and is refused without touching the engine.
 */
bool parseRequest(const char *payload, size_t length, Request &request);

/**
 * Delivery metadata for one command.
 *
 * A retained OTA command is refused, because a broker restart would otherwise
 * replay an old update. The duplicate flag carries the MQTT DUP bit, so an
 * exact QoS-1 redelivery after a failed attempt does not retry by accident.
 */
struct Meta {
  bool retained = false;
  bool duplicate = false;
};

/**
 * Manifest lookup seam.
 *
 * The handler needs the available version, the image length and the inactive
 * slot capacity before it may start a download. A host double answers
 * immediately. The firmware composition fetches the same values from the
 * release manifest; no fetch is implemented in this slice.
 */
class Source {
 public:
  virtual ~Source() = default;

  struct Available {
    char version[STATUS_VERSION_BYTES] = {};
    size_t imageSize = 0;
    size_t slotCapacity = 0;
  };

  /** Fill `available` for `url`. False when the manifest cannot be trusted. */
  virtual bool fetch(const char *url, Available &available) = 0;
};

/** Runtime observations the handler must not read from hardware itself. */
class Context {
 public:
  virtual ~Context() = default;

  /** Value snapshot of every admission condition. */
  virtual Conditions conditions(void) = 0;

  /** Running firmware version, for the version comparison. */
  virtual const char *installedVersion(void) = 0;

  /** Boot health verdict the device last recorded. */
  virtual BootReport bootReport(void) = 0;
};

/**
 * A transport's specific failure label.
 *
 * The engine reports a coarse error such as verification_failed. The adapter
 * knows whether that was a digest or a signature mismatch, and a dashboard
 * needs the difference, so the composition supplies the label separately.
 */
class Detail {
 public:
  virtual ~Detail() = default;

  /** Stable label for the last transport operation, or "none". */
  virtual const char *detail(void) const = 0;
};

/**
 * Host-testable `cmd/ota` handler.
 *
 * The handler parses the payload, enforces the preconditions, starts at most
 * one update per URL, and turns the engine snapshot into the user-facing
 * status record. It never publishes and never touches a socket or flash, so
 * the same object runs in the host tests, the simulator and the MQTT owner
 * task.
 */
class Handler final {
 public:
  Handler(Engine &engine, Source &source, Context &context, Detail *detail = nullptr);

  struct Result {
    bool accepted = false;
    bool duplicate = false;
    Refusal refusal = Refusal::None;
  };

  /** Handle one delivered payload. Retained commands are refused here too. */
  Result onMessage(const Meta &meta, const char *payload, size_t length);

  /**
   * Advance one engine stage or one download poll.
   *
   * Returns true when the engine was busy and advanced. The caller owns the
   * cadence; no delay or task lives in the handler.
   */
  bool step(void);

  Status status(void) const;

  /**
   * Retain policy for the status payload.
   *
   * A final result is retained so a dashboard that connects later still sees
   * it. Live progress is not retained. The first `checking` record of a new
   * update is retained once, which replaces the previous retained result
   * instead of leaving it on the broker for the whole transfer.
   */
  bool statusRetained(void) const;

  /** Increments whenever the status record may have changed. */
  uint32_t revision(void) const;

 private:
  Result handle(const Request &request, const Meta &meta);
  Result refuse(Refusal refusal, Action action);
  Result duplicate(Action action);
  Result accept(Action action);
  bool fetchAvailable(const char *url, Source::Available &available);
  void rememberUrl(const char *url);
  void copyVersion(char (&destination)[STATUS_VERSION_BYTES], const char *source) const;
  void noteProgress(void);

  Engine &m_Engine;
  Source &m_Source;
  Context &m_Context;
  Detail *const m_Detail;

  char m_LastUrl[Engine::MAX_URL_LENGTH] = {};
  char m_UpdateUrl[Engine::MAX_URL_LENGTH] = {};
  char m_AvailableVersion[STATUS_VERSION_BYTES] = {};
  State m_UpdateTerminal = State::Idle;
  Refusal m_Refusal = Refusal::None;
  Action m_Command = Action::None;
  Outcome m_Outcome = Outcome::None;
  bool m_Checked = false;
  bool m_UpdateAvailable = false;
  bool m_ClearRetained = false;
  size_t m_ResumeOffset = 0;
  State m_LastState = State::Idle;
  uint8_t m_LastProgress = 0;
  size_t m_LastBytes = 0;
  uint32_t m_Revision = 0;
};

}  // namespace Command
}  // namespace OTA
}  // namespace Furble

#endif  // FURBLE_OTA_COMMAND_H
