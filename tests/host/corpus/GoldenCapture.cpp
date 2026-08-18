#include "GoldenCapture.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <utility>

namespace Furble {
namespace Host {

namespace {

const std::string EMPTY;

std::string trim(const std::string &value) {
  size_t first = 0;
  while ((first < value.size()) && std::isspace(static_cast<unsigned char>(value[first]))) {
    first++;
  }

  size_t last = value.size();
  while ((last > first) && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    last--;
  }
  return value.substr(first, last - first);
}

std::vector<std::string> split(const std::string &value, char delimiter) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t end = value.find(delimiter, start);
    if (end == std::string::npos) {
      parts.push_back(value.substr(start));
      break;
    }
    parts.push_back(value.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

int hexDigit(char value) {
  if ((value >= '0') && (value <= '9')) {
    return value - '0';
  }
  if ((value >= 'a') && (value <= 'f')) {
    return value - 'a' + 10;
  }
  if ((value >= 'A') && (value <= 'F')) {
    return value - 'A' + 10;
  }
  return -1;
}

bool assignType(const std::string &name, GoldenEventType &type) {
  if (name == "advertisement") {
    type = GoldenEventType::ADVERTISEMENT;
  } else if (name == "connect") {
    type = GoldenEventType::CONNECT;
  } else if (name == "subscribe") {
    type = GoldenEventType::SUBSCRIBE;
  } else if (name == "write") {
    type = GoldenEventType::WRITE;
  } else if (name == "notify") {
    type = GoldenEventType::NOTIFY;
  } else if (name == "disconnect") {
    type = GoldenEventType::DISCONNECT;
  } else {
    return false;
  }
  return true;
}

}  // namespace

const std::string &GoldenCapture::value(const std::string &key) const {
  const auto found = metadata.find(key);
  return (found == metadata.end()) ? EMPTY : found->second;
}

bool decodeHex(const std::string &value, std::vector<uint8_t> &bytes, std::string &error) {
  bytes.clear();
  if ((value.size() % 2) != 0) {
    error = "hex payload has odd length";
    return false;
  }

  bytes.reserve(value.size() / 2);
  for (size_t i = 0; i < value.size(); i += 2) {
    const int high = hexDigit(value[i]);
    const int low = hexDigit(value[i + 1]);
    if ((high < 0) || (low < 0)) {
      error = "hex payload contains a non-hex character";
      bytes.clear();
      return false;
    }
    bytes.push_back(static_cast<uint8_t>((high << 4) | low));
  }
  return true;
}

std::string encodeHex(const std::vector<uint8_t> &bytes) {
  static constexpr char HEX[] = "0123456789abcdef";
  std::string value;
  value.reserve(bytes.size() * 2);
  for (const uint8_t byte : bytes) {
    value.push_back(HEX[byte >> 4]);
    value.push_back(HEX[byte & 0x0f]);
  }
  return value;
}

bool loadGoldenCapture(const std::string &path, GoldenCapture &capture, std::string &error) {
  capture = {};
  error.clear();

  std::ifstream input(path);
  if (!input) {
    error = "cannot open capture: " + path;
    return false;
  }

  std::string line;
  size_t line_number = 0;
  bool header_seen = false;
  while (std::getline(input, line)) {
    line_number++;
    line = trim(line);
    if (line.empty() || (line[0] == '#')) {
      continue;
    }

    if (!header_seen) {
      constexpr const char *PREFIX = "FURBLE-GOLDEN-CAPTURE ";
      if (line.compare(0, std::strlen(PREFIX), PREFIX) != 0) {
        error = "line " + std::to_string(line_number) + ": missing capture header";
        return false;
      }
      try {
        capture.schema = std::stoi(line.substr(std::strlen(PREFIX)));
      } catch (...) {
        error = "line " + std::to_string(line_number) + ": invalid schema";
        return false;
      }
      if (capture.schema != 1) {
        error = "line " + std::to_string(line_number) + ": unsupported schema";
        return false;
      }
      header_seen = true;
      continue;
    }

    if (line.find('|') == std::string::npos) {
      const size_t separator = line.find('=');
      if (separator == std::string::npos) {
        error = "line " + std::to_string(line_number) + ": invalid metadata";
        return false;
      }
      const std::string key = trim(line.substr(0, separator));
      if (key.empty()) {
        error = "line " + std::to_string(line_number) + ": empty metadata key";
        return false;
      }
      capture.metadata[key] = trim(line.substr(separator + 1));
      continue;
    }

    const std::vector<std::string> parts = split(line, '|');
    if (parts.size() != 4) {
      error = "line " + std::to_string(line_number) + ": expected four event fields";
      return false;
    }

    GoldenEvent event {};
    if (!assignType(parts[0], event.type)) {
      error = "line " + std::to_string(line_number) + ": unknown event type";
      return false;
    }

    if (event.type == GoldenEventType::ADVERTISEMENT) {
      event.field = trim(parts[1]);
      event.service = trim(parts[2]);
      event.text = trim(parts[3]);
      if (event.field.empty()) {
        error = "line " + std::to_string(line_number) + ": empty advertisement field";
        return false;
      }
    } else if (event.type == GoldenEventType::CONNECT
               || event.type == GoldenEventType::DISCONNECT) {
      if (!parts[1].empty() || !parts[2].empty() || !parts[3].empty()) {
        error = "line " + std::to_string(line_number) + ": connection event has fields";
        return false;
      }
    } else {
      event.service = trim(parts[1]);
      event.characteristic = trim(parts[2]);
      if (event.service.empty() || event.characteristic.empty()) {
        error = "line " + std::to_string(line_number) + ": event UUID is missing";
        return false;
      }

      if (event.type == GoldenEventType::SUBSCRIBE) {
        event.text = trim(parts[3]);
        if ((event.text != "notification") && (event.text != "indication")) {
          error = "line " + std::to_string(line_number) + ": invalid subscription kind";
          return false;
        }
        event.indication = event.text == "indication";
      } else if (!decodeHex(trim(parts[3]), event.payload, error)) {
        error = "line " + std::to_string(line_number) + ": " + error;
        return false;
      }
    }
    capture.events.push_back(std::move(event));
  }

  if (!header_seen) {
    error = "capture is empty";
    return false;
  }
  return true;
}

}  // namespace Host
}  // namespace Furble
