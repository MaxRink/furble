#include "FurbleOTAHTTPS.h"

#include <cstring>

namespace Furble {
namespace OTA {
namespace HTTPS {

namespace {

constexpr char HTTPS_SCHEME[] = "https://";

/**
 * Accept only an absolute https URL with a non-empty authority.
 *
 * The scheme is compared without case, because RFC 3986 makes it case
 * insensitive, while the rest of the URL is left untouched. A userinfo, port
 * or path is allowed; an empty host is not, because the sentence cuts it here.
 */
bool isHTTPSUrl(const char *url) {
  if (url == nullptr) {
    return false;
  }

  const size_t schemeLength = sizeof(HTTPS_SCHEME) - 1;
  for (size_t i = 0; i < schemeLength; i++) {
    const char expected = HTTPS_SCHEME[i];
    const char actual = url[i];
    if (actual == '\0') {
      return false;
    }
    const char lowered =
        ((actual >= 'A') && (actual <= 'Z')) ? static_cast<char>(actual - 'A' + 'a') : actual;
    if (lowered != expected) {
      return false;
    }
  }

  const char *authority = url + schemeLength;
  if (authority[0] == '\0') {
    return false;
  }

  size_t hostLength = 0;
  for (const char *cursor = authority; *cursor != '\0'; cursor++) {
    if ((*cursor == '/') || (*cursor == '?') || (*cursor == '#')) {
      break;
    }
    hostLength++;
  }
  if (hostLength == 0) {
    return false;
  }

  // Reject a query or fragment before the authority, which is not a URL.
  return (authority[0] != '?') && (authority[0] != '#');
}

}  // namespace

const char *reasonName(Reason reason) {
  switch (reason) {
    case Reason::None:
      return "none";
    case Reason::UrlNotHttps:
      return "url_not_https";
    case Reason::UrlMalformed:
      return "url_malformed";
    case Reason::SourceUnreachable:
      return "source_unreachable";
    case Reason::ImageSizeInvalid:
      return "image_size_invalid";
    case Reason::SlotTooSmall:
      return "slot_too_small";
    case Reason::TransferInterrupted:
      return "transfer_interrupted";
    case Reason::IncompleteTransfer:
      return "incomplete_transfer";
    case Reason::SizeMismatch:
      return "size_mismatch";
    case Reason::DigestMismatch:
      return "digest_mismatch";
    case Reason::SignatureInvalid:
      return "signature_invalid";
    case Reason::ImageInvalid:
      return "image_invalid";
    case Reason::ApplyFailed:
      return "apply_failed";
    case Reason::Aborted:
      return "aborted";
  }
  return "unknown";
}

Adapter::Adapter(Client &client) : m_Client(client) {}

bool Adapter::check(const char *url, size_t resumeOffset, size_t &totalBytes) {
  m_Reason = Reason::None;
  m_Aborted = false;
  m_Open = false;
  m_TotalBytes = 0;
  m_BytesRetrieved = 0;
  m_ResumeOffset = resumeOffset;

  if (!isHTTPSUrl(url)) {
    totalBytes = 0;
    return fail(Reason::UrlNotHttps);
  }

  Client::Open opened;
  if (!m_Client.open(url, resumeOffset, opened)) {
    totalBytes = 0;
    return fail(Reason::SourceUnreachable);
  }

  if ((opened.imageSize == 0) || (opened.imageSize > MAX_IMAGE_BYTES)) {
    totalBytes = 0;
    m_Client.discard();
    return fail(Reason::ImageSizeInvalid);
  }

  if (opened.slotCapacity < opened.imageSize) {
    totalBytes = 0;
    m_Client.discard();
    return fail(Reason::SlotTooSmall);
  }

  m_Open = true;
  m_TotalBytes = opened.imageSize;
  m_BytesRetrieved = resumeOffset;
  totalBytes = opened.imageSize;
  return true;
}

DownloadProgress Adapter::download() {
  DownloadProgress progress;
  progress.totalBytes = m_TotalBytes;
  progress.bytesDownloaded = m_BytesRetrieved;

  if (!m_Open || m_Aborted) {
    progress.result = DownloadResult::Failed;
    if (m_Reason == Reason::None) {
      m_Reason = m_Aborted ? Reason::Aborted : Reason::TransferInterrupted;
    }
    return progress;
  }

  const Client::Poll poll = m_Client.poll();
  if (poll.failed) {
    progress.result = DownloadResult::Failed;
    fail(Reason::TransferInterrupted);
    return progress;
  }

  if (poll.bytesRetrieved < m_BytesRetrieved) {
    progress.result = DownloadResult::Failed;
    fail(Reason::TransferInterrupted);
    return progress;
  }

  if (poll.bytesRetrieved > m_TotalBytes) {
    progress.result = DownloadResult::Failed;
    fail(Reason::SizeMismatch);
    return progress;
  }

  m_BytesRetrieved = poll.bytesRetrieved;
  progress.bytesDownloaded = m_BytesRetrieved;
  progress.totalBytes = m_TotalBytes;

  if (poll.complete) {
    if (m_BytesRetrieved != m_TotalBytes) {
      progress.result = DownloadResult::Failed;
      fail(Reason::IncompleteTransfer);
      return progress;
    }
    progress.result = DownloadResult::Complete;
    return progress;
  }

  progress.result = DownloadResult::InProgress;
  return progress;
}

bool Adapter::verify() {
  if (!m_Open || m_Aborted) {
    return fail(m_Aborted ? Reason::Aborted : Reason::TransferInterrupted);
  }

  switch (m_Client.finish()) {
    case Client::Verify::Ok:
      return true;
    case Client::Verify::SizeMismatch:
      return fail(Reason::SizeMismatch);
    case Client::Verify::DigestMismatch:
      return fail(Reason::DigestMismatch);
    case Client::Verify::SignatureInvalid:
      return fail(Reason::SignatureInvalid);
    case Client::Verify::ImageInvalid:
      return fail(Reason::ImageInvalid);
  }
  return fail(Reason::ImageInvalid);
}

bool Adapter::apply() {
  if (!m_Open || m_Aborted) {
    return fail(m_Aborted ? Reason::Aborted : Reason::TransferInterrupted);
  }

  if (!m_Client.apply()) {
    return fail(Reason::ApplyFailed);
  }

  m_Open = false;
  return true;
}

void Adapter::abort() {
  if (m_Aborted) {
    return;
  }

  if (m_Open) {
    m_Client.discard();
  }
  m_Open = false;
  m_Aborted = true;
  if (m_Reason == Reason::None) {
    m_Reason = Reason::Aborted;
  }
}

Reason Adapter::reason(void) const {
  return m_Reason;
}

bool Adapter::fail(Reason reason) {
  m_Reason = reason;
  return false;
}

}  // namespace HTTPS
}  // namespace OTA
}  // namespace Furble
