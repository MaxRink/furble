#include "FurbleTimezone.h"

#include <strings.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

#include "protocol/UtcTime.h"

namespace Furble {
namespace Timezone {
namespace {

/*
 * Source: IANA tzdata through the POSIX strings published in
 * https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv and the
 * representative coordinates in zone1970.tab / zone.tab. Refresh the table with
 * tools/timezone_table.py after a tzdata release: it validates names, POSIX
 * strings, offsets, and the auto-zone ids, then prints the rows to paste here.
 * Bump TZDATA_REVIEWED when the strings or coordinates change.
 *
 * The table is a curated set of major zones, not every IANA zone. Auto mode
 * picks the nearest entry and is an approximation: a point near a border or in
 * a large country can resolve to a neighbouring rule. Remote oceans and polar
 * regions deliberately fall back to UTC beyond AUTO_MAX_DISTANCE_DEGREES.
 */
constexpr const char *TZDATA_REVIEWED = "2026d";

constexpr zone_t ZONES[] = {
    // Name, label, POSIX TZ, standard offset minutes, DST shift minutes.
    {Id::UTC,                            "UTC",                            "UTC",              "UTC0",                            0,    0 },
    {Id::EUROPE_BERLIN,                  "Europe/Berlin",                  "Europe/Berlin",    "CET-1CEST,M3.5.0,M10.5.0/3",      60,   60},
    {Id::EUROPE_LONDON,                  "Europe/London",                  "Europe/London",    "GMT0BST,M3.5.0/1,M10.5.0",        0,    60},
    {Id::AMERICA_NEW_YORK,               "America/New_York",               "US Eastern",       "EST5EDT,M3.2.0,M11.1.0",          -300, 60},
    {Id::AMERICA_CHICAGO,                "America/Chicago",                "US Central",       "CST6CDT,M3.2.0,M11.1.0",          -360, 60},
    {Id::AMERICA_DENVER,                 "America/Denver",                 "US Mountain",      "MST7MDT,M3.2.0,M11.1.0",          -420, 60},
    {Id::AMERICA_LOS_ANGELES,            "America/Los_Angeles",            "US Pacific",       "PST8PDT,M3.2.0,M11.1.0",          -480,
     60                                                                                                                                   },
    {Id::AUSTRALIA_SYDNEY,               "Australia/Sydney",               "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3",
     600,                                                                                                                               60},
    {Id::ASIA_TOKYO,                     "Asia/Tokyo",                     "Asia/Tokyo",       "JST-9",                           540,  0 },
    {Id::ASIA_KOLKATA,                   "Asia/Kolkata",                   "India",            "IST-5:30",                        330,  0 },
    {Id::EUROPE_ATHENS,                  "Europe/Athens",                  "Europe/Athens",    "EET-2EEST,M3.5.0/3,M10.5.0/4",    120,  60},
    {Id::EUROPE_MOSCOW,                  "Europe/Moscow",                  "Moscow",           "MSK-3",                           180,  0 },
    {Id::EUROPE_LISBON,                  "Europe/Lisbon",                  "Europe/Lisbon",    "WET0WEST,M3.5.0/1,M10.5.0",       0,    60},
    {Id::AMERICA_HALIFAX,                "America/Halifax",                "US Atlantic",      "AST4ADT,M3.2.0,M11.1.0",          -240, 60},
    {Id::AMERICA_ST_JOHNS,               "America/St_Johns",               "Newfoundland",     "NST3:30NDT,M3.2.0,M11.1.0",       -210,
     60                                                                                                                                   },
    {Id::AMERICA_SAO_PAULO,              "America/Sao_Paulo",              "Brazil",           "<-03>3",                          -180, 0 },
    {Id::AMERICA_SANTIAGO,               "America/Santiago",               "Chile",            "<-04>4<-03>,M9.1.6/24,M4.1.6/24", -240,
     60                                                                                                                                   },
    {Id::AMERICA_ARGENTINA_BUENOS_AIRES, "America/Argentina/Buenos_Aires", "Argentina",        "<-03>3",
     -180,                                                                                                                              0 },
    {Id::AMERICA_MEXICO_CITY,            "America/Mexico_City",            "Mexico",           "CST6",                            -360, 0 },
    {Id::AMERICA_PHOENIX,                "America/Phoenix",                "US Arizona",       "MST7",                            -420, 0 },
    {Id::AMERICA_ANCHORAGE,              "America/Anchorage",              "US Alaska",        "AKST9AKDT,M3.2.0,M11.1.0",        -540, 60},
    {Id::PACIFIC_HONOLULU,               "Pacific/Honolulu",               "Hawaii",           "HST10",                           -600, 0 },
    {Id::AFRICA_CAIRO,                   "Africa/Cairo",                   "Egypt",            "EET-2EEST,M4.5.5/0,M10.5.4/24",   120,  60},
    {Id::AFRICA_JOHANNESBURG,            "Africa/Johannesburg",            "South Africa",     "SAST-2",                          120,  0 },
    {Id::AFRICA_LAGOS,                   "Africa/Lagos",                   "West Africa",      "WAT-1",                           60,   0 },
    {Id::AFRICA_NAIROBI,                 "Africa/Nairobi",                 "East Africa",      "EAT-3",                           180,  0 },
    // Morocco is permanently +01 outside Ramadan. POSIX TZ has no Ramadan
    // exception, so this is the baseline rule.
    {Id::AFRICA_CASABLANCA,              "Africa/Casablanca",              "Morocco",          "<+01>-1",                         60,   0 },
    {Id::ASIA_DUBAI,                     "Asia/Dubai",                     "Dubai",            "<+04>-4",                         240,  0 },
    {Id::ASIA_KARACHI,                   "Asia/Karachi",                   "Pakistan",         "PKT-5",                           300,  0 },
    {Id::ASIA_BANGKOK,                   "Asia/Bangkok",                   "Thailand",         "<+07>-7",                         420,  0 },
    {Id::ASIA_SHANGHAI,                  "Asia/Shanghai",                  "China",            "CST-8",                           480,  0 },
    {Id::ASIA_SEOUL,                     "Asia/Seoul",                     "Korea",            "KST-9",                           540,  0 },
    {Id::ASIA_KATHMANDU,                 "Asia/Kathmandu",                 "Nepal",            "<+0545>-5:45",                    345,  0 },
    {Id::ASIA_JERUSALEM,                 "Asia/Jerusalem",                 "Israel",           "IST-2IDT,M3.4.4/26,M10.5.0",      120,  60},
    {Id::ASIA_TEHRAN,                    "Asia/Tehran",                    "Iran",             "<+0330>-3:30",                    210,  0 },
    {Id::ASIA_JAKARTA,                   "Asia/Jakarta",                   "Indonesia",        "WIB-7",                           420,  0 },
    {Id::AUSTRALIA_PERTH,                "Australia/Perth",                "Perth",            "AWST-8",                          480,  0 },
    {Id::AUSTRALIA_BRISBANE,             "Australia/Brisbane",             "Brisbane",         "AEST-10",                         600,  0 },
    {Id::PACIFIC_AUCKLAND,               "Pacific/Auckland",               "New Zealand",      "NZST-12NZDT,M9.5.0,M4.1.0/3",     720,
     60                                                                                                                                   },
};

static_assert(sizeof(ZONES) / sizeof(ZONES[0]) == ZONE_COUNT, "timezone table size mismatch");

struct auto_point_t {
  Id id;
  int16_t latitude_e2;
  int16_t longitude_e2;
};

// Representative points. The first entry for each zone is the zone.tab
// coordinate; additional points keep large countries and coasts from snapping
// to a neighbour. Refresh these with tools/timezone_table.py --zone-tab.
constexpr auto_point_t AUTO_POINTS[] = {
    {Id::EUROPE_BERLIN,                  5250,  1337  },
    {Id::EUROPE_BERLIN,                  4886,  236   },
    {Id::EUROPE_BERLIN,                  4042,  -374  },
    {Id::EUROPE_BERLIN,                  4154,  1215  },
    {Id::EUROPE_BERLIN,                  5223,  468   },
    {Id::EUROPE_BERLIN,                  5921,  1807  },
    {Id::EUROPE_BERLIN,                  5252,  1340  },
    {Id::EUROPE_LONDON,                  5151,  -13   },
    {Id::EUROPE_LONDON,                  5320,  -618  },
    {Id::AMERICA_NEW_YORK,               4071,  -7401 },
    {Id::AMERICA_NEW_YORK,               3375,  -8439 },
    {Id::AMERICA_NEW_YORK,               2576,  -8019 },
    {Id::AMERICA_NEW_YORK,               4236,  -7106 },
    {Id::AMERICA_CHICAGO,                4185,  -8765 },
    {Id::AMERICA_CHICAGO,                3278,  -9680 },
    {Id::AMERICA_CHICAGO,                4498,  -9327 },
    {Id::AMERICA_DENVER,                 3974,  -10498},
    {Id::AMERICA_DENVER,                 4076,  -11189},
    {Id::AMERICA_DENVER,                 3508,  -10665},
    {Id::AMERICA_LOS_ANGELES,            3405,  -11824},
    {Id::AMERICA_LOS_ANGELES,            4761,  -12233},
    {Id::AMERICA_LOS_ANGELES,            3777,  -12242},
    {Id::AUSTRALIA_SYDNEY,               -3387, 15122 },
    {Id::AUSTRALIA_SYDNEY,               -3781, 14496 },
    {Id::ASIA_TOKYO,                     3565,  13974 },
    {Id::ASIA_TOKYO,                     3469,  13550 },
    {Id::ASIA_KOLKATA,                   2253,  8837  },
    {Id::ASIA_KOLKATA,                   2861,  7721  },
    {Id::ASIA_KOLKATA,                   1908,  7288  },
    {Id::EUROPE_ATHENS,                  3797,  2372  },
    {Id::EUROPE_MOSCOW,                  5576,  3762  },
    {Id::EUROPE_MOSCOW,                  5993,  3034  },
    {Id::EUROPE_LISBON,                  3872,  -913  },
    {Id::AMERICA_HALIFAX,                4465,  -6360 },
    {Id::AMERICA_ST_JOHNS,               4757,  -5272 },
    {Id::AMERICA_SAO_PAULO,              -2353, -4662 },
    {Id::AMERICA_SAO_PAULO,              -2289, -4319 },
    {Id::AMERICA_SANTIAGO,               -3345, -7067 },
    {Id::AMERICA_ARGENTINA_BUENOS_AIRES, -3460, -5845 },
    {Id::AMERICA_MEXICO_CITY,            1940,  -9915 },
    {Id::AMERICA_PHOENIX,                3345,  -11207},
    {Id::AMERICA_ANCHORAGE,              6122,  -14990},
    {Id::PACIFIC_HONOLULU,               2131,  -15786},
    {Id::AFRICA_CAIRO,                   3005,  3125  },
    {Id::AFRICA_JOHANNESBURG,            -2625, 2800  },
    {Id::AFRICA_LAGOS,                   645,   340   },
    {Id::AFRICA_NAIROBI,                 -128,  3682  },
    {Id::AFRICA_CASABLANCA,              3365,  -758  },
    {Id::ASIA_DUBAI,                     2530,  5530  },
    {Id::ASIA_KARACHI,                   2487,  6705  },
    {Id::ASIA_BANGKOK,                   1375,  10052 },
    {Id::ASIA_SHANGHAI,                  3123,  12147 },
    {Id::ASIA_SHANGHAI,                  3990,  11640 },
    {Id::ASIA_SEOUL,                     3755,  12697 },
    {Id::ASIA_KATHMANDU,                 2772,  8532  },
    {Id::ASIA_JERUSALEM,                 3178,  3522  },
    {Id::ASIA_TEHRAN,                    3567,  5143  },
    {Id::ASIA_JAKARTA,                   -617,  10680 },
    {Id::AUSTRALIA_PERTH,                -3195, 11585 },
    {Id::AUSTRALIA_BRISBANE,             -2747, 15303 },
    {Id::PACIFIC_AUCKLAND,               -3687, 17477 },
};

constexpr double PI = 3.14159265358979323846;

std::mutex g_Mutex;
bool g_Applied = false;
Id g_Selection = Id::UTC;
Id g_Current = Id::UTC;
Id g_AutoZone = Id::UTC;
uint32_t g_AutoSeenMs = 0;
uint32_t g_AutoChangedMs = 0;
bool g_AutoValid = false;

const zone_t *zoneLocked(Id id) {
  const size_t index = static_cast<size_t>(id);
  return index < ZONE_COUNT ? &ZONES[index] : nullptr;
}

bool setZoneLocked(Id id) {
  const zone_t *entry = zoneLocked(id);
  if (entry == nullptr) {
    return false;
  }
  if (setenv("TZ", entry->posix, 1) != 0) {
    return false;
  }
  tzset();
  g_Current = id;
  g_Applied = true;
  return true;
}

Id effectiveLocked(void) {
  if (g_Selection == Id::AUTO) {
    return g_AutoValid ? g_AutoZone : Id::UTC;
  }
  return g_Selection;
}

bool refreshLocked(void) {
  const Id effective = effectiveLocked();
  if (g_Applied && (effective == g_Current)) {
    return true;
  }
  return setZoneLocked(effective);
}

void ensureAppliedLocked(void) {
  if (!g_Applied) {
    (void)setZoneLocked(Id::UTC);
  }
}

}  // namespace

const zone_t *zones(void) {
  return ZONES;
}

const zone_t *zone(Id id) {
  return zoneLocked(id);
}

Id clamp(uint8_t value) {
  return value < SELECTION_COUNT ? static_cast<Id>(value) : Id::UTC;
}

bool find(const char *text, Id &id) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }

