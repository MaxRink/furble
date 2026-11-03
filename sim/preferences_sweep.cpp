#include "preferences_sweep.h"

#include <cctype>
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
constexpr const char *STORE_MARKER_SUFFIX = ".generated";

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

std::optional<int> parseGeneratedStore(const std::string &filename) {
  if (filename.rfind(STORE_PREFIX, 0) != 0) {
    return std::nullopt;
  }
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
  const std::string random = base.substr(randomSeparator + 1);
  if (random.size() != 6) {
    return std::nullopt;
  }
  for (const char character : random) {
    if (!std::isalnum(static_cast<unsigned char>(character))) {
      return std::nullopt;
    }
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

}  // namespace

std::optional<int> generatedPreferencePid(const std::string &filename) {
  // A temporary sidecar is `<store>.bin.tmp.<pid>`. Treat it as a sidecar only
  // when the trailing component is a pid and the primary store, reconstructed
  // with its `.bin` suffix, is a valid generated name with the same pid.
  // Otherwise fall through so a scenario containing `.bin.tmp.` still parses.
  const std::size_t marker = filename.rfind(TEMP_MARKER);
  if (marker != std::string::npos) {
    int sidecarPid = 0;
    if (parsePositivePid(filename.substr(marker + std::strlen(TEMP_MARKER)), &sidecarPid)) {
      const std::string primary = filename.substr(0, marker) + STORE_SUFFIX;
      const std::optional<int> storePid = parseGeneratedStore(primary);
      if (storePid.has_value() && *storePid == sidecarPid) {
        return sidecarPid;
      }
    }
  }
  return parseGeneratedStore(filename);
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

    // Only a store with the simulator-owned marker is ours. A caller-owned
    // path can match the generated naming pattern; the marker is the proof
    // that this run created it, not the caller.
    const std::filesystem::path marker = path.string() + STORE_MARKER_SUFFIX;
    if (!filename.empty() && filename.find(TEMP_MARKER) != std::string::npos) {
      // A sidecar is removable only while its primary store and marker exist.
      const std::string primaryName = filename.substr(0, filename.find(TEMP_MARKER)) + STORE_SUFFIX;
      const std::filesystem::path primary = directory / primaryName;
      if (!ownedRegularFile(primary)
          || !std::filesystem::is_regular_file(primary.string() + STORE_MARKER_SUFFIX, error)) {
        continue;
      }
      if (std::filesystem::remove(path, error)) {
        removed++;
      }
      continue;
    }
    if (!std::filesystem::is_regular_file(marker, error)) {
      continue;
    }
    if (std::filesystem::remove(path, error)) {
      removed++;
      (void)std::filesystem::remove(marker, error);
    }
  }
  return removed;
}

}  // namespace Furble::Sim
