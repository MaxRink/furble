#pragma once
#include <cstddef>
namespace Furble {
/** NVS opens, so a test can prove a path does no storage work. */
inline int preferencesBegins = 0;
class Preferences {
 public:
  bool begin(const char *, bool = false, const char * = nullptr) {
    preferencesBegins++;
    return true;
  }
  void end() {}
  bool isKey(const char *) { return false; }
  size_t getBytesLength(const char *) { return 0; }
  size_t get(const char *, void *, size_t) { return 0; }
  template <typename T>
  T get(const char *, T value) {
    return value;
  }
  void setDeferredCommit(bool) {}
  bool commit() { return true; }
  size_t put(const char *, const void *, size_t len) { return len; }
  template <typename T>
  size_t put(const char *, T value) {
    return sizeof(value);
  }
  bool remove(const char *) { return true; }
};
}  // namespace Furble
