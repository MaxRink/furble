#ifndef FURBLE_TIMEZONE_H
#define FURBLE_TIMEZONE_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace Furble {

/**
 * Local civil time conversion for the user-selected time zone.
 *
 * The wall clock stays UTC. A zone is applied to the C library with a POSIX TZ
 * string so localtime_r() can derive the civil time and the current UTC offset
 * without shipping tzdata files. Keep the table in FurbleTimezone.cpp; every
 * entry is one POSIX TZ string copied from the IANA tz database.
 */
namespace Timezone {

enum class Id : uint8_t {
  UTC = 0,
  EUROPE_BERLIN,
  EUROPE_LONDON,
  AMERICA_NEW_YORK,
  AMERICA_CHICAGO,
  AMERICA_DENVER,
  AMERICA_LOS_ANGELES,
  AUSTRALIA_SYDNEY,
  ASIA_TOKYO,
  ASIA_KOLKATA,
  EUROPE_ATHENS,
  EUROPE_MOSCOW,
  EUROPE_LISBON,
  AMERICA_HALIFAX,
  AMERICA_ST_JOHNS,
  AMERICA_SAO_PAULO,
  AMERICA_SANTIAGO,
  AMERICA_ARGENTINA_BUENOS_AIRES,
  AMERICA_MEXICO_CITY,
  AMERICA_PHOENIX,
  AMERICA_ANCHORAGE,
  PACIFIC_HONOLULU,
  AFRICA_CAIRO,
  AFRICA_JOHANNESBURG,
  AFRICA_LAGOS,
  AFRICA_NAIROBI,
  AFRICA_CASABLANCA,
  ASIA_DUBAI,
  ASIA_KARACHI,
  ASIA_BANGKOK,
  ASIA_SHANGHAI,
  ASIA_SEOUL,
  ASIA_KATHMANDU,
  ASIA_JERUSALEM,
  ASIA_TEHRAN,
  ASIA_JAKARTA,
  AUSTRALIA_PERTH,
  AUSTRALIA_BRISBANE,
  PACIFIC_AUCKLAND,
  /** Use the zone derived from a fresh wired GPS fix, or UTC without one. */
  AUTO,
  COUNT,
};

/** Where the effective local zone came from. */
enum class Source : uint8_t {
  MANUAL,
  GPS,
  UTC,
};

struct zone_t {
  Id id;
  /** IANA zone name, used by the console and diagnostics. */
  const char *name;
  /** Short label used by the on-device rollers. */
  const char *label;
  /** POSIX TZ string passed to setenv("TZ", ...). */
  const char *posix;
  /** Standard offset from UTC in minutes, east positive. */
  int16_t standard_offset_minutes;
  /** DST shift in minutes, zero when the zone has no DST. */
  uint8_t dst_offset_minutes;
};

/** A local calendar value with the offsets of the zone at that instant. */
struct local_t {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  /** Current offset from UTC in minutes, east positive, including DST. */
  int16_t offset_minutes;
  /** Standard offset from UTC in minutes, east positive. */
  int16_t standard_offset_minutes;
  /** DST shift in minutes for the zone, zero when it has no DST. */
  uint8_t dst_offset_minutes;
  /** True while daylight saving time is in effect. */
  bool dst;
};

constexpr size_t ZONE_COUNT = static_cast<size_t>(Id::AUTO);
constexpr size_t SELECTION_COUNT = static_cast<size_t>(Id::COUNT);

/** Nearest-zone match radius. Beyond this the auto mode falls back to UTC. */
constexpr double AUTO_MAX_DISTANCE_DEGREES = 30.0;
/** Minimum time between automatic zone changes. */
constexpr uint32_t AUTO_MIN_UPDATE_MS = 5U * 60U * 1000U;
/** How long the last derived zone survives without a fresh fix. */
constexpr uint32_t AUTO_RETAIN_MS = 30U * 60U * 1000U;

/** The curated zone table, in roller and console index order. */
const zone_t *zones(void);

/** Look up one table row. Returns nullptr for AUTO and out-of-range ids. */
const zone_t *zone(Id id);

/** Map a stored value to a valid selection, falling back to UTC. */
Id clamp(uint8_t value);

/** Map a name, IANA name, label, numeric index, or "auto" to an id. */
bool find(const char *text, Id &id);

/** Display label for a roller. */
const char *label(Id id);

/** IANA name for logs and the console. */
const char *name(Id id);

/** IANA tzdata release the POSIX strings were last reviewed against. */
const char *tzdataVersion(void);

/** Newline separated roller options, including Auto (GPS) last. */
std::string labels(void);

/**
 * Apply a manual selection, or AUTO.
 *
 * AUTO uses the last derived zone while it is within AUTO_RETAIN_MS and falls
 * back to UTC otherwise. A manual selection always wins over a derived zone.
 */
bool apply(Id id);

/** Return the applied selection. AUTO is a mode, not a table row. */
Id selection(void);

/** Return the zone currently applied to the C library. */
Id current(void);

/** Return where the current zone came from. */
Source source(void);

/** Return the last GPS-derived zone, or UTC before the first derivation. */
Id derived(void);

/**
 * Feed a fresh, wired GPS fix into the automatic selection.
 *
 * Does nothing unless the stored selection is AUTO. A candidate change is held
 * for AUTO_MIN_UPDATE_MS after the previous change. Returns true when the
 * applied zone or the applied POSIX string changed.
 */
bool updateAuto(double latitude, double longitude, uint32_t now_ms);

/** Expire the derived zone after AUTO_RETAIN_MS without a fresh fix. */
bool service(uint32_t now_ms);

/** Nearest curated zone for a coordinate. False when outside the snap radius. */
bool nearest(double latitude, double longitude, Id &id);

/** Convert a UTC Unix time to local civil time in the applied zone. */
bool toLocal(uint64_t epoch_us, local_t &local);

/** Convert a UTC calendar tuple to local civil time in the applied zone. */
bool toLocal(uint16_t year,
             uint8_t month,
             uint8_t day,
             uint8_t hour,
             uint8_t minute,
             uint8_t second,
             local_t &local);

}  // namespace Timezone
}  // namespace Furble

#endif