  if (strcasecmp(text, "auto") == 0 || strcasecmp(text, "gps") == 0) {
    id = Id::AUTO;
    return true;
  }

  char *end = nullptr;
  const unsigned long numeric = strtoul(text, &end, 0);
  if ((end != text) && (*end == '\0') && (numeric < SELECTION_COUNT)) {
    id = static_cast<Id>(numeric);
    return true;
  }

  for (const auto &entry : ZONES) {
    if ((strcasecmp(text, entry.name) == 0) || (strcasecmp(text, entry.label) == 0)
        || (strcasecmp(text, entry.posix) == 0)) {
      id = entry.id;
      return true;
    }
  }
  return false;
}

const char *label(Id id) {
  if (id == Id::AUTO) {
    return "Auto (GPS)";
  }
  const zone_t *entry = zoneLocked(id);
  return entry != nullptr ? entry->label : "UTC";
}

const char *name(Id id) {
  if (id == Id::AUTO) {
    return "Auto (GPS)";
  }
  const zone_t *entry = zoneLocked(id);
  return entry != nullptr ? entry->name : "UTC";
}

const char *tzdataVersion(void) {
  return TZDATA_REVIEWED;
}

std::string labels(void) {
  std::string result;
  for (size_t index = 0; index < ZONE_COUNT; index++) {
    if (!result.empty()) {
      result += '\n';
    }
    result += ZONES[index].label;
  }
  result += '\n';
  result += label(Id::AUTO);
  return result;
}

