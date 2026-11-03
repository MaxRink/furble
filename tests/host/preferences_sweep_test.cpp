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
  check(generatedPreferencePid("furble-sim-preferences-my-scenario-123-ABCDEF.bin.tmp.123") == 123,
        "a matching temporary sidecar pid is parsed from its trailing component");
  check(!generatedPreferencePid("furble-sim-preferences-my-scenario-123-ABCDEF.bin.tmp.456")
             .has_value(),
        "a sidecar whose pid does not match its store is rejected");
  check(!generatedPreferencePid("furble-sim-preferences-broken.bin").has_value(),
        "a store without a pid is rejected");
  check(!generatedPreferencePid("unrelated-123-ABCDEF.bin").has_value(),
        "an unrelated file is rejected");
  check(!generatedPreferencePid("furble-sim-preferences-user-123-x.bin").has_value(),
        "a store without the six-character random component is rejected");
  check(!generatedPreferencePid("furble-sim-preferences-not-a-store.bin.tmp.123").has_value(),
        "a sidecar with an invalid primary store is rejected");
  check(generatedPreferencePid("furble-sim-preferences-foo.bin.tmp.999-456-ABCDEF.bin") == 456,
        "a scenario containing .bin.tmp. still parses as a normal store");

  char directoryTemplate[] = "/tmp/furble-prefs-XXXXXX";
  const char *created = mkdtemp(directoryTemplate);
  if (created == nullptr) {
    std::cerr << "FAIL: could not create the temporary directory\n";
    return 1;
  }
  const std::filesystem::path directory(created);

  const pid_t dead = spawnChild(false);
  if (dead <= 0) {
    std::cerr << "FAIL: the dead child was not created\n";
    std::filesystem::remove_all(directory);
    return 1;
  }
  int status = 0;
  waitpid(dead, &status, 0);

  const pid_t live = spawnChild(true);
  if (live <= 0) {
    std::cerr << "FAIL: the live child was not created\n";
    std::filesystem::remove_all(directory);
    return 1;
  }

  const std::string deadName =
      "furble-sim-preferences-stale-" + std::to_string(dead) + "-ABCDEF.bin";
  const std::string liveName =
      "furble-sim-preferences-live-" + std::to_string(live) + "-ABCDEF.bin";
  const std::string selfName =
      "furble-sim-preferences-self-" + std::to_string(getpid()) + "-ABCDEF.bin";
  const std::string temporaryName = deadName + ".tmp." + std::to_string(dead);
  const std::string callerName =
      "furble-sim-preferences-caller-" + std::to_string(dead) + "-ABCDEF.bin";
  const std::filesystem::path deadFile = directory / deadName;
  const std::filesystem::path liveFile = directory / liveName;
  const std::filesystem::path selfFile = directory / selfName;
  const std::filesystem::path temporaryFile = directory / temporaryName;
  const std::filesystem::path callerFile = directory / callerName;
  const std::filesystem::path unrelatedFile = directory / "notes.txt";
  const std::filesystem::path foreignSidecar = directory / (deadName + ".tmp.999999");
  const std::filesystem::path notesSidecar = directory / (deadName + ".tmp.notes");
  touch(deadFile);
  touch(std::filesystem::path(deadFile.string() + ".generated"));
  touch(liveFile);
  touch(std::filesystem::path(liveFile.string() + ".generated"));
  touch(selfFile);
  touch(std::filesystem::path(selfFile.string() + ".generated"));
  touch(temporaryFile);
  // A caller-owned path can match the generated name; without the marker it is
  // never treated as simulator-owned.
  touch(callerFile);
  touch(unrelatedFile);
  touch(foreignSidecar);
  touch(notesSidecar);

  const std::size_t removed = sweepStalePreferences(directory, static_cast<int>(getpid()));
  check(removed == 2, "the sweep removed the dead store and its sidecar");
  check(!std::filesystem::exists(deadFile), "the dead store was removed");
  check(!std::filesystem::exists(deadFile.string() + ".generated"),
        "the dead store marker was removed");
  check(!std::filesystem::exists(temporaryFile), "the dead sidecar was removed");
  check(std::filesystem::exists(liveFile), "a live store was kept");
  check(std::filesystem::exists(selfFile), "the current process store was kept");
  check(std::filesystem::exists(callerFile), "a caller-owned store without a marker was kept");
  check(std::filesystem::exists(foreignSidecar), "a sidecar with another pid was kept");
  check(std::filesystem::exists(notesSidecar), "a non-pid temporary file was kept");
  check(std::filesystem::exists(unrelatedFile), "an unrelated file was kept");

  if (live > 0) {
    kill(live, SIGKILL);
    waitpid(live, &status, 0);
  }
  std::filesystem::remove_all(directory);

  if (failures != 0) {
    std::cerr << failures << " preference sweep checks failed\n";
    return 1;
  }
  std::cout << "preference sweep: PASS\n";
  return 0;
}
