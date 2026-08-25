#include "FurbleOTA.h"

#include <cstring>

#if defined(ESP_PLATFORM)
#include <esp_flash_partitions.h>
#include <esp_system.h>

static_assert(ESP_RST_UNKNOWN == 0 && ESP_RST_POWERON == 1 && ESP_RST_EXT == 2 && ESP_RST_SW == 3
                  && ESP_RST_PANIC == 4 && ESP_RST_INT_WDT == 5 && ESP_RST_TASK_WDT == 6
                  && ESP_RST_WDT == 7 && ESP_RST_DEEPSLEEP == 8 && ESP_RST_BROWNOUT == 9
                  && ESP_RST_SDIO == 10 && ESP_RST_USB == 11 && ESP_RST_JTAG == 12
                  && ESP_RST_EFUSE == 13 && ESP_RST_PWR_GLITCH == 14 && ESP_RST_CPU_LOCKUP == 15,
              "Update resetReasonFromIdf() for the selected ESP-IDF");
static_assert(ESP_OTA_IMG_NEW == 0U && ESP_OTA_IMG_PENDING_VERIFY == 1U && ESP_OTA_IMG_VALID == 2U
                  && ESP_OTA_IMG_INVALID == 3U && ESP_OTA_IMG_ABORTED == 4U
                  && ESP_OTA_IMG_UNDEFINED == 0xFFFFFFFFU,
              "Update rollbackStateFromIdf() for the selected ESP-IDF");
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

}  // namespace

Engine::Engine(Transport &transport) : m_Transport(transport) {}

ResetReason resetReasonFromIdf(int reasonValue) {
  switch (reasonValue) {
    case 1:
      return ResetReason::PowerOn;
    case 2:
      return ResetReason::ExternalPin;
    case 3:
      return ResetReason::Software;
    case 4:
      return ResetReason::Panic;
    case 5:
      return ResetReason::InterruptWatchdog;
    case 6:
      return ResetReason::TaskWatchdog;
    case 7:
      return ResetReason::OtherWatchdog;
    case 8:
      return ResetReason::DeepSleep;
    case 9:
      return ResetReason::Brownout;
    case 10:
      return ResetReason::Sdio;
    case 11:
      return ResetReason::Usb;
    case 12:
      return ResetReason::Jtag;
    case 13:
      return ResetReason::Efuse;
    case 14:
      return ResetReason::PowerGlitch;
    case 15:
      return ResetReason::CpuLockup;
    case 0:
    default:
      return ResetReason::Unknown;
  }
}

RollbackState rollbackStateFromIdf(bool readSucceeded, uint32_t stateValue) {
  if (!readSucceeded) {
    return RollbackState::ReadError;
  }
  switch (stateValue) {
    case 0U:
      return RollbackState::New;
    case 1U:
      return RollbackState::PendingVerify;
    case 2U:
      return RollbackState::Valid;
    case 3U:
      return RollbackState::Invalid;
    case 4U:
      return RollbackState::Aborted;
    case UINT32_MAX:
      return RollbackState::Undefined;
    default:
      return RollbackState::ReadError;
  }
}

BootDecision evaluateBootHealth(const BootHealth &health) {
  switch (health.rollbackState) {
    case RollbackState::ReadError:
      return {health.validationDeadlineReached ? BootAction::MarkInvalidRollbackAndReboot
                                               : BootAction::WaitForHealth,
              BootFailure::StateRead};
    case RollbackState::New:
      return {health.validationDeadlineReached ? BootAction::MarkInvalidRollbackAndReboot
                                               : BootAction::WaitForHealth,
              BootFailure::UnexpectedState};
    case RollbackState::Undefined:
    case RollbackState::Valid:
    case RollbackState::Invalid:
    case RollbackState::Aborted:
      return {BootAction::NoAction, BootFailure::NotPending};
    case RollbackState::PendingVerify:
      break;
  }

  BootFailure failure = BootFailure::None;
  if (!health.nvsReady) {
    failure = BootFailure::Nvs;
  } else if (!health.platformReady) {
    failure = BootFailure::Platform;
  } else if (!health.bleReady) {
    failure = BootFailure::Ble;
  } else if (!health.controlReady) {
    failure = BootFailure::Control;
  } else if (health.serviceTicks < BootHealth::MIN_SERVICE_TICKS) {
    failure = BootFailure::ServiceLoop;
  } else if ((health.heapFloor == 0) || (health.freeHeap < health.heapFloor)) {
    failure = BootFailure::Heap;
  } else {
    switch (health.resetReason) {
      case ResetReason::PowerOn:
      case ResetReason::Software:
      case ResetReason::DeepSleep:
        break;
      case ResetReason::Unknown:
      case ResetReason::ExternalPin:
      case ResetReason::Brownout:
      case ResetReason::Panic:
      case ResetReason::TaskWatchdog:
      case ResetReason::InterruptWatchdog:
      case ResetReason::OtherWatchdog:
      case ResetReason::Sdio:
      case ResetReason::Usb:
      case ResetReason::Jtag:
      case ResetReason::Efuse:
      case ResetReason::PowerGlitch:
      case ResetReason::CpuLockup:
        failure = BootFailure::UnsafeReset;
        break;
    }
  }

  if (failure == BootFailure::None) {
    return {BootAction::MarkValid, BootFailure::None};
  }
  if (!health.validationDeadlineReached) {
    return {BootAction::WaitForHealth, failure};
  }
  return {BootAction::MarkInvalidRollbackAndReboot, failure};
}

const char *bootFailureName(BootFailure failure) {
  switch (failure) {
    case BootFailure::None:
      return "none";
    case BootFailure::NotPending:
      return "not-pending";
    case BootFailure::StateRead:
      return "state-read";
    case BootFailure::UnexpectedState:
      return "unexpected-state";
    case BootFailure::Nvs:
      return "nvs";
    case BootFailure::Platform:
      return "platform";
    case BootFailure::Ble:
      return "ble";
    case BootFailure::Control:
      return "control";
    case BootFailure::ServiceLoop:
      return "service-loop";
    case BootFailure::Heap:
      return "heap";
    case BootFailure::UnsafeReset:
      return "unsafe-reset";
  }
  return "unknown";
}

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
