#include <sys/stat.h>
#include <unistd.h>
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

  // Close/reopen observes persisted state, and removal persists too.
  assert(prefs.begin("api_contract", false));
  assert(prefs.put<uint32_t>("persisted", 7) == sizeof(uint32_t));
  prefs.end();
  Furble::Preferences reopened;
  assert(reopened.begin("api_contract", false));
  assert(reopened.readU32("persisted", value) == Furble::Preferences::status::OK);
  assert(value == 7);
  assert(reopened.removeKey("persisted") == Furble::Preferences::status::OK);
  reopened.end();
  assert(reopened.begin("api_contract", false));
  assert(reopened.readU32("persisted", value) == Furble::Preferences::status::NOT_FOUND);
  reopened.end();

  // Lifecycle and read-only errors match the firmware API.
  Furble::Preferences unopened;
  assert(unopened.put<uint32_t>("x", 1) == 0);
  assert(unopened.removeKey("x") == Furble::Preferences::status::ERROR);
  assert(unopened.readU32(nullptr, value) == Furble::Preferences::status::ERROR);
  Furble::Preferences readOnly;
  assert(readOnly.begin("api_contract_ro", true));
  assert(readOnly.put<uint32_t>("x", 1) == 0);
  assert(readOnly.removeKey("x") == Furble::Preferences::status::ERROR);
  readOnly.end();

  // An unwritable backend leaves the in-memory value intact and reports an
  // error rather than claiming a successful NVS transaction.
  const char *badPath = "/tmp/furble-sim-preferences-contract-dir";
  mkdir(badPath, 0700);
  setenv("FURBLE_SIM_PREFS", path, 1);
  Furble::Preferences failing;
  assert(failing.begin("api_contract_failure", false));
  assert(failing.put<uint32_t>("stable", 9) == sizeof(uint32_t));
  setenv("FURBLE_SIM_PREFS", badPath, 1);
  assert(failing.put<uint32_t>("stable", 10) == 0);
  assert(failing.readU32("stable", value) == Furble::Preferences::status::OK);
  assert(value == 9);
  assert(failing.removeKey("stable") == Furble::Preferences::status::ERROR);
  assert(failing.readU32("stable", value) == Furble::Preferences::status::OK);
  assert(value == 9);
  failing.end();
  rmdir(badPath);
  std::remove(path);
  return 0;
}
