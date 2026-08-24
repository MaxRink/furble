#include <algorithm>
#include <cstring>

#include "FurbleCompanionAuth.h"

namespace Furble {

CompanionAuth::CompanionAuth(hmac_fn_t hmac, nonce_fn_t nonceGenerator)
    : m_Hmac {hmac}, m_NonceGenerator {nonceGenerator} {}

void CompanionAuth::setPassword(const std::string &password) {
  // std::string assignment is not required to overwrite the old SSO/heap
  // bytes. Clear the previous secret before replacing it.
  std::fill(m_Password.begin(), m_Password.end(), '\0');
  m_Password.clear();
  m_Password = password;
  m_Failures = 0;
  clearChallenge();
  m_State = m_Password.empty() ? state_t::AUTHENTICATED : state_t::UNAUTHENTICATED;
}

void CompanionAuth::onConnected(void) {
  m_Failures = 0;
  clearChallenge();
  m_State = m_Password.empty() ? state_t::AUTHENTICATED : state_t::UNAUTHENTICATED;
}

void CompanionAuth::onDisconnected(void) {
  m_Failures = 0;
  clearChallenge();
  m_State = state_t::UNAUTHENTICATED;
}

bool CompanionAuth::begin(std::array<uint8_t, NONCE_SIZE> &nonce) {
  nonce.fill(0);
  if (m_Password.empty()) {
    m_State = state_t::AUTHENTICATED;
    return false;
  }
  if (m_State == state_t::DROPPED) {
    return false;
  }
  if (m_NonceGenerator == nullptr || !m_NonceGenerator(m_Nonce.data(), m_Nonce.size())) {
    clearChallenge();
    m_State = state_t::UNAUTHENTICATED;
    return false;
  }

  m_HaveNonce = true;
  m_State = state_t::CHALLENGED;
  nonce = m_Nonce;
  return true;
}

CompanionAuth::response_t CompanionAuth::respond(const uint8_t *response, size_t len) {
  if (m_Password.empty()) {
    m_State = state_t::AUTHENTICATED;
    return response_t::NOT_REQUIRED;
  }
  if (m_State == state_t::DROPPED) {
    return response_t::DROPPED;
  }
  if (!m_HaveNonce) {
    return response_t::REJECTED;
  }
  if (response == nullptr || len != RESPONSE_SIZE || m_Hmac == nullptr) {
    // A response consumes the single outstanding challenge even when its
    // framing is invalid. Otherwise a peer can retry malformed packets
    // forever against the same nonce and bypass the failure limit.
    clearChallenge();
    if (m_Failures < MAX_FAILURES) {
      m_Failures++;
    }
    m_State = (m_Failures >= MAX_FAILURES) ? state_t::DROPPED : state_t::UNAUTHENTICATED;
    return m_State == state_t::DROPPED ? response_t::DROPPED : response_t::REJECTED;
  }

  std::array<uint8_t, HMAC_SIZE> digest = {};
  const bool computed =
      m_Hmac(reinterpret_cast<const uint8_t *>(m_Password.data()), m_Password.size(),
             m_Nonce.data(), m_Nonce.size(), digest.data(), digest.size());
  const bool matches = computed && constantTimeEqual(digest.data(), response, RESPONSE_SIZE);
  clearChallenge();
  std::fill(digest.begin(), digest.end(), 0);

  if (matches) {
    m_Failures = 0;
    m_State = state_t::AUTHENTICATED;
    return response_t::AUTHENTICATED;
  }

  if (m_Failures < MAX_FAILURES) {
    m_Failures++;
  }
  m_State = (m_Failures >= MAX_FAILURES) ? state_t::DROPPED : state_t::UNAUTHENTICATED;
  return m_State == state_t::DROPPED ? response_t::DROPPED : response_t::REJECTED;
}

bool CompanionAuth::isAuthenticated(void) const {
  return m_State == state_t::AUTHENTICATED;
}

bool CompanionAuth::allowsProtected(void) const {
  return isAuthenticated();
}

bool CompanionAuth::isPasswordSet(void) const {
  return !m_Password.empty();
}

bool CompanionAuth::hasChallenge(void) const {
  return m_HaveNonce;
}

bool CompanionAuth::isDropped(void) const {
  return m_State == state_t::DROPPED;
}

uint8_t CompanionAuth::failureCount(void) const {
  return m_Failures;
}

CompanionAuth::state_t CompanionAuth::state(void) const {
  return m_State;
}

bool CompanionAuth::constantTimeEqual(const uint8_t *left, const uint8_t *right, size_t len) {
  uint8_t difference = 0;
  for (size_t index = 0; index < len; index++) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}

void CompanionAuth::clearChallenge(void) {
  std::fill(m_Nonce.begin(), m_Nonce.end(), 0);
  m_HaveNonce = false;
}

}  // namespace Furble
