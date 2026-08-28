#include "Preferences.h"

namespace Furble {
Preferences::Preferences() : _handle(0), _started(false), _readOnly(false) {}
Preferences::~Preferences() = default;
bool Preferences::begin(const char *, bool, const char *) {
  return true;
}
void Preferences::end() {}
bool Preferences::clear() {
  return true;
}
bool Preferences::remove(const char *) {
  return true;
}
size_t Preferences::put(const char *, const void *, size_t bytes) {
  return bytes;
}
size_t Preferences::put(const char *, const char *) {
  return 0;
}
size_t Preferences::get(const char *, void *, size_t) {
  return 0;
}
bool Preferences::isKey(const char *) {
  return false;
}
Preferences::status Preferences::readU32(const char *, uint32_t &) {
  return status::NOT_FOUND;
}
Preferences::status Preferences::removeKey(const char *) {
  return status::NOT_FOUND;
}
size_t Preferences::getBytesLength(const char *) {
  return 0;
}
size_t Preferences::freeEntries() {
  return 0;
}
}  // namespace Furble
