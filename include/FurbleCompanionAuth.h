#ifndef FURBLE_COMPANION_AUTH_H
#define FURBLE_COMPANION_AUTH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Furble {

/** Connection-local shared-password challenge state. */
class CompanionAuth {
 public:
  static constexpr size_t NONCE_SIZE = 16;
  static constexpr size_t HMAC_SIZE = 32;
  static constexpr size_t RESPONSE_SIZE = 16;
  static constexpr size_t PASSWORD_MAX = 63;
  static constexpr uint8_t MAX_FAILURES = 3;

  enum class state_t : uint8_t {
    UNAUTHENTICATED,
    CHALLENGED,
    AUTHENTICATED,
    DROPPED,
  };

  enum class response_t : uint8_t {
    REJECTED,
    AUTHENTICATED,
    DROPPED,
    NOT_REQUIRED,
  };

  using hmac_fn_t = bool (*)(const uint8_t *key,
                             size_t keyLen,
                             const uint8_t *message,
                             size_t messageLen,
                             uint8_t *digest,
                             size_t digestLen);
  using nonce_fn_t = bool (*)(uint8_t *nonce, size_t len);

  CompanionAuth(hmac_fn_t hmac, nonce_fn_t nonceGenerator);
  ~CompanionAuth();

  /** Replace the connection password. Input length is measured in UTF-8 bytes. */
  bool setPassword(const std::string &password);
  void onConnected(void);
  void onDisconnected(void);

  /** Start a challenge. Returns false when no challenge is required or possible. */
  bool begin(std::array<uint8_t, NONCE_SIZE> &nonce);

  /** Verify the truncated HMAC response for the single outstanding challenge. */
  response_t respond(const uint8_t *response, size_t len);

  bool isAuthenticated(void) const;
  bool allowsProtected(void) const;
  bool isPasswordSet(void) const;
  bool hasChallenge(void) const;
  bool isDropped(void) const;
  uint8_t failureCount(void) const;
  state_t state(void) const;

 private:
  static bool constantTimeEqual(const uint8_t *left, const uint8_t *right, size_t len);
  static void secureZero(void *data, size_t len);
  void clearChallenge(void);

  std::array<uint8_t, PASSWORD_MAX + 1> m_Password = {};
  size_t m_PasswordLen = 0;
  hmac_fn_t m_Hmac;
  nonce_fn_t m_NonceGenerator;
  std::array<uint8_t, NONCE_SIZE> m_Nonce = {};
  bool m_HaveNonce = false;
  state_t m_State = state_t::UNAUTHENTICATED;
  uint8_t m_Failures = 0;
};

/** Firmware HMAC adapter. The state machine does not depend on mbedTLS. */
bool companionHmacSha256(const uint8_t *key,
                         size_t keyLen,
                         const uint8_t *message,
                         size_t messageLen,
                         uint8_t *digest,
                         size_t digestLen);

}  // namespace Furble

#endif
