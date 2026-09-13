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
#include <map>
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

struct Reservation {
  std::string owner;
  int wire_id;
};

std::string trim(std::string value) {
  const size_t first = value.find_first_not_of(" \t");
  if (first == std::string::npos) {
    return {};
  }
  const size_t last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1);
}

bool parseReservations(const std::string &source,
                       std::vector<Reservation> &reservations,
                       std::string &error) {
  reservations.clear();
  error.clear();
  const std::regex rowPattern(R"(^\|\s*([^|]+)\|[^|]*\|\s*([^|]+)\|\s*$)");
  const std::regex idPattern(R"(^([0-9]+)(-([0-9]+))?(\s*\([^)]*\))?$)");
  bool inLedger = false;
  bool foundHeader = false;
  std::istringstream lines(source);
  std::string line;
  while (std::getline(lines, line)) {
    if (trim(line) == "### Companion wire id reservations") {
      inLedger = true;
      continue;
    }
    if (!inLedger) {
      continue;
    }
    if (line.empty() || (line[0] != '|')) {
      if (foundHeader) {
        break;
      }
      continue;
    }
    std::smatch row;
    if (!std::regex_match(line, row, rowPattern)) {
      error = "malformed reservation row";
      return false;
    }
    const std::string owner = trim(row[1].str());
    const std::string ids = trim(row[2].str());
    if (owner == "PR") {
      foundHeader = true;
      continue;
    }
    if (owner == "---") {
      continue;
    }
    foundHeader = true;
    if (owner.empty() || ids.empty()) {
      error = "empty reservation owner or ID column";
      return false;
    }
    // Retain a marker for every row, including rows declaring no IDs, so
    // validation can enforce owner-row presence and uniqueness.
    reservations.push_back({owner, 0});
    if (ids == "none") {
      continue;
    }
    if (ids.back() == ',') {
      error = "empty reservation ID token";
      return false;
    }
    std::istringstream tokens(ids);
    std::string token;
    while (std::getline(tokens, token, ',')) {
      token = trim(token);
      if (token.empty()) {
        error = "empty reservation ID token";
        return false;
      }
      std::smatch id;
      if (!std::regex_match(token, id, idPattern)) {
        error = "malformed reservation ID token";
        return false;
      }
      const int first = std::stoi(id[1].str());
      const int last = id[3].matched ? std::stoi(id[3].str()) : first;
      if ((first < 1) || (last > 255) || (first > last)) {
        error = "reservation ID is out of range or reversed";
        return false;
      }
      for (int wireId = first; wireId <= last; ++wireId) {
        reservations.push_back({owner, wireId});
      }
    }
  }
  if (!foundHeader || reservations.empty()) {
    error = "reservation table is missing or empty";
    return false;
  }
  return true;
}

bool validateReservations(const std::vector<Reservation> &reservations,
                          const std::set<int> &exposedIds,
                          std::string &error) {
  static const std::set<std::string> expectedOwners = {
      "Master", "Master (conditional)", "Historical claims", "#59", "#63", "#90", "#265", "#273"};
  std::set<std::string> owners;
  std::set<std::string> ownerRows;
  std::map<int, std::string> reservationOwners;
  std::set<int> masterIds;
  for (const auto &reservation : reservations) {
    if (reservation.wire_id == 0) {
      if (!ownerRows.insert(reservation.owner).second) {
        error = "reservation owner row is duplicated";
        return false;
      }
      owners.insert(reservation.owner);
      continue;
    }
    owners.insert(reservation.owner);
    if (!reservationOwners.emplace(reservation.wire_id, reservation.owner).second) {
      error = "reservation wire ID has multiple owners";
      return false;
    }
    if (reservation.owner.find("Master") == 0) {
      masterIds.insert(reservation.wire_id);
    }
  }
  if (owners != expectedOwners) {
    error = "reservation owner rows are incomplete";
    return false;
  }
  if (masterIds != exposedIds) {
    error = "Master reservation IDs do not equal the settings table";
    return false;
  }
  for (const auto &reservation : reservations) {
    if ((reservation.owner.find("Master") != 0) && (exposedIds.count(reservation.wire_id) != 0)) {
      error = "non-Master reservation is already shipped";
      return false;
    }
  }
  return true;
}

