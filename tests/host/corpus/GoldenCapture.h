#ifndef FURBLE_HOST_GOLDEN_CAPTURE_H
#define FURBLE_HOST_GOLDEN_CAPTURE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Furble {
namespace Host {

enum class GoldenEventType {
  ADVERTISEMENT,
  CONNECT,
  SUBSCRIBE,
  WRITE,
  NOTIFY,
  DISCONNECT,
};

struct GoldenEvent {
  GoldenEventType type;
  std::string field;
  std::string service;
  std::string characteristic;
  std::string text;
  std::vector<uint8_t> payload;
  bool indication = false;
};

struct GoldenCapture {
  int schema = 0;
  std::map<std::string, std::string> metadata;
  std::vector<GoldenEvent> events;

  const std::string &value(const std::string &key) const;
};

/** Load the normalized, line-oriented capture format documented in README.md. */
bool loadGoldenCapture(const std::string &path, GoldenCapture &capture, std::string &error);

/** Decode an even-length lower or upper case hexadecimal byte string. */
bool decodeHex(const std::string &value, std::vector<uint8_t> &bytes, std::string &error);

std::string encodeHex(const std::vector<uint8_t> &bytes);

}  // namespace Host
}  // namespace Furble

#endif
