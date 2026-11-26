#include "FurbleOTACommand.h"

#include <cstring>

namespace Furble {
namespace OTA {
namespace Command {

namespace {

constexpr char HTTPS_PREFIX[] = "https://";

bool isSpace(char character) {
  return (character == ' ') || (character == '\t') || (character == '\r') || (character == '\n');
}

bool matches(const char *text, size_t length, const char *literal) {
  const size_t literalLength = std::strlen(literal);
  return (length == literalLength) && (std::memcmp(text, literal, length) == 0);
}

/** Copy a bounded string, rejecting a value that does not fit. */
template <size_t N>
bool copyBounded(char (&destination)[N], const char *source) {
  if (source == nullptr) {
    return false;
  }
  const size_t length = std::strlen(source);
  if (length >= N) {
    return false;
  }
  std::memcpy(destination, source, length + 1);
  return true;
}

/** Same scheme rule the HTTPS adapter enforces, checked before any fetch. */
bool isHTTPSUrl(const char *url) {
  if (url == nullptr) {
    return false;
  }
  for (size_t i = 0; i < (sizeof(HTTPS_PREFIX) - 1); i++) {
    char actual = url[i];
    if (actual == '\0') {
      return false;
    }
    if ((actual >= 'A') && (actual <= 'Z')) {
      actual = static_cast<char>(actual - 'A' + 'a');
    }
    if (actual != HTTPS_PREFIX[i]) {
      return false;
    }
  }
  const char *authority = url + (sizeof(HTTPS_PREFIX) - 1);
  if ((authority[0] == '\0') || (authority[0] == '/') || (authority[0] == '?')
      || (authority[0] == '#')) {
    return false;
  }
  for (const char *cursor = url; *cursor != '\0'; cursor++) {
    if (isSpace(*cursor)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool parseRequest(const char *payload, size_t length, Request &request) {
  request = Request {};

  if ((payload == nullptr) || (length == 0) || (length > MAX_PAYLOAD_LENGTH)) {
    return false;
  }
  if (std::memchr(payload, '\0', length) != nullptr) {
    return false;
  }

  size_t begin = 0;
  size_t end = length;
  while ((begin < end) && isSpace(payload[begin])) {
    begin++;
  }
  while ((end > begin) && isSpace(payload[end - 1])) {
    end--;
  }
  if (begin == end) {
    return false;
  }

  size_t tokenEnd = begin;
  while ((tokenEnd < end) && !isSpace(payload[tokenEnd])) {
    tokenEnd++;
  }
  const char *token = payload + begin;
  const size_t tokenLength = tokenEnd - begin;

  size_t restBegin = tokenEnd;
  while ((restBegin < end) && isSpace(payload[restBegin])) {
    restBegin++;
  }
  const bool haveRest = restBegin < end;

  if (matches(token, tokenLength, "status") || matches(token, tokenLength, "abort")) {
    if (haveRest) {
      return false;
    }
    request.action = matches(token, tokenLength, "status") ? Action::Status : Action::Abort;
    return true;
  }

  if (matches(token, tokenLength, "check")) {
    request.action = Action::Check;
    if (!haveRest) {
      return true;
    }
    const size_t urlLength = end - restBegin;
    if (urlLength >= sizeof(request.url)) {
      return false;
    }
    std::memcpy(request.url, payload + restBegin, urlLength);
    request.url[urlLength] = '\0';
    // A URL cannot contain whitespace, so a second word is not a URL.
    return isHTTPSUrl(request.url);
  }

  if (haveRest || (tokenLength >= sizeof(request.url))) {
    return false;
  }
  std::memcpy(request.url, token, tokenLength);
  request.url[tokenLength] = '\0';
  if (!isHTTPSUrl(request.url)) {
    return false;
  }
  request.action = Action::Update;
  return true;
}

Handler::Handler(Engine &engine, Source &source, Context &context, Detail *detail)
    : m_Engine(engine), m_Source(source), m_Context(context), m_Detail(detail) {}

Handler::Result Handler::onMessage(const Meta &meta, const char *payload, size_t length) {
  Request request;
  if (!parseRequest(payload, length, request)) {
    return refuse(Refusal::InvalidSource, Action::None);
  }

  if (meta.retained) {
    return refuse(Refusal::RetainedCommand, request.action);
  }

  return handle(request, meta);
}

Handler::Result Handler::handle(const Request &request, const Meta &meta) {
  switch (request.action) {
    case Action::Status:
    {
      m_Command = Action::Status;
      return accept(Action::Status);
    }

    case Action::Abort:
    {
      m_Command = Action::Abort;
      if (m_Engine.busy()) {
        m_Engine.abort();
        m_UpdateTerminal = m_Engine.snapshot().state;
        noteProgress();
      }
      return accept(Action::Abort);
    }

    case Action::Check:
    {
      m_Command = Action::Check;
      const char *url = (request.url[0] != '\0') ? request.url : m_LastUrl;
      if ((url[0] == '\0') || !isHTTPSUrl(url)) {
        return refuse(Refusal::InvalidSource, Action::Check);
      }

      Conditions conditions = m_Context.conditions();
      conditions.updateBusy = m_Engine.busy();
      if (!conditions.linkReady) {
        return refuse(Refusal::LinkDown, Action::Check);
      }
      // A check downloads a small manifest, not an image, so the camera,
      // intervalometer, battery and slot gates stay closed for the update.

      Source::Available available;
      if (!fetchAvailable(url, available)) {
        return refuse(Refusal::SourceUnavailable, Action::Check);
      }

      rememberUrl(url);
      if (!copyBounded(m_AvailableVersion, available.version)) {
        return refuse(Refusal::InvalidSource, Action::Check);
      }
      m_Checked = true;
      m_UpdateAvailable =
          versionIsNewer(compareVersions(m_Context.installedVersion(), available.version));
      return accept(Action::Check);
    }

    case Action::Update:
    {
      m_Command = Action::Update;
      if (!isHTTPSUrl(request.url)) {
        return refuse(Refusal::InvalidSource, Action::Update);
      }

      if (m_Engine.busy()) {
        if (std::strcmp(request.url, m_UpdateUrl) == 0) {
          return duplicate(Action::Update);
        }
        return refuse(Refusal::Busy, Action::Update);
      }

      if (std::strcmp(request.url, m_UpdateUrl) == 0) {
        // A redelivered command for an update that already succeeded must not
        // download it again. A failed attempt is retried only by a fresh
        // command, not by the broker replaying the old one.
        if (m_UpdateTerminal == State::Done) {
          return duplicate(Action::Update);
        }
        if ((m_UpdateTerminal == State::Error) && meta.duplicate) {
          return duplicate(Action::Update);
        }
      }

      Conditions conditions = m_Context.conditions();
      conditions.updateBusy = m_Engine.busy();
      const Refusal refusal = evaluateUpdate(conditions);
      if (refusal != Refusal::None) {
        return refuse(refusal, Action::Update);
      }

      Source::Available available;
      if (!fetchAvailable(request.url, available)) {
        return refuse(Refusal::SourceUnavailable, Action::Update);
      }
      if (!versionIsNewer(compareVersions(m_Context.installedVersion(), available.version))) {
        return refuse(Refusal::NoNewerVersion, Action::Update);
      }
      if (available.slotCapacity < available.imageSize) {
        return refuse(Refusal::SlotUnavailable, Action::Update);
      }

      m_ClearRetained = statusTerminal(status());
      if (std::strcmp(request.url, m_UpdateUrl) != 0) {
        // A different image cannot resume the previous transfer.
        m_ResumeOffset = 0;
      }
      rememberUrl(request.url);
      if (!copyBounded(m_UpdateUrl, request.url)
          || !copyBounded(m_AvailableVersion, available.version)) {
        m_ClearRetained = false;
        return refuse(Refusal::InvalidSource, Action::Update);
      }
      m_Checked = true;
      m_UpdateAvailable = true;
      m_UpdateTerminal = State::Idle;

      if (!m_Engine.begin(request.url, m_ResumeOffset)) {
        m_ClearRetained = false;
        m_UpdateTerminal = State::Idle;
        return refuse(m_Engine.lastError() == Error::Busy ? Refusal::Busy : Refusal::InvalidSource,
                      Action::Update);
      }

      noteProgress();
      m_Refusal = Refusal::None;
      m_Outcome = Outcome::Accepted;
      m_Revision++;
      return {true, false, Refusal::None};
    }

    case Action::None:
      break;
  }

  return refuse(Refusal::InvalidSource, Action::None);
}

bool Handler::step(void) {
  if (!m_Engine.busy()) {
    return false;
  }

  const bool advanced = m_Engine.step();
  noteProgress();
  return advanced;
}

Status Handler::status(void) const {
  const Snapshot &snapshot = m_Engine.snapshot();

  Status status;
  status.state = snapshot.state;
  status.error = snapshot.error;
  status.progress = snapshot.progress;
  status.bytesDownloaded = snapshot.bytesDownloaded;
  status.totalBytes = snapshot.totalBytes;
  status.reason = (m_Outcome == Outcome::Refused) ? m_Refusal : Refusal::None;
  status.command = m_Command;
  status.outcome = m_Outcome;
  status.updateAvailable = m_UpdateAvailable;
  status.checked = m_Checked;
  status.busy = m_Engine.busy();
  copyVersion(status.installedVersion, m_Context.installedVersion());
  copyVersion(status.availableVersion, m_AvailableVersion);
  status.boot = m_Context.bootReport();

  if ((m_Outcome != Outcome::Refused) && (status.error != Error::None) && (m_Detail != nullptr)) {
    const char *detail = m_Detail->detail();
    if (detail != nullptr) {
      copyBounded(status.detail, detail);
    }
  }

  return status;
}

bool Handler::statusRetained(void) const {
  const Status current = status();
  if (statusTerminal(current)) {
    return true;
  }
  return (current.state == State::Checking) && m_ClearRetained;
}

uint32_t Handler::revision(void) const {
  return m_Revision;
}

Handler::Result Handler::refuse(Refusal refusal, Action action) {
  m_Command = action;
  m_Outcome = Outcome::Refused;
  m_Refusal = refusal;
  m_Revision++;
  return {false, false, refusal};
}

Handler::Result Handler::duplicate(Action action) {
  m_Command = action;
  m_Outcome = Outcome::Duplicate;
  m_Refusal = Refusal::None;
  m_Revision++;
  return {false, true, Refusal::None};
}

Handler::Result Handler::accept(Action action) {
  m_Command = action;
  m_Outcome = Outcome::Accepted;
  m_Refusal = Refusal::None;
  m_Revision++;
  return {true, false, Refusal::None};
}

bool Handler::fetchAvailable(const char *url, Source::Available &available) {
  available = Source::Available {};

  Source::Available fetched;
  if (!m_Source.fetch(url, fetched)) {
    return false;
  }
  if ((fetched.version[0] == '\0') || (fetched.imageSize == 0)) {
    return false;
  }
  if (std::memchr(fetched.version, '\0', sizeof(fetched.version)) == nullptr) {
    return false;
  }
  available = fetched;
  return true;
}

void Handler::rememberUrl(const char *url) {
  copyBounded(m_LastUrl, url);
}

void Handler::copyVersion(char (&destination)[STATUS_VERSION_BYTES], const char *source) const {
  if (!copyBounded(destination, source)) {
    destination[0] = '\0';
  }
}

void Handler::noteProgress(void) {
  const Snapshot &snapshot = m_Engine.snapshot();
  if ((snapshot.state != m_LastState) || (snapshot.progress != m_LastProgress)
      || (snapshot.bytesDownloaded != m_LastBytes)) {
    m_LastState = snapshot.state;
    m_LastProgress = snapshot.progress;
    m_LastBytes = snapshot.bytesDownloaded;
    m_Revision++;
  }

  if (snapshot.state != State::Checking) {
    m_ClearRetained = false;
  }
  if (!m_Engine.busy()) {
    m_UpdateTerminal = snapshot.state;
    m_ClearRetained = false;
    // Only a transfer that confirmed bytes is resumable. A verification or
    // apply failure leaves nothing worth keeping, so its checkpoint is zero.
    const bool resumable =
        (snapshot.state == State::Error) && (snapshot.error == Error::DownloadFailed);
    m_ResumeOffset = resumable ? snapshot.bytesDownloaded : 0;
  }
}

}  // namespace Command
}  // namespace OTA
}  // namespace Furble
