// Local timezone conversion and POSIX TZ table regression.
//
// The table maps one POSIX TZ string per supported zone. These cases pin both
// sides of the 2026 DST transitions so a wrong sign or a wrong weekday rule
// fails on the host instead of on a camera clock.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "FurbleTimezone.h"
#include "FurbleTimezoneGPS.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    failures++;
  }
}

Furble::Timezone::local_t localAt(Furble::Timezone::Id zone, uint64_t epoch_us) {
  Furble::Timezone::local_t local = {};
  if (!Furble::Timezone::apply(zone)) {
    std::fprintf(stderr, "FAIL: cannot apply %s\n", Furble::Timezone::name(zone));
    failures++;
    return local;
  }
  if (!Furble::Timezone::toLocal(epoch_us, local)) {
    std::fprintf(stderr, "FAIL: cannot convert %llu in %s\n",
                 static_cast<unsigned long long>(epoch_us), Furble::Timezone::name(zone));
    failures++;
  }
  return local;
}

void checkLocal(const Furble::Timezone::local_t &local,
                uint16_t year,
                uint8_t month,
                uint8_t day,
                uint8_t hour,
                uint8_t minute,
                uint8_t second,
                int16_t offset,
                bool dst,
                const char *label) {
  check((local.year == year) && (local.month == month) && (local.day == day) && (local.hour == hour)
            && (local.minute == minute) && (local.second == second),
        label);
  check(local.offset_minutes == offset, label);
  check(local.dst == dst, label);
}

}  // namespace

