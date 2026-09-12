#pragma once
#include <cstddef>
namespace Furble {
class Preferences {
 public:
  bool begin(const char *, bool = false, const char * = nullptr) { return true; }
  void end() {}
  bool isKey(const char *) { return false; }
  size_t getBytesLength(const char *) { return 0; }
  size_t get(const char *, void *, size_t) { return 0; }
  template <typename T>
  T get(const char *, T value) {
    return value;
  }
  size_t put(const char *, const void *, size_t len) { return len; }
  template <typename T>
  size_t put(const char *, T value) {
    return sizeof(value);
  }
  bool remove(const char *) { return true; }
};
}  // namespace Furble
