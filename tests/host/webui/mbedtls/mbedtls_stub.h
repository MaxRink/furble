#ifndef FURBLE_HOST_WEBUI_MBEDTLS_STUB_H
#define FURBLE_HOST_WEBUI_MBEDTLS_STUB_H

#include <cstddef>
#include <cstdint>
#include <cstring>

struct mbedtls_entropy_context {};
struct mbedtls_ctr_drbg_context {};
struct mbedtls_pk_context {};
struct mbedtls_x509write_cert {};
struct mbedtls_x509_crt {
  struct {
    unsigned char *p = nullptr;
    size_t len = 0;
  } raw;
};

constexpr int MBEDTLS_PK_ECKEY = 1;
constexpr int MBEDTLS_ECP_DP_SECP256R1 = 1;
constexpr int MBEDTLS_X509_CRT_VERSION_3 = 2;
constexpr int MBEDTLS_MD_SHA256 = 1;
constexpr unsigned MBEDTLS_X509_KU_DIGITAL_SIGNATURE = 1;

inline void mbedtls_entropy_init(mbedtls_entropy_context *) {}
inline void mbedtls_entropy_free(mbedtls_entropy_context *) {}
inline int mbedtls_entropy_func(void *, unsigned char *, size_t) { return 0; }
inline void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *) {}
inline void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *) {}
inline int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *, int (*)(void *, unsigned char *, size_t),
                                 void *, const unsigned char *, size_t) { return 0; }
inline int mbedtls_ctr_drbg_random(void *, unsigned char *output, size_t length) {
  std::memset(output, 1, length);
  return 0;
}
inline void mbedtls_pk_init(mbedtls_pk_context *) {}
inline void mbedtls_pk_free(mbedtls_pk_context *) {}
inline const void *mbedtls_pk_info_from_type(int) { return nullptr; }
inline int mbedtls_pk_setup(mbedtls_pk_context *, const void *) { return 0; }
#define mbedtls_pk_ec(context) nullptr
inline int mbedtls_ecp_gen_key(int, void *, int (*)(void *, unsigned char *, size_t), void *) {
  return 0;
}
inline int mbedtls_pk_write_key_pem(mbedtls_pk_context *, unsigned char *output, size_t length) {
  if (length != 0) {
    output[0] = 0;
  }
  return 0;
}
inline void mbedtls_x509write_crt_init(mbedtls_x509write_cert *) {}
inline void mbedtls_x509write_crt_free(mbedtls_x509write_cert *) {}
inline void mbedtls_x509write_crt_set_version(mbedtls_x509write_cert *, int) {}
inline void mbedtls_x509write_crt_set_md_alg(mbedtls_x509write_cert *, int) {}
inline void mbedtls_x509write_crt_set_subject_key(mbedtls_x509write_cert *, mbedtls_pk_context *) {}
inline void mbedtls_x509write_crt_set_issuer_key(mbedtls_x509write_cert *, mbedtls_pk_context *) {}
inline int mbedtls_x509write_crt_set_subject_name(mbedtls_x509write_cert *, const char *) { return 0; }
inline int mbedtls_x509write_crt_set_issuer_name(mbedtls_x509write_cert *, const char *) { return 0; }
inline int mbedtls_x509write_crt_set_serial_raw(mbedtls_x509write_cert *, const unsigned char *, size_t) { return 0; }
inline int mbedtls_x509write_crt_set_validity(mbedtls_x509write_cert *, const char *, const char *) { return 0; }
inline int mbedtls_x509write_crt_set_basic_constraints(mbedtls_x509write_cert *, int, int) { return 0; }
inline int mbedtls_x509write_crt_set_key_usage(mbedtls_x509write_cert *, unsigned) { return 0; }
inline int mbedtls_x509write_crt_pem(mbedtls_x509write_cert *, unsigned char *output, size_t length,
                                     int (*)(void *, unsigned char *, size_t), void *) {
  if (length != 0) {
    output[0] = 0;
  }
  return 0;
}
inline void mbedtls_x509_crt_init(mbedtls_x509_crt *) {}
inline void mbedtls_x509_crt_free(mbedtls_x509_crt *) {}
inline int mbedtls_x509_crt_parse(mbedtls_x509_crt *, const unsigned char *, size_t) { return -1; }
inline int mbedtls_sha256(const unsigned char *, size_t, unsigned char output[32], int) {
  std::memset(output, 0, 32);
  return 0;
}

inline int mbedtls_base64_decode(unsigned char *output, size_t outputSize, size_t *outputLength,
                                 const unsigned char *input, size_t inputLength) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t produced = 0;
  unsigned accumulator = 0;
  unsigned bits = 0;
  for (size_t index = 0; index < inputLength; index++) {
    if (input[index] == '=') break;
    const char *found = std::strchr(alphabet, input[index]);
    if (found == nullptr) return -1;
    accumulator = (accumulator << 6) | static_cast<unsigned>(found - alphabet);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (produced >= outputSize) return -1;
      output[produced++] = static_cast<unsigned char>((accumulator >> bits) & 0xff);
    }
  }
  *outputLength = produced;
  return 0;
}

#endif