bool apply(Id id) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  if (static_cast<size_t>(id) >= SELECTION_COUNT) {
    return false;
  }
  g_Selection = id;
  if (id != Id::AUTO) {
    // A manual choice wins and discards stale GPS state, so switching back to
    // AUTO starts from UTC until the next fresh fix.
    g_AutoValid = false;
    g_AutoZone = Id::UTC;
    g_AutoSeenMs = 0;
    g_AutoChangedMs = 0;
  }
  return refreshLocked();
}

Id selection(void) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  return g_Selection;
}

Id current(void) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  return g_Current;
}

Source source(void) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  if (g_Selection != Id::AUTO) {
    return Source::MANUAL;
  }
  return g_AutoValid ? Source::GPS : Source::UTC;
}

Id derived(void) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  return g_AutoValid ? g_AutoZone : Id::UTC;
}

bool nearest(double latitude, double longitude, Id &id) {
  if (!std::isfinite(latitude) || !std::isfinite(longitude) || (latitude < -90.0)
      || (latitude > 90.0) || (longitude < -180.0) || (longitude > 180.0)) {
    return false;
  }

  double best = AUTO_MAX_DISTANCE_DEGREES * AUTO_MAX_DISTANCE_DEGREES;
  Id bestId = Id::UTC;
  bool found = false;
  for (const auto &point : AUTO_POINTS) {
    const double pointLat = point.latitude_e2 / 100.0;
    const double pointLon = point.longitude_e2 / 100.0;
    double deltaLon = longitude - pointLon;
    if (deltaLon > 180.0) {
      deltaLon -= 360.0;
    } else if (deltaLon < -180.0) {
      deltaLon += 360.0;
    }
    const double midLat = (latitude + pointLat) * 0.5 * PI / 180.0;
    const double scale = std::cos(midLat);
    const double deltaLat = latitude - pointLat;
    const double distance = (deltaLat * deltaLat) + (deltaLon * scale * deltaLon * scale);
    if (distance < best) {
      best = distance;
      bestId = point.id;
      found = true;
    }
  }
  if (found) {
    id = bestId;
  }
  return found;
}

