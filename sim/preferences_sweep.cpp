#include "preferences_sweep.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <system_error>

#include <sys/stat.h>
#include <unistd.h>

namespace Furble::Sim {
namespace {

constexpr const char *STORE_PREFIX = "furble-sim-preferences-";
constexpr const char *STORE_SUFFIX = ".bin";
constexpr const char *TEMP_MARKER = ".bin.tmp.";

bool parsePositivePid(const std::string &text, int *pid) {
  if (text.empty() || text.size() > 9) {
    return false;
  }
  int value = 0;
  for (const char character : text) {
    if (character < '0' || character > '9') {
      return false;
    }
    value = (value * 10) + (character - '0');
  }
  if (value <= 0) {
    return false;
  }
  *pid = value;
  return true;
}

bool ownedRegularFile(const std::filesystem::path &path) {
  struct stat info = {};
  if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
    return false;
  }
  return info.st_uid == getuid();
}

}  // namespace

std::optional<int> generatedPreferencePid(const std::string &filename) {
  if (filename.rfind(STORE_PREFIX, 0) != 0) {
    return std::nullopt;
  }

  // A temporary sidecar is `<store>.bin.tmp.<pid>`.
  const std::size_t marker = filename.rfind(TEMP_MARKER);
  if (marker != std::string::npos) {
    int pid = 0;
    if (parsePositivePid(filename.substr(marker + std::strlen(TEMP_MARKER)), &pid)) {
      return pid;
    }
    return std::nullopt;
  }

  // A store is `<prefix><scenario>-<pid>-XXXXXX.bin`; skip the random suffix,
  // then read the pid. Scenario names may contain dashes.
  const std::size_t suffixLength = std::strlen(STORE_SUFFIX);
  if (filename.size() <= suffixLength
      || filename.compare(filename.size() - suffixLength, suffixLength, STORE_SUFFIX) != 0) {
    return std::nullopt;
  }
  const std::string base = filename.substr(0, filename.size() - suffixLength);
  const std::size_t randomSeparator = base.rfind('-');
  if (randomSeparator == std::string::npos) {
    return std::nullopt;
  }
  const std::string beforeRandom = base.substr(0, randomSeparator);
  const std::size_t pidSeparator = beforeRandom.rfind('-');
  if (pidSeparator == std::string::npos) {
    return std::nullopt;
  }
  int pid = 0;
  if (!parsePositivePid(beforeRandom.substr(pidSeparator + 1), &pid)) {
    return std::nullopt;
  }
  return pid;
}

bool processAlive(int pid) {
  if (pid <= 0) {
    return false;
  }
  if (kill(pid, 0) == 0) {
    return true;
  }
  // EPERM means the process exists but belongs to another user.
  return errno == EPERM;
}

std::size_t sweepStalePreferences(const std::filesystem::path &directory, int selfPid) {
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) {
    return 0;
  }

  std::size_t removed = 0;
  for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
       it.increment(error)) {
    const std::filesystem::path path = it->path();
    const std::string filename = path.filename().string();
    if (filename.rfind(STORE_PREFIX, 0) != 0) {
      continue;
    }
    const std::optional<int> pid = generatedPreferencePid(filename);
    if (!pid.has_value() || *pid == selfPid || processAlive(*pid)) {
      continue;
    }
    if (!ownedRegularFile(path)) {
      continue;
    }
    if (std::filesystem::remove(path, error)) {
      removed++;
    }
  }
  return removed;
}

}  // namespace Furble::Sim
