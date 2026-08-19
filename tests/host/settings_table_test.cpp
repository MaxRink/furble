// Source invariant tests for the settings table in src/FurbleSettings.cpp.
//
// The protocol conformance suite already checks the wire ids against the golden
// corpus, but two storage invariants had no coverage. ESP-IDF NVS caps both a
// key and a namespace at fifteen characters. A longer string is silently
// truncated at runtime, so two settings could collide on the same stored key.
// This test parses the table straight from the source, so it does not need the
// heavy NVS and Bluetooth headers that a compiled FurbleSettings.cpp pulls in.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ESP-IDF NVS_KEY_NAME_MAX_SIZE and NVS_NS_NAME_MAX_SIZE are sixteen including
// the terminator, so the usable text is fifteen characters.
constexpr size_t NVS_NAME_MAX = 15;

int g_failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_failures++;
  }
}

std::string readText(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::cerr << "FAIL: cannot read " << path << '\n';
    g_failures++;
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

struct Entry {
  std::string symbol;
  int wire_id;
  std::string key;
  std::string ns;
};

// Each table row reads {SYMBOL, wire_id, "Name", "key", NAMESPACE}. The
// namespace is either the FURBLE_STR macro or a quoted literal. The outer map
// wraps every row as {SYMBOL, {row}}, but the inner brace is the only place a
// symbol is followed by a number, so the pattern locks onto it.
//
// The pattern only matches a decimal literal wire id and a FURBLE_STR or quoted
// namespace, so a future row using a macro or hex wire id, or a macro
// namespace, is silently skipped. The row count floor below guards against
// wholesale breakage, not a single dropped row.
std::vector<Entry> parseTable(const std::string &source) {
  const std::regex pattern(
      "\\{\\s*([A-Z0-9_]+)\\s*,\\s*([0-9]+)\\s*,\\s*\"[^\"]*\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*"
      "(FURBLE_STR|\"[^\"]*\")\\s*\\}");
  std::vector<Entry> entries;
  for (std::sregex_iterator it(source.begin(), source.end(), pattern), end; it != end; ++it) {
    Entry entry;
    entry.symbol = (*it)[1].str();
    entry.wire_id = std::stoi((*it)[2].str());
    entry.key = (*it)[3].str();
    std::string ns = (*it)[4].str();
    if (ns == "FURBLE_STR") {
      // FURBLE_STR is defined as "furble" in lib/furble/FurbleTypes.h.
      ns = "furble";
    } else {
      ns = ns.substr(1, ns.size() - 2);
    }
    entry.ns = ns;
    entries.push_back(entry);
  }
  return entries;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " REPOSITORY_ROOT\n";
    return 2;
  }

  const std::string root = argv[1];
  const std::string source = readText(root + "/src/FurbleSettings.cpp");
  const auto entries = parseTable(source);

  check(entries.size() >= 20, "the settings table parsed at least twenty rows");

  std::set<int> exposedIds;
  for (const auto &entry : entries) {
    check(!entry.key.empty(), entry.symbol + " has a non-empty NVS key");
    check(entry.key.size() <= NVS_NAME_MAX,
          entry.symbol + " NVS key '" + entry.key + "' is within the fifteen character limit");
    check(entry.ns.size() <= NVS_NAME_MAX,
          entry.symbol + " NVS namespace '" + entry.ns + "' is within the fifteen character limit");

    // Wire id zero marks a hidden setting that is not exposed on the wire, so
    // only the nonzero ids must be unique.
    if (entry.wire_id != 0) {
      check(exposedIds.insert(entry.wire_id).second,
            entry.symbol + " has a unique exposed wire id");
    }
  }

  if (g_failures > 0) {
    std::cerr << "settings table tests: " << g_failures << " FAILED\n";
    return 1;
  }
  std::cout << "settings table tests: PASS (" << entries.size() << " rows)\n";
  return 0;
}
