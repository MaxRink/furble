#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "Preferences.h"

int main() {
  const char *path = "/tmp/furble-sim-preferences-api-contract.bin";
  std::remove(path);
  setenv("FURBLE_SIM_PREFS", path, 1);

  Furble::Preferences prefs;
  assert(prefs.begin("api_contract", false));
  uint32_t value = 0;
  assert(prefs.readU32("missing", value) == Furble::Preferences::status::NOT_FOUND);
  assert(prefs.put<uint32_t>("number", 42) == sizeof(uint32_t));
  assert(prefs.readU32("number", value) == Furble::Preferences::status::OK);
  assert(value == 42);
  assert(prefs.put("number", "wrong-type") != 0);
  assert(prefs.readU32("number", value) == Furble::Preferences::status::ERROR);
  assert(prefs.removeKey("number") == Furble::Preferences::status::OK);
  assert(prefs.removeKey("number") == Furble::Preferences::status::NOT_FOUND);
  prefs.end();
  std::remove(path);
  return 0;
}
