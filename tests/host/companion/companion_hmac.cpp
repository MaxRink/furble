#include "HostHmacSha256.h"

namespace Furble {

// Host stand-in for src/FurbleCompanionCrypto.cpp, which needs mbedTLS.
bool companionHmacSha256(const uint8_t *key,
                         size_t keyLen,
                         const uint8_t *message,
                         size_t messageLen,
                         uint8_t *digest,
                         size_t digestLen) {
  return FurbleHostCrypto::hmacSha256(key, keyLen, message, messageLen, digest, digestLen);
}

}  // namespace Furble