int main() {
  using namespace Furble::Timezone;

  check(ZONE_COUNT == 39, "the curated table has 39 zones");
  check(SELECTION_COUNT == 40, "the selector adds Auto (GPS) to the table");
  check(std::strcmp(name(Id::EUROPE_BERLIN), "Europe/Berlin") == 0, "Berlin name");
  check(std::strcmp(label(Id::AMERICA_NEW_YORK), "US Eastern") == 0, "Eastern label");
  check(std::strcmp(name(Id::AUTO), "Auto (GPS)") == 0, "Auto is not a table row");
  check(zone(Id::AUTO) == nullptr, "Auto has no POSIX row of its own");
  check(std::strcmp(tzdataVersion(), "2026d") == 0, "tzdata review version is visible");

  Id found = Id::COUNT;
  check(find("Europe/Berlin", found) && (found == Id::EUROPE_BERLIN), "find by IANA name");
  check(find("us eastern", found) && (found == Id::AMERICA_NEW_YORK), "find by label");
  check(find("3", found) && (found == Id::AMERICA_NEW_YORK), "find by table index");
  check(find("auto", found) && (found == Id::AUTO), "find auto");
  check(!find("Mars/Olympus", found), "an unknown zone is refused");
  check(clamp(200) == Id::UTC, "an out-of-range stored value falls back to UTC");
  check(clamp(static_cast<uint8_t>(Id::AUTO)) == Id::AUTO, "auto survives a stored value");

  const uint64_t berlinWinter = 1768478400ULL * 1000000ULL;

  // Every row must be unique and resolvable through localtime_r. This is the
  // table refresh sanity check: a duplicate name or a malformed POSIX string
  // fails here before it can reach a camera.
  std::set<std::string> names;
  for (size_t index = 0; index < ZONE_COUNT; index++) {
    const zone_t *entry = zone(static_cast<Id>(index));
    check(entry != nullptr, "every table row has an entry");
    if (entry == nullptr) {
      continue;
    }
    check(names.insert(entry->name).second, "zone names are unique");
    check(entry->standard_offset_minutes >= -720 && entry->standard_offset_minutes <= 840,
          "standard offsets stay in the real-world range");
    local_t probe = {};
    check(apply(entry->id) && toLocal(berlinWinter, probe), "the POSIX string applies");
  }

  const uint64_t berlinSpringBefore = 1774745999ULL * 1000000ULL;
  const uint64_t berlinSpringAfter = 1774746000ULL * 1000000ULL;
  const uint64_t berlinAutumnBefore = 1792889999ULL * 1000000ULL;
  const uint64_t berlinAutumnAfter = 1792890000ULL * 1000000ULL;

  checkLocal(localAt(Id::EUROPE_BERLIN, berlinWinter), 2026, 1, 15, 13, 0, 0, 60, false,
             "Berlin winter is CET");
  checkLocal(localAt(Id::EUROPE_BERLIN, berlinSpringBefore), 2026, 3, 29, 1, 59, 59, 60, false,
             "Berlin just before the spring transition is CET");
  checkLocal(localAt(Id::EUROPE_BERLIN, berlinSpringAfter), 2026, 3, 29, 3, 0, 0, 120, true,
             "Berlin at the spring transition is CEST");
  checkLocal(localAt(Id::EUROPE_BERLIN, berlinAutumnBefore), 2026, 10, 25, 2, 59, 59, 120, true,
             "Berlin just before the autumn transition is CEST");
  checkLocal(localAt(Id::EUROPE_BERLIN, berlinAutumnAfter), 2026, 10, 25, 2, 0, 0, 60, false,
             "Berlin at the autumn transition is CET");

  checkLocal(localAt(Id::EUROPE_LONDON, 1774746000ULL * 1000000ULL), 2026, 3, 29, 2, 0, 0, 60, true,
             "London spring transition is BST");
  checkLocal(localAt(Id::EUROPE_LONDON, 1792890000ULL * 1000000ULL), 2026, 10, 25, 1, 0, 0, 0,
             false, "London autumn transition is GMT");

  checkLocal(localAt(Id::AMERICA_NEW_YORK, 1772953199ULL * 1000000ULL), 2026, 3, 8, 1, 59, 59, -300,
             false, "New York just before the spring transition is EST");
  checkLocal(localAt(Id::AMERICA_NEW_YORK, 1772953200ULL * 1000000ULL), 2026, 3, 8, 3, 0, 0, -240,
             true, "New York at the spring transition is EDT");
  checkLocal(localAt(Id::AMERICA_NEW_YORK, 1793512800ULL * 1000000ULL), 2026, 11, 1, 1, 0, 0, -300,
             false, "New York at the autumn transition is EST");

  checkLocal(localAt(Id::AUSTRALIA_SYDNEY, 1791043199ULL * 1000000ULL), 2026, 10, 4, 1, 59, 59, 600,
             false, "Sydney just before the spring transition is AEST");
  checkLocal(localAt(Id::AUSTRALIA_SYDNEY, 1791043200ULL * 1000000ULL), 2026, 10, 4, 3, 0, 0, 660,
             true, "Sydney at the spring transition is AEDT");
  checkLocal(localAt(Id::AUSTRALIA_SYDNEY, 1775318400ULL * 1000000ULL), 2026, 4, 5, 2, 0, 0, 600,
             false, "Sydney at the autumn transition is AEST");

  checkLocal(localAt(Id::ASIA_TOKYO, berlinWinter), 2026, 1, 15, 21, 0, 0, 540, false,
             "Tokyo has no DST");
  checkLocal(localAt(Id::ASIA_KOLKATA, berlinWinter), 2026, 1, 15, 17, 30, 0, 330, false,
             "Kolkata keeps its half-hour offset");

  checkLocal(localAt(Id::UTC, berlinWinter), 2026, 1, 15, 12, 0, 0, 0, false, "UTC stays UTC");

  check(nearest(52.52, 13.40, found) && (found == Id::EUROPE_BERLIN), "Berlin coordinate");
  check(nearest(51.50, -0.12, found) && (found == Id::EUROPE_LONDON), "London coordinate");
  check(nearest(40.71, -74.01, found) && (found == Id::AMERICA_NEW_YORK), "New York coordinate");
  check(nearest(35.68, 139.69, found) && (found == Id::ASIA_TOKYO), "Tokyo coordinate");
  check(!nearest(-60.0, -30.0, found), "remote ocean is outside the snap radius");

  using Furble::TimezoneGPS::freshWiredFix;
  constexpr uint32_t FRESH = 30000;
  check(freshWiredFix(true, true, true, true, true, true, true, 1, 2, 3, FRESH),
        "a fresh wired fix is accepted");
  check(!freshWiredFix(false, true, true, true, true, true, true, 1, 2, 3, FRESH),
        "a disabled receiver is rejected");
  check(!freshWiredFix(true, false, true, true, true, true, true, 1, 2, 3, FRESH),
        "a held fix is rejected");
  check(!freshWiredFix(true, true, false, true, true, true, true, 1, 2, 3, FRESH),
        "a companion fix is rejected");
  check(!freshWiredFix(true, true, true, true, true, true, true, FRESH, 2, 3, FRESH),
        "a stale location is rejected");
  check(!freshWiredFix(true, true, true, true, true, true, true, 1, 2, FRESH, FRESH),
        "a stale time is rejected");

  check(apply(Id::AUTO), "auto selection applies");
  check((current() == Id::UTC) && (source() == Source::UTC), "auto starts at UTC without a fix");
  check(updateAuto(52.52, 13.40, 1000), "the first fresh fix applies a zone");
  check((current() == Id::EUROPE_BERLIN) && (source() == Source::GPS), "auto picks Berlin");
  check(!updateAuto(51.50, -0.12, 2000), "a candidate is held inside the minimum interval");
  check(current() == Id::EUROPE_BERLIN, "the held zone is retained");
  check(updateAuto(51.50, -0.12, 2000 + AUTO_MIN_UPDATE_MS), "the next interval applies London");
  check(current() == Id::EUROPE_LONDON, "auto picks London after the interval");
  check(!service(2000 + AUTO_MIN_UPDATE_MS + AUTO_RETAIN_MS), "the derived zone is still fresh");
  check(service(2000 + AUTO_MIN_UPDATE_MS + AUTO_RETAIN_MS + 1), "an old derived zone expires");
  check((current() == Id::UTC) && (source() == Source::UTC), "auto falls back to UTC");

  check(apply(Id::EUROPE_BERLIN), "manual selection applies");
  check(!updateAuto(40.71, -74.01, 1000 + AUTO_MIN_UPDATE_MS), "manual wins over a GPS candidate");
  check((current() == Id::EUROPE_BERLIN) && (source() == Source::MANUAL), "manual zone is kept");
  check(apply(Id::AUTO), "auto can be selected again");
  check((current() == Id::UTC) && (source() == Source::UTC),
        "switching back to auto starts from UTC");

  local_t invalid = {};
  check(
      !toLocal(static_cast<uint16_t>(2026), static_cast<uint8_t>(2), static_cast<uint8_t>(29),
               static_cast<uint8_t>(12), static_cast<uint8_t>(0), static_cast<uint8_t>(0), invalid),
      "an invalid date is refused");

  return failures == 0 ? 0 : 1;
}
