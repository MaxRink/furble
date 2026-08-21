// Host tests for the GPX 1.1 track writer in src/FurbleGPX.cpp.
//
// The writer is a pure file writer with no SD or settings knowledge, so it
// runs on the host once its mount point is redirected to a writable sandbox.
// FurbleGPX.cpp is compiled with -DFURBLE_GPX_DIRECTORY pointing at that
// sandbox, and this test reads the same macro to find the files it produced.
//
// The invariants under test are the ones that matter on a card that can lose
// power at any moment:
//   - Every point is written in schema order: ele, then time, then sat.
//   - The closing tags are on disk after every point, so the file at rest is
//     always a valid, closed GPX document even before close() is called.
//   - A gap over the segment threshold or a backwards time step starts a fresh
//     track segment.
//   - close() leaves exactly one set of closing tags.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "FurbleGPX.h"

// FurbleGPX.cpp references LOG_TAG through FurbleTypes.h.
const char *LOG_TAG = "furble-test";

namespace fs = std::filesystem;

namespace {

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

size_t count(const std::string &haystack, const std::string &needle) {
  size_t total = 0;
  size_t pos = haystack.find(needle);
  while (pos != std::string::npos) {
    total++;
    pos = haystack.find(needle, pos + needle.size());
  }
  return total;
}

Furble::GPX::point_t makePoint(uint16_t year,
                               uint8_t month,
                               uint8_t day,
                               uint8_t hour,
                               uint8_t minute,
                               uint8_t second) {
  Furble::GPX::point_t point = {};
  point.latitude = -34.928500;
  point.longitude = 138.600700;
  point.altitude = 50.0;
  point.altitude_valid = true;
  point.satellites = 9;
  point.year = year;
  point.month = month;
  point.day = day;
  point.hour = hour;
  point.minute = minute;
  point.second = second;
  return point;
}

std::string pathFor(const Furble::GPX::point_t &point) {
  char name[64];
  snprintf(name, sizeof(name), "%s/%04u%02u%02u-%02u%02u%02u.gpx", FURBLE_GPX_DIRECTORY,
           static_cast<unsigned>(point.year), static_cast<unsigned>(point.month),
           static_cast<unsigned>(point.day), static_cast<unsigned>(point.hour),
           static_cast<unsigned>(point.minute), static_cast<unsigned>(point.second));
  return std::string(name);
}

// The writer is a singleton, so close between scenarios and clear the sandbox
// so a stale file cannot mask a regression.
void reset(void) {
  auto &gpx = Furble::GPX::getInstance();
  gpx.close();
  std::error_code ec;
  fs::remove_all(FURBLE_GPX_DIRECTORY, ec);
}

// One point produces a valid, closed document with the trkpt fields in schema
// order and the closing tags already on disk.
void testSinglePoint(void) {
  reset();
  auto &gpx = Furble::GPX::getInstance();
  const auto point = makePoint(2026, 8, 16, 1, 23, 45);

  check(gpx.addPoint(point, 5), "addPoint should succeed");
  check(gpx.isOpen(), "writer should be open after a point");

  const std::string text = readText(pathFor(point));
  check(text.find("<gpx version=\"1.1\" creator=\"furble\"") != std::string::npos,
        "header must be present");
  check(text.find("<trkpt lat=\"-34.928500\" lon=\"138.600700\">") != std::string::npos,
        "trkpt must carry six decimal coordinates");

  const size_t ele = text.find("<ele>");
  const size_t time = text.find("<time>");
  const size_t sat = text.find("<sat>");
  check(ele != std::string::npos && time != std::string::npos && sat != std::string::npos,
        "ele, time and sat must all be present");
  check(ele < time && time < sat, "trkpt fields must be in schema order: ele, time, sat");
  check(text.find("<time>2026-08-16T01:23:45Z</time>") != std::string::npos,
        "time must be UTC in the GPX format");

  // The rewound closer trick keeps the file valid at rest before close.
  check(count(text, "</gpx>") == 1, "exactly one closing gpx tag at rest");
  const std::string closers = "\n  </trkseg></trk>\n</gpx>\n";
  check(text.size() >= closers.size()
            && text.compare(text.size() - closers.size(), closers.size(), closers) == 0,
        "file at rest must end with the GPX closing tags");
}

// close() must not duplicate the closing tags.
void testCloseIsClean(void) {
  reset();
  auto &gpx = Furble::GPX::getInstance();
  const auto point = makePoint(2026, 8, 16, 2, 0, 0);
  check(gpx.addPoint(point, 5), "addPoint should succeed");
  gpx.close();
  check(!gpx.isOpen(), "writer should be closed after close");

  const std::string text = readText(pathFor(point));
  check(count(text, "</gpx>") == 1, "close must leave exactly one closing gpx tag");
  check(count(text, "</trkseg>") == 1, "close must leave exactly one trkseg close");
}

// A missing altitude omits the ele element entirely.
void testNoAltitude(void) {
  reset();
  auto &gpx = Furble::GPX::getInstance();
  auto point = makePoint(2026, 8, 16, 3, 0, 0);
  point.altitude_valid = false;
  check(gpx.addPoint(point, 5), "addPoint without altitude should succeed");

  const std::string text = readText(pathFor(point));
  check(text.find("<ele>") == std::string::npos, "ele must be omitted when altitude is invalid");
  check(text.find("<sat>") != std::string::npos, "sat must still be present");
}

// Points close in time share one segment; a long gap or a backwards step opens
// a new one.
void testSegmentBreaks(void) {
  reset();
  auto &gpx = Furble::GPX::getInstance();
  const auto first = makePoint(2026, 8, 16, 4, 0, 0);

  check(gpx.addPoint(first, 5), "first point should succeed");
  check(gpx.addPoint(makePoint(2026, 8, 16, 4, 0, 5), 5), "close point should succeed");
  // 40 s gap is over the 30 s threshold, so a fresh segment opens.
  check(gpx.addPoint(makePoint(2026, 8, 16, 4, 0, 45), 5), "gap point should succeed");
  // A backwards time step also opens a fresh segment.
  check(gpx.addPoint(makePoint(2026, 8, 16, 4, 0, 30), 5), "backwards point should succeed");

  const std::string text = readText(pathFor(first));
  // Header opens the first trkseg, then two breaks open two more.
  check(count(text, "<trkseg>") == 3, "a gap and a backwards step must open two extra segments");
  check(count(text, "<trkpt ") == 4, "all four points must be written");
  check(count(text, "</gpx>") == 1, "still exactly one closing gpx tag at rest");
}

}  // namespace

int main(void) {
  testSinglePoint();
  testCloseIsClean();
  testNoAltitude();
  testSegmentBreaks();

  // Leave the sandbox clean.
  std::error_code ec;
  fs::remove_all(FURBLE_GPX_DIRECTORY, ec);

  if (g_failures > 0) {
    std::cerr << g_failures << " GPX writer check(s) failed\n";
    return 1;
  }
  std::cout << "GPX writer tests passed\n";
  return 0;
}
