// Generated preference-store sweep for issue #289.
//
// A simulator run that dies without orderly cleanup leaves
// `.pio/furble-sim-preferences-<scenario>-<pid>-XXXXXX.bin` behind. The sweep
// removes stores whose encoded pid no longer exists, but must keep stores owned
// by a live process, the current process, and unrelated files.

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

#include "preferences_sweep.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

pid_t spawnChild(bool stayAlive) {
  const pid_t child = fork();
  if (child == 0) {
    if (stayAlive) {
      sleep(30);
    }
    _exit(0);
  }
  return child;
}

void touch(const std::filesystem::path &path) {
  std::ofstream stream(path);
  stream << "store";
}

}  // namespace

int main() {
  using namespace Furble::Sim;

  check(generatedPreferencePid("furble-sim-preferences-my-scenario-123-ABCDEF.bin") == 123,
        "a store pid is parsed to the left of the random suffix");
  check(generatedPreferencePid("furble-sim-preferences-my-scenario-123-ABCDEF.bin.tmp.456") == 456,
        "a temporary sidecar pid is parsed from its trailing component");
  check(!generatedPreferencePid("furble-sim-preferences-broken.bin").has_value(),
        "a store without a pid is rejected");
  check(!generatedPreferencePid("unrelated-123-ABCDEF.bin").has_value(),
        "an unrelated file is rejected");

  char directoryTemplate[] = "/tmp/furble-prefs-XXXXXX";
  const char *created = mkdtemp(directoryTemplate);
  if (created == nullptr) {
    std::cerr << "FAIL: could not create the temporary directory\n";
    return 1;
  }
  const std::filesystem::path directory(created);

  const pid_t dead = spawnChild(false);
  check(dead > 0, "the dead child was created");
  int status = 0;
  waitpid(dead, &status, 0);

  const pid_t live = spawnChild(true);
  check(live > 0, "the live child was created");

  const std::string deadName =
      "furble-sim-preferences-stale-" + std::to_string(dead) + "-ABCDEF.bin";
  const std::string liveName =
      "furble-sim-preferences-live-" + std::to_string(live) + "-ABCDEF.bin";
  const std::string selfName =
      "furble-sim-preferences-self-" + std::to_string(getpid()) + "-ABCDEF.bin";
  const std::string temporaryName = deadName + ".tmp." + std::to_string(dead);
  const std::filesystem::path deadFile = directory / deadName;
  const std::filesystem::path liveFile = directory / liveName;
  const std::filesystem::path selfFile = directory / selfName;
  const std::filesystem::path temporaryFile = directory / temporaryName;
  const std::filesystem::path unrelatedFile = directory / "notes.txt";
  touch(deadFile);
  touch(liveFile);
  touch(selfFile);
  touch(temporaryFile);
  touch(unrelatedFile);

  const std::size_t removed = sweepStalePreferences(directory, static_cast<int>(getpid()));
  check(removed == 2, "the sweep removed the dead store and its sidecar");
  check(!std::filesystem::exists(deadFile), "the dead store was removed");
  check(!std::filesystem::exists(temporaryFile), "the dead sidecar was removed");
  check(std::filesystem::exists(liveFile), "a live store was kept");
  check(std::filesystem::exists(selfFile), "the current process store was kept");
  check(std::filesystem::exists(unrelatedFile), "an unrelated file was kept");

  kill(live, SIGKILL);
  waitpid(live, &status, 0);
  std::filesystem::remove_all(directory);

  if (failures != 0) {
    std::cerr << failures << " preference sweep checks failed\n";
    return 1;
  }
  std::cout << "preference sweep: PASS\n";
  return 0;
}
