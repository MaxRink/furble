// Host unit test for the GPS Data page sentence age formatter.
//
// The GPS Data page renders the time since the receiver last spoke. That value
// is unbounded: nothing resets the last sentence tick while a receiver is
// quiet, so an unplugged unit counts up for as long as furble runs. The row it
// lands in is budgeted to fourteen characters, because a wider centred row
// slides under the floating navigation indicator on the non-touch Stick layout,
// and the row is "uart nmea " plus this field. A plain seconds count crosses
// that after about seventeen minutes, and again at 3.4 hours, which is exactly
// when a quiet receiver is worth reading.
//
// The simulator cannot reach these ages. The degraded retry path re-sends its
// configuration, the fake receiver answers, and any received byte refreshes the
// tick, so a scripted scenario never renders more than about twenty seconds.
// That is why the formatter is a pure header with a unit test rather than a
// scenario assertion, following FurbleGPSPowerCycle.h.
//
// Mutation checks (the test's teeth):
//   * Drop the minutes branch, so the seconds count runs on: testWidthIsBounded
//     fails at 1000 seconds.
//   * Drop the saturating branch: testWidthIsBounded fails at 100 minutes.
//   * Move the seconds cutoff to 1000: testWidthIsBounded fails, the field
//     becomes five characters wide.
//   * Round minutes instead of truncating: testMinutesTruncate fails.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "FurbleGPSFormat.h"

using Furble::gpsSentenceAge;

namespace {

// The row is "<source> nmea <age>", four characters of source, a space, four of
// "nmea", a space. Fourteen total leaves four for the age field.
constexpr size_t MAX_AGE_CHARS = 4;

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

// Format one age and return it in a fresh buffer, so a short write cannot hide
// behind a previous call's bytes.
std::string format(uint32_t age_ms) {
  char out[16];
  std::memset(out, '?', sizeof(out));
  gpsSentenceAge(out, 5, age_ms);
  return std::string(out);
}

void checkFormat(uint32_t age_ms, const char *expected, const char *message) {
  const std::string actual = format(age_ms);
  if (actual != expected) {
    std::cerr << "  FAIL: " << message << " (" << age_ms << " ms rendered \"" << actual
              << "\", expected \"" << expected << "\")\n";
    g_Failures++;
  }
}

// Seconds below the minute cutoff, including both ends of the range.
void testSeconds(void) {
  std::cerr << "test: an age under 100 seconds renders in seconds\n";
  checkFormat(0, "0s", "a fresh sentence");
  checkFormat(999, "0s", "a sub-second age truncates to zero");
  checkFormat(1000, "1s", "one second");
  checkFormat(42 * 1000, "42s", "a mid range age");
  checkFormat(99 * 1000, "99s", "the last second before the minute cutoff");
}

// The cutoff itself, and truncation rather than rounding on the way up.
void testMinutesTruncate(void) {
  std::cerr << "test: an age of 100 seconds and over renders in whole minutes\n";
  checkFormat(100 * 1000, "1m", "the first age past the seconds cutoff");
  checkFormat(119 * 1000, "1m", "119 seconds truncates down, it does not round to 2m");
  checkFormat(120 * 1000, "2m", "two whole minutes");
  checkFormat(3600 * 1000, "60m", "an hour");
  checkFormat(5940 * 1000, "99m", "the last minute before saturation");
}

// Saturation, so the field can never grow a fifth character.
void testSaturates(void) {
  std::cerr << "test: an age of 100 minutes and over saturates\n";
  checkFormat(6000 * 1000, "99m+", "the first age past the minutes cutoff");
  checkFormat(24 * 3600 * 1000UL, "99m+", "a day");
  checkFormat(UINT32_MAX, "99m+", "the largest tick difference representable");
}

// The property the page depends on: this field is never wider than the budget,
// at any age. The field is the only unbounded part of the row.
void testWidthIsBounded(void) {
  std::cerr << "test: no age renders wider than the row budget\n";
  // Every second of the first three hours, then a coarse sweep to the maximum.
  for (uint32_t seconds = 0; seconds <= 3 * 3600; seconds++) {
    const std::string rendered = format(seconds * 1000);
    if (rendered.size() > MAX_AGE_CHARS) {
      std::cerr << "  FAIL: " << seconds << " s rendered \"" << rendered << "\", "
                << rendered.size() << " characters\n";
      g_Failures++;
      return;
    }
  }
  for (uint32_t seconds = 3 * 3600; seconds < UINT32_MAX / 1000; seconds += 3607) {
    const std::string rendered = format(seconds * 1000);
    if (rendered.size() > MAX_AGE_CHARS) {
      std::cerr << "  FAIL: " << seconds << " s rendered \"" << rendered << "\", "
                << rendered.size() << " characters\n";
      g_Failures++;
      return;
    }
  }
  check(true, "the sweep completed");
}

// A caller that hands over a short buffer must be truncated, not overrun.
void testShortBufferTruncates(void) {
  std::cerr << "test: a short buffer truncates rather than overruns\n";
  char out[8];
  std::memset(out, '?', sizeof(out));
  gpsSentenceAge(out, 3, 6000 * 1000);
  check(std::strlen(out) == 2, "a three byte buffer holds two characters and a terminator");
  check(out[3] == '?', "nothing was written past the buffer");
}

}  // namespace

int main(void) {
  testSeconds();
  testMinutesTruncate();
  testSaturates();
  testWidthIsBounded();
  testShortBufferTruncates();

  if (g_Failures > 0) {
    std::cerr << g_Failures << " check(s) failed\n";
    return 1;
  }
  std::cerr << "all GPS sentence age format checks passed\n";
  return 0;
}