bool updateAuto(double latitude, double longitude, uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  if (g_Selection != Id::AUTO) {
    return false;
  }

  Id candidate = Id::UTC;
  if (!nearest(latitude, longitude, candidate)) {
    // A fresh fix outside the snap radius must fall back to UTC immediately.
    // Keeping the previous derived zone would apply a zone the current
    // location is not in until the retention window expires.
    if (!g_AutoValid) {
      return false;
    }
    g_AutoValid = false;
    g_AutoZone = Id::UTC;
    g_AutoSeenMs = now_ms;
    return refreshLocked();
  }

  if (g_AutoValid && (candidate != g_AutoZone)
      && ((now_ms - g_AutoChangedMs) < AUTO_MIN_UPDATE_MS)) {
    // The fix is fresh but the candidate moved too soon after the last change.
    // Refresh the retention clock and keep the established zone.
    g_AutoSeenMs = now_ms;
    return false;
  }

  const bool changed = !g_AutoValid || (candidate != g_AutoZone);
  g_AutoZone = candidate;
  g_AutoSeenMs = now_ms;
  g_AutoValid = true;
  if (changed) {
    g_AutoChangedMs = now_ms;
  }
  if (!changed && g_Applied) {
    return false;
  }
  return refreshLocked();
}

bool service(uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  if ((g_Selection != Id::AUTO) || !g_AutoValid) {
    return false;
  }
  if ((now_ms - g_AutoSeenMs) <= AUTO_RETAIN_MS) {
    return false;
  }
  g_AutoValid = false;
  g_AutoZone = Id::UTC;
  return refreshLocked();
}