void testReservationParser() {
  const std::string valid =
      "### Companion wire id reservations\n"
      "| PR | Setting keys | Wire ids |\n"
      "| --- | --- | --- |\n"
      "| Master | shipped | 1-2 |\n"
      "| Master (conditional) | conditional | 3 (S3 42) |\n"
      "| Historical claims | audit | 4, 5 |\n"
      "| #59 | setting | 75, 76 |\n"
      "| #63 | none | none |\n"
      "| #90 | setting | 62 |\n"
      "| #265 | none | none |\n"
      "| #273 | setting | 65 |\n";
  std::vector<Reservation> parsed;
  std::string error;
  check(parseReservations(valid, parsed, error), "reservation parser accepts annotated IDs");
  check(validateReservations(parsed, {1, 2, 3}, error), "complete reservation owner set validates");

  std::string deleted = valid.substr(0, valid.find("| #273"));
  check(parseReservations(deleted, parsed, error), "deleted-row fixture parses");
  check(!validateReservations(parsed, {1, 2, 3}, error),
        "deleted reservation owner row is rejected");

  std::string duplicate = valid + "| #90 | duplicate | 62 |\n";
  check(parseReservations(duplicate, parsed, error), "duplicate-row fixture parses");
  check(!validateReservations(parsed, {1, 2, 3}, error), "duplicate reservation row is rejected");

  std::string duplicateOwner = valid + "| #90 | duplicate owner | 250 |\n";
  check(parseReservations(duplicateOwner, parsed, error),
        "distinct-ID duplicate-owner fixture parses");
  check(!validateReservations(parsed, {1, 2, 3}, error),
        "duplicate owner is rejected even with a distinct ID");

  std::string emptyToken = valid;
  emptyToken.replace(emptyToken.find("75, 76"), 6, "75,,76");
  check(!parseReservations(emptyToken, parsed, error), "empty reservation token is rejected");

  std::string trailingToken = valid;
  trailingToken.replace(trailingToken.find("75, 76"), 6, "75, 76,");
  check(!parseReservations(trailingToken, parsed, error),
        "trailing empty reservation token is rejected");

  std::string reversed = valid;
  reversed.replace(reversed.find("75, 76"), 6, "76-75");
  check(!parseReservations(reversed, parsed, error), "reversed reservation range is rejected");

  std::string outOfRange = valid;
  outOfRange.replace(outOfRange.find("75, 76"), 6, "256");
  check(!parseReservations(outOfRange, parsed, error), "out-of-range reservation ID is rejected");

  std::string malformedAnnotation = valid;
  malformedAnnotation.replace(malformedAnnotation.find("3 (S3 42)"), 9, "3 (S3 42");
  check(!parseReservations(malformedAnnotation, parsed, error),
        "malformed reservation annotation is rejected");

  check(parseReservations(valid, parsed, error), "omitted-source fixture parses");
  check(!validateReservations(parsed, {1, 2}, error), "omitted Master source ID is rejected");
}

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
  std::vector<Reservation> reservations;
  std::string reservationError;
  check(parseReservations(readText(root + "/include/CLAUDE.md"), reservations, reservationError),
        "reservation table parses: " + reservationError);

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

  check(validateReservations(reservations, exposedIds, reservationError), reservationError);
  testReservationParser();

  if (g_failures > 0) {
    std::cerr << "settings table tests: " << g_failures << " FAILED\n";
    return 1;
  }
  std::cout << "settings table tests: PASS (" << entries.size() << " rows)\n";
  return 0;
}
