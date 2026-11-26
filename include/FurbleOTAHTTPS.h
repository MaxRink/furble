#ifndef FURBLE_OTA_HTTPS_H
#define FURBLE_OTA_HTTPS_H

#include <cstddef>
#include <cstdint>

#include "FurbleOTA.h"

namespace Furble {
namespace OTA {
namespace HTTPS {

/** Specific refusal or failure detail behind the engine's coarse error. */
enum class Reason : uint8_t {
  None,
  UrlNotHttps,
  UrlMalformed,
  SourceUnreachable,
  ImageSizeInvalid,
  SlotTooSmall,
  TransferInterrupted,
  IncompleteTransfer,
  SizeMismatch,
  DigestMismatch,
  SignatureInvalid,
  ImageInvalid,
  ApplyFailed,
  Aborted,
};

/** Stable topic and console label for a reason. */
const char *reasonName(Reason reason);

/**
 * HTTP fetch and flash write seam behind the adapter.
 *
 * This is the host-testable shape of the ESP-IDF advanced HTTPS OTA call
 * sequence: open() maps to esp_https_ota_begin() plus the image and partition
 * metadata it reports, poll() maps to one esp_https_ota_perform() call,
 * finish() maps to the esp_image_verify() and digest work before
 * esp_https_ota_finish(), and apply() maps to selecting the staged slot.
 *
 * A host double scripts the whole transfer. The firmware adapter is a later,
 * hardware-gated slice: this header deliberately contains no network or flash
 * dependency, so no real fetch happens here.
 */
class Client {
 public:
  virtual ~Client() = default;

  /** Result of esp_image_verify() plus the manifest digest and signature. */
  enum class Verify : uint8_t {
    Ok,
    SizeMismatch,
    DigestMismatch,
    SignatureInvalid,
    ImageInvalid,
  };

  struct Open {
    /** Image length the server announced, zero when it announced none. */
    size_t imageSize = 0;
    /** Usable bytes in the inactive app slot. */
    size_t slotCapacity = 0;
  };

  /** One non-blocking download poll. Byte counts are absolute for the transfer. */
  struct Poll {
    bool failed = false;
    bool complete = false;
    size_t bytesRetrieved = 0;
  };

  /**
   * Open the image at url, resuming at resumeOffset when non-zero.
   *
   * Returns false for a refused connection, a non-success HTTP status or a
   * failed range request.
   */
  virtual bool open(const char *url, size_t resumeOffset, Open &result) = 0;

  /** Poll one chunk. A failed poll must leave the staged bytes discardable. */
  virtual Poll poll() = 0;

  /** Verify the complete staged image, including size, digest and signature. */
  virtual Verify finish() = 0;

  /** Select the verified image for the next boot. */
  virtual bool apply() = 0;

  /** Close the connection and discard the staged image. Must be idempotent. */
  virtual void discard() = 0;
};

/**
 * HTTPS-only delivery policy over an injected Client.
 *
 * The adapter owns everything the transport-independent engine must not know:
 * the https scheme rule, the resume offset, the inactive slot capacity check,
 * the image size bounds, and the mapping from a client failure to a stable
 * reason label. The engine still owns the lifecycle, the progress invariants
 * and the abort seam, so a client cannot skip a state.
 */
class Adapter final: public Transport {
 public:
  /** Upper bound on an accepted image, matching the MQTT OTA envelope. */
  static constexpr size_t MAX_IMAGE_BYTES = 16 * 1024 * 1024;

  explicit Adapter(Client &client);

  bool check(const char *url, size_t resumeOffset, size_t &totalBytes) override;
  DownloadProgress download() override;
  bool verify() override;
  bool apply() override;
  void abort() override;

  /** Detail for the engine's last failure, or Reason::None. */
  Reason reason(void) const;

 private:
  bool fail(Reason reason);

  Client &m_Client;
  Reason m_Reason = Reason::None;
  size_t m_TotalBytes = 0;
  size_t m_BytesRetrieved = 0;
  size_t m_ResumeOffset = 0;
  bool m_Open = false;
  bool m_Aborted = false;
};

}  // namespace HTTPS
}  // namespace OTA
}  // namespace Furble

#endif  // FURBLE_OTA_HTTPS_H
