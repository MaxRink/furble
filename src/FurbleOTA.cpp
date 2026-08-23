#include "FurbleOTA.h"

#include <cstring>

#if defined(ESP_PLATFORM)
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#endif

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

#if defined(ESP_PLATFORM)

/**
 * ESP-IDF delivery adapter. The lifecycle above never includes ESP-IDF types,
 * so the same state machine can be linked into a host test with no SDK.
 */
class EspHttpsOtaTransport final: public Transport {
 public:
  bool check(const char *url, size_t resumeOffset, size_t &totalBytes) override {
    abort();

    // esp_https_ota exposes HTTP range requests for one active transfer via
    // partial_http_download, but it does not expose a persisted image offset
    // after an aborted handle. The host seam still supports checkpoints, while
    // the firmware adapter reports a non-zero checkpoint as unsupported rather
    // than writing a restarted image at the wrong offset.
    if (resumeOffset != 0) {
      return false;
    }

    m_HttpConfig = {};
    m_HttpConfig.url = url;
    m_HttpConfig.crt_bundle_attach = esp_crt_bundle_attach;

    m_OtaConfig = {};
    m_OtaConfig.http_config = &m_HttpConfig;
    m_OtaConfig.partial_http_download = true;
    m_OtaConfig.max_http_request_size = 4096;

    if (esp_https_ota_begin(&m_OtaConfig, &m_Handle) != ESP_OK) {
      m_Handle = nullptr;
      return false;
    }

    const int imageSize = esp_https_ota_get_image_size(m_Handle);
    m_TotalBytes = imageSize > 0 ? static_cast<size_t>(imageSize) : 0;
    totalBytes = m_TotalBytes;
    return true;
  }

  DownloadProgress download() override {
    DownloadProgress progress;
    progress.totalBytes = m_TotalBytes;

    if (m_Handle == nullptr) {
      progress.result = DownloadResult::Failed;
      return progress;
    }

    const esp_err_t result = esp_https_ota_perform(m_Handle);
    const int imageLength = esp_https_ota_get_image_len_read(m_Handle);
    progress.bytesDownloaded = imageLength > 0 ? static_cast<size_t>(imageLength) : 0;

    if (result == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      progress.result = DownloadResult::InProgress;
      return progress;
    }

    if ((result != ESP_OK) || !esp_https_ota_is_complete_data_received(m_Handle)) {
      progress.result = DownloadResult::Failed;
      return progress;
    }

    progress.result = DownloadResult::Complete;
    return progress;
  }

  bool verify() override {
    if (m_Handle == nullptr) {
      return false;
    }

    // esp_https_ota_finish validates the image and commits the boot partition.
    // The engine keeps an explicit Applying state so a different transport can
    // separate those operations, while this adapter treats finish as the
    // device's verify-and-apply preparation step.
    const bool ok = esp_https_ota_finish(m_Handle) == ESP_OK;
    m_Handle = nullptr;
    m_Verified = ok;
    return ok;
  }

  bool apply() override {
    // esp_https_ota_finish has already selected the next boot partition. The
    // caller decides when to restart, so applying never unexpectedly resets a
    // debug console or an MQTT task.
    return m_Verified;
  }

  void abort() override {
    if (m_Handle != nullptr) {
      esp_https_ota_abort(m_Handle);
      m_Handle = nullptr;
    }
    m_TotalBytes = 0;
    m_Verified = false;
  }

 private:
  esp_https_ota_handle_t m_Handle = nullptr;
  esp_http_client_config_t m_HttpConfig = {};
  esp_https_ota_config_t m_OtaConfig = {};
  size_t m_TotalBytes = 0;
  bool m_Verified = false;
};

#else

/** Host fallback used only by getEngine() when no transport is injected. */
class UnavailableTransport final: public Transport {
 public:
  bool check(const char *, size_t, size_t &) override { return false; }

  DownloadProgress download() override {
    DownloadProgress progress;
    progress.result = DownloadResult::Failed;
    return progress;
  }

  bool verify() override { return false; }
  bool apply() override { return false; }
  void abort() override {}
};

#endif

Transport &defaultTransport() {
#if defined(ESP_PLATFORM)
  static EspHttpsOtaTransport transport;
#else
  static UnavailableTransport transport;
#endif
  return transport;
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

Engine &getEngine() {
  static Engine engine(defaultTransport());
  return engine;
}

}  // namespace OTA
}  // namespace Furble
