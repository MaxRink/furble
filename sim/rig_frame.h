#ifndef FURBLE_SIM_RIG_FRAME_H
#define FURBLE_SIM_RIG_FRAME_H

#include <cstdint>

namespace Furble::Rig {

enum op_t : uint8_t {
  WRITE = 0x01,
  WRITE_NO_RSP = 0x02,
  WRITE_RESPONSE = 0x03,
  READ_REQUEST = 0x04,
  READ_RESPONSE = 0x05,
  NOTIFY = 0x06,
  INDICATE = 0x07,
  INDICATE_CONFIRM = 0x08,
  SUBSCRIBE = 0x09,
  ERROR = 0x0a,
  HELLO = 0x0b,
  HELLO_ACK = 0x0c,
  PAIR_REQUEST = 0x0d,
  PAIR_CONFIRM = 0x0e,
};

enum char_t : uint8_t {
  CHAR_NONE = 0x00,
  CHAR_LOCATION = 0x02,
  CHAR_STATUS = 0x03,
  CHAR_SETTINGS = 0x04,
  CHAR_TRIGGER = 0x05,
  CHAR_OTA_CONTROL = 0x10,
  CHAR_OTA_DATA = 0x11,
};

struct __attribute__((packed)) header_t {
  uint8_t magic0;
  uint8_t magic1;
  uint8_t op;
  uint8_t char_id;
  uint16_t length;
};

struct __attribute__((packed)) hello_t {
  uint8_t rig_version;
  uint8_t wire_version;
  uint8_t role;
  uint8_t reserved;
  uint8_t service_uuid[16];
  uint16_t max_payload;
};

static_assert(sizeof(header_t) == 6, "rig frame header must be six bytes");
static_assert(sizeof(hello_t) == 22, "rig hello must be 22 bytes");

using rig_op_t = op_t;
using rig_char_t = char_t;
using rig_header_t = header_t;
using rig_hello_t = hello_t;

constexpr uint8_t MAGIC0 = 'F';
constexpr uint8_t MAGIC1 = 'R';
constexpr uint8_t RIG_VERSION = 1;
constexpr uint16_t DEFAULT_MAX_PAYLOAD = 244;
constexpr uint16_t MAX_FRAME_PAYLOAD = 1024;

}  // namespace Furble::Rig

#endif
