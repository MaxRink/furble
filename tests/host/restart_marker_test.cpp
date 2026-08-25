#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

#include "FurbleRestartMarker.h"

using Storage = Furble::RestartMarkerStorage;

class FaultStorage final: public Storage {
 public:
  std::map<std::string, uint32_t> values;
  int failAt = -1;
  int calls = 0;

  bool fail(void) { return failAt >= 0 && calls++ == failAt; }
  result read(const char *key, uint32_t &value) override {
    if (fail())
      return result::ERROR;
    const auto it = values.find(key);
    if (it == values.end())
      return result::ABSENT;
    value = it->second;
    return result::PRESENT;
  }
  result write(const char *key, uint32_t value) override {
    const bool error = fail();
    values[key] = value;
    return error ? result::ERROR : result::SUCCESS;
  }
  result remove(const char *key) override {
    const bool error = fail();
    values.erase(key);
    return error ? result::ERROR : result::SUCCESS;
  }
  result exists(const char *key) override {
    if (fail())
      return result::ERROR;
    return values.count(key) != 0 ? result::PRESENT : result::ABSENT;
  }
};

static FaultStorage armedStorage(void) {
  FaultStorage storage;
  assert(Furble::RestartMarker::mark(storage));
  storage.calls = 0;
  return storage;
}

int main() {
  FaultStorage storage = armedStorage();
  assert(Furble::RestartMarker::consume(storage));
  storage.calls = 0;
  assert(!Furble::RestartMarker::consume(storage));

  // With an empty generation, mark has four operations before it can return
  // success: pending write/readback and commit write/readback.
  for (int boundary = 0; boundary < 4; ++boundary) {
    FaultStorage fault;
    fault.failAt = boundary;
    assert(!Furble::RestartMarker::mark(fault));
    fault.failAt = -1;
    fault.calls = 0;
    if (Furble::RestartMarker::consume(fault)) {
      std::fprintf(stderr, "unexpected second fast boundary %d\\n", boundary);
      return 1;
    }
  }

  // Fault every consume operation after a successful mark, including the
  // reviewer boundaries 0, 6 and 8, then simulate the next boot.
  for (int boundary = 0; boundary < 9; ++boundary) {
    FaultStorage fault = armedStorage();
    fault.failAt = boundary;
    if (Furble::RestartMarker::consume(fault)) {
      std::fprintf(stderr, "unexpected second consume boundary %d\\n", boundary);
      return 1;
    }
    fault.failAt = -1;
    fault.calls = 0;
    if (Furble::RestartMarker::consume(fault)) {
      std::fprintf(stderr, "unexpected reboot fast boundary %d\\n", boundary);
      return 1;
    }
  }

  // Regression for the poison-recovery resurrection: b1 is the injected
  // failure, b2 is the poison recovery boot, and b3 must remain false.
  FaultStorage regression = armedStorage();
  regression.failAt = 0;
  assert(!Furble::RestartMarker::consume(regression));  // b1
  regression.failAt = -1;
  regression.calls = 0;
  assert(!Furble::RestartMarker::consume(regression));  // b2
  regression.calls = 0;
  assert(!Furble::RestartMarker::consume(regression));  // b3

  // If the poison write itself fails, every later boot still advances the
  // generation and cannot resurrect the old marker.
  FaultStorage poisonWrite = armedStorage();
  poisonWrite.failAt = 4;
  assert(!Furble::RestartMarker::consume(poisonWrite));
  poisonWrite.failAt = -1;
  poisonWrite.calls = 0;
  assert(!Furble::RestartMarker::consume(poisonWrite));
  poisonWrite.calls = 0;
  assert(!Furble::RestartMarker::consume(poisonWrite));
  return 0;
}
