#include <mbedtls/md.h>

#include "FurbleCompanionAuth.h"

namespace Furble {

bool companionHmacSha256(const uint8_t *key,
                         size_t keyLen,
                         const uint8_t *message,
                         size_t messageLen,
                         uint8_t *digest,
                         size_t digestLen) {
  if (digest == nullptr || digestLen < CompanionAuth::HMAC_SIZE || (key == nullptr && keyLen != 0)
      || (message == nullptr && messageLen != 0)) {
    return false;
  }

  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return (info != nullptr)
         && (mbedtls_md_hmac(info, key, keyLen, message, messageLen, digest) == 0);
}

}  // namespace Furble
