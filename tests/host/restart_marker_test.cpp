#include <cassert>
#include <cstdint>
#include <map>

#include "FurbleRestartMarker.h"

using Furble::RestartMarker;
using Furble::RestartMarkerStorage;

class FaultStorage final: public RestartMarkerStorage {
 public:
  int failAt = -1;
  int calls = 0;
  std::map<const char *, uint32_t> values;

  bool trip(void) { return failAt >= 0 && calls++ == failAt; }
  bool read(const char *key, uint32_t &value) override {
    if (trip())
      return false;
    auto it = values.find(key);
    if (it == values.end())
      return false;
    value = it->second;
    return true;
  }
  bool write(const char *key, uint32_t value) override {
    const bool fail = trip();
    values[key] = value;  // A reset can occur after the NVS write commits.
    return !fail;
  }
  bool remove(const char *key) override {
    const bool fail = trip();
    values.erase(key);
    return !fail;
  }
  bool exists(const char *key) override {
    if (trip())
      return false;
    return values.find(key) != values.end();
  }
};

int main() {
  FaultStorage storage;
  assert(RestartMarker::mark(storage));
  assert(RestartMarker::consume(storage));
  assert(!RestartMarker::consume(storage));

  // Every write/read boundary in arming is fail-closed after the simulated
  // reset. A subsequent boot may advance the generation, but cannot use the
  // half-written token as a clean restart.
  for (int boundary = 0; boundary < 8; ++boundary) {
    FaultStorage fault;
    fault.failAt = boundary;
    const bool armed = RestartMarker::mark(fault);
    if (!armed) {
      fault.failAt = -1;
      assert(!RestartMarker::consume(fault));
    }
  }

  return 0;
}
