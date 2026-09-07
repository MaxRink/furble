#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "Preferences.h"

namespace {

using Result = Furble::Preferences::string_result_t;

int fail(const char *message) {
  std::fprintf(stderr, "%s\n", message);
  return 1;
}

bool writeBytes(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  return file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())).good();
}

int checkResult(const std::filesystem::path &path, Result expected) {
  setenv("FURBLE_SIM_PREFS", path.c_str(), 1);
  Furble::Preferences preferences;
  if (!preferences.begin("prefs", true)) {
    return fail("begin failed");
  }
  std::string value;
  const Result result = preferences.getString("value", value);
  preferences.end();
  return result == expected ? 0 : fail("unexpected getString result");
}

int run(const std::string &scenario) {
  const auto path = std::filesystem::temp_directory_path()
                    / ("furble-preferences-sim-" + std::to_string(getpid()));
  std::filesystem::remove_all(path);
  const auto cleanup = [&] {
    std::filesystem::remove_all(path);
    std::filesystem::remove_all(path.string() + ".blocked");
  };

  if (scenario == "missing") {
    const int result = checkResult(path, Result::NOT_FOUND);
    cleanup();
    return result;
  }
  if (scenario == "empty") {
    std::filesystem::create_directories(path.parent_path());
    writeBytes(path, "");
    const int result = checkResult(path, Result::ERROR);
    cleanup();
    return result;
  }
  if (scenario == "corruption") {
    std::string bytes(4, '\0');
    bytes[0] = 1;
    writeBytes(path, bytes);
    const int result = checkResult(path, Result::ERROR);
    cleanup();
    return result;
  }
  if (scenario == "typeerror") {
    // One uint8 value under the first namespace handle, not a string.
    std::string bytes(4, '\0');
    bytes[0] = 1;
    bytes.push_back(7);
    bytes.append(3, '\0');
    bytes.push_back(1);
    bytes.append(4, '\0');
    bytes.push_back(0);
    bytes.append("1:value", 7);
    bytes.push_back(42);
    const int result = checkResult(path, Result::ERROR);
    cleanup();
    return result;
  }
  if (scenario == "embedded-nul") {
    // A string with an embedded NUL must remain non-empty and byte-preserving.
    std::string bytes(4, '\0');
    bytes[0] = 1;
    bytes.push_back(7);
    bytes.append(3, '\0');
    bytes.push_back(3);
    bytes.append(3, '\0');
    bytes.push_back(1);
    bytes.append("1:value", 7);
    bytes.push_back('\0');
    bytes.push_back('x');
    bytes.push_back('\0');
    writeBytes(path, bytes);
    setenv("FURBLE_SIM_PREFS", path.c_str(), 1);
    Furble::Preferences preferences;
    if (!preferences.begin("prefs", true)) {
      cleanup();
      return fail("begin failed");
    }
    std::string value;
    const bool preserved = preferences.getString("value", value) == Result::OK
                           && value.size() == 2 && value[0] == '\0' && value[1] == 'x';
    preferences.end();
    cleanup();
    return preserved ? 0 : fail("embedded NUL was truncated");
  }
  if (scenario == "empty-string") {
    setenv("FURBLE_SIM_PREFS", path.c_str(), 1);
    Furble::Preferences preferences;
    if (!preferences.begin("prefs") || !preferences.putString("value", "")) {
      cleanup();
      return fail("empty string write failed");
    }
    std::string value = "sentinel";
    const bool ok = preferences.getString("value", value) == Result::OK && value.empty();
    preferences.end();
    cleanup();
    return ok ? 0 : fail("empty string read failed");
  }
  if (scenario == "failed-save") {
    setenv("FURBLE_SIM_PREFS", path.c_str(), 1);
    Furble::Preferences preferences;
    if (!preferences.begin("prefs") || !preferences.putString("value", "old")) {
      cleanup();
      return fail("initial write failed");
    }
    const auto blocked = path.string() + ".blocked";
    std::filesystem::create_directory(blocked);
    setenv("FURBLE_SIM_PREFS", blocked.c_str(), 1);
    const bool failed = !preferences.putString("value", "new");
    std::string value;
    const bool rolled_back = preferences.getString("value", value) == Result::OK
                             && value == "old";
    preferences.end();
    cleanup();
    return failed && rolled_back ? 0 : fail("failed save did not roll back");
  }
  return fail("unknown scenario");
}

}  // namespace

int main(int argc, char **argv) {
  return argc == 2 ? run(argv[1]) : fail("scenario argument required");
}