bool toLocal(uint64_t epoch_us, local_t &local) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  ensureAppliedLocked();

  const uint64_t seconds = epoch_us / 1000000ULL;
  const time_t instant = static_cast<time_t>(seconds);
  struct tm civil = {};
  if (localtime_r(&instant, &civil) == nullptr) {
    return false;
  }

  if ((civil.tm_year < 70) || (civil.tm_mon < 0) || (civil.tm_mon > 11) || (civil.tm_mday < 1)
      || (civil.tm_mday > 31) || (civil.tm_hour < 0) || (civil.tm_hour > 23) || (civil.tm_min < 0)
      || (civil.tm_min > 59) || (civil.tm_sec < 0) || (civil.tm_sec > 60)) {
    return false;
  }

  // localtime_r gives the civil fields. Rebuild that tuple as if it were UTC
  // to recover the current offset without relying on non-portable tm_gmtoff.
  const uint32_t local_seconds = UtcTime::epochSeconds(
      static_cast<unsigned int>(civil.tm_year + 1900), static_cast<unsigned int>(civil.tm_mon + 1),
      static_cast<unsigned int>(civil.tm_mday), static_cast<unsigned int>(civil.tm_hour),
      static_cast<unsigned int>(civil.tm_min), static_cast<unsigned int>(civil.tm_sec));
  if (local_seconds == 0) {
    return false;
  }

  const int64_t offset_seconds =
      static_cast<int64_t>(local_seconds) - static_cast<int64_t>(seconds);
  if ((offset_seconds < -(24LL * 60LL * 60LL)) || (offset_seconds > (24LL * 60LL * 60LL))) {
    return false;
  }

  const zone_t *entry = zoneLocked(g_Current);
  local.year = static_cast<uint16_t>(civil.tm_year + 1900);
  local.month = static_cast<uint8_t>(civil.tm_mon + 1);
  local.day = static_cast<uint8_t>(civil.tm_mday);
  local.hour = static_cast<uint8_t>(civil.tm_hour);
  local.minute = static_cast<uint8_t>(civil.tm_min);
  local.second = static_cast<uint8_t>(civil.tm_sec);
  local.offset_minutes = static_cast<int16_t>(offset_seconds / 60);
  local.standard_offset_minutes = entry != nullptr ? entry->standard_offset_minutes : 0;
  local.dst_offset_minutes = entry != nullptr ? entry->dst_offset_minutes : 0;
  local.dst = civil.tm_isdst > 0;
  return true;
}

bool toLocal(uint16_t year,
             uint8_t month,
             uint8_t day,
             uint8_t hour,
             uint8_t minute,
             uint8_t second,
             local_t &local) {
  const uint32_t epoch = UtcTime::epochSeconds(year, month, day, hour, minute, second);
  if (epoch == 0) {
    return false;
  }
  return toLocal(static_cast<uint64_t>(epoch) * 1000000ULL, local);
}

}  // namespace Timezone
}  // namespace Furble
