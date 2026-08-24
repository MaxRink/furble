#include "FurbleOTA.h"

#include <cstring>

namespace Furble {
namespace OTA {

namespace {

uint8_t percentage(size_t bytesDownloaded, size_t totalBytes) {
  if (totalBytes == 0) {
    return 0;
  }

  // Keep the multiplication bounded even if a future transport reports a
  // very large image size.
  const size_t whole = bytesDownloaded / totalBytes;
  const size_t remainder = bytesDownloaded % totalBytes;
  const size_t value = (whole * 100) + ((remainder * 100) / totalBytes);
  return static_cast<uint8_t>(value > 100 ? 100 : value);
}

}  // namespace

Engine::Engine(Transport &transport) : m_Transport(transport) {}

bool Engine::begin(const char *url, size_t resumeOffset) {
  if (busy()) {
    m_LastError = Error::Busy;
    return false;
  }

  if ((url == nullptr) || (url[0] == '\0')) {
    m_Snapshot = {};
    m_Snapshot.state = State::Error;
    m_Snapshot.error = Error::InvalidRequest;
    m_LastError = Error::InvalidRequest;
    return false;
  }

  const size_t length = std::strlen(url);
  if (length >= MAX_URL_LENGTH) {
    m_Snapshot = {};
    m_Snapshot.state = State::Error;
    m_Snapshot.error = Error::InvalidRequest;
    m_LastError = Error::InvalidRequest;
    return false;
  }

  std::memcpy(m_Url, url, length + 1);
  m_Snapshot = {};
  m_Snapshot.state = State::Checking;
  m_Snapshot.resumeOffset = resumeOffset;
  m_Snapshot.bytesDownloaded = resumeOffset;
  m_LastError = Error::None;
  return true;
}

bool Engine::step() {
  switch (m_Snapshot.state) {
    case State::Checking:
    {
      size_t totalBytes = 0;
      if (!m_Transport.check(m_Url, m_Snapshot.resumeOffset, totalBytes)) {
        return fail(Error::CheckFailed);
      }

      if ((totalBytes != 0) && (m_Snapshot.resumeOffset > totalBytes)) {
        return fail(Error::InvalidResume);
      }

      m_Snapshot.totalBytes = totalBytes;
      updateProgress(m_Snapshot.resumeOffset, totalBytes);
      if ((totalBytes != 0) && (m_Snapshot.resumeOffset == totalBytes)) {
        m_Snapshot.progress = 100;
        m_Snapshot.state = State::Verifying;
      } else {
        m_Snapshot.state = State::Downloading;
      }
      return true;
    }

    case State::Downloading:
    {
      const DownloadProgress progress = m_Transport.download();
      size_t totalBytes = m_Snapshot.totalBytes;
      if (progress.totalBytes != 0) {
        if ((totalBytes != 0) && (progress.totalBytes != totalBytes)) {
          return fail(Error::DownloadFailed);
        }
        totalBytes = progress.totalBytes;
      }

      const size_t bytesDownloaded = progress.bytesDownloaded < m_Snapshot.bytesDownloaded
                                         ? m_Snapshot.bytesDownloaded
                                         : progress.bytesDownloaded;
      if ((progress.result != DownloadResult::Failed)
          && (progress.bytesDownloaded < m_Snapshot.bytesDownloaded)) {
        return fail(Error::DownloadFailed);
      }
      if ((totalBytes != 0) && (bytesDownloaded > totalBytes)) {
        return fail(Error::DownloadFailed);
      }

      updateProgress(bytesDownloaded, totalBytes);
      if (progress.result == DownloadResult::Failed) {
        return fail(Error::DownloadFailed);
      }

      if (progress.result == DownloadResult::Complete) {
        if ((totalBytes != 0) && (bytesDownloaded != totalBytes)) {
          return fail(Error::DownloadFailed);
        }
        m_Snapshot.progress = 100;
        m_Snapshot.state = State::Verifying;
      }
      return true;
    }

    case State::Verifying:
      if (!m_Transport.verify()) {
        return fail(Error::VerificationFailed);
      }
      m_Snapshot.state = State::Applying;
      return true;

    case State::Applying:
      if (!m_Transport.apply()) {
        return fail(Error::ApplyFailed);
      }
      m_Snapshot.progress = 100;
      m_Snapshot.state = State::Done;
      m_LastError = Error::None;
      return true;

    case State::Idle:
    case State::Done:
    case State::Error:
      return false;
  }

  return false;
}

void Engine::abort() {
  if (!busy()) {
    return;
  }

  m_Transport.abort();
  m_Snapshot.state = State::Error;
  m_Snapshot.error = Error::Aborted;
  m_LastError = Error::Aborted;
}

bool Engine::busy() const {
  return (m_Snapshot.state == State::Checking) || (m_Snapshot.state == State::Downloading)
         || (m_Snapshot.state == State::Verifying) || (m_Snapshot.state == State::Applying);
}

const Snapshot &Engine::snapshot() const {
  return m_Snapshot;
}

Error Engine::lastError() const {
  return m_LastError;
}

bool Engine::fail(Error error) {
  m_Transport.abort();
  m_Snapshot.state = State::Error;
  m_Snapshot.error = error;
  m_LastError = error;
  return false;
}

void Engine::updateProgress(size_t bytesDownloaded, size_t totalBytes) {
  m_Snapshot.bytesDownloaded = bytesDownloaded;
  if (totalBytes != 0) {
    m_Snapshot.totalBytes = totalBytes;
    const uint8_t next = percentage(bytesDownloaded, totalBytes);
    if (next > m_Snapshot.progress) {
      m_Snapshot.progress = next;
    }
  }
}

const char *stateName(State state) {
  switch (state) {
    case State::Idle:
      return "idle";
    case State::Checking:
      return "checking";
    case State::Downloading:
      return "downloading";
    case State::Verifying:
      return "verifying";
    case State::Applying:
      return "applying";
    case State::Done:
      return "done";
    case State::Error:
      return "error";
  }
  return "unknown";
}

const char *errorName(Error error) {
  switch (error) {
    case Error::None:
      return "none";
    case Error::Busy:
      return "busy";
    case Error::InvalidRequest:
      return "invalid_request";
    case Error::InvalidResume:
      return "invalid_resume";
    case Error::CheckFailed:
      return "check_failed";
    case Error::DownloadFailed:
      return "download_failed";
    case Error::VerificationFailed:
      return "verification_failed";
    case Error::ApplyFailed:
      return "apply_failed";
    case Error::Aborted:
      return "aborted";
  }
  return "unknown";
}

}  // namespace OTA
}  // namespace Furble
