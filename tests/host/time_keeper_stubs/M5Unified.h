#ifndef FURBLE_HOST_TIME_KEEPER_M5UNIFIED_H
#define FURBLE_HOST_TIME_KEEPER_M5UNIFIED_H

#include <cstdint>
#include <ctime>

namespace m5 {

enum class board_t {
  board_M5Stack,
  board_M5StackCore2,
  board_M5StickC,
  board_M5StickCPlus,
  board_M5StickS3,
};

struct rtc_date_t {
  int year = 0;
  int month = 0;
  int date = 0;
};

struct rtc_time_t {
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
};

struct rtc_datetime_t {
  rtc_date_t date;
  rtc_time_t time;

  rtc_datetime_t() = default;
  explicit rtc_datetime_t(const tm &utc)
      : date {utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday},
        time {utc.tm_hour, utc.tm_min, utc.tm_sec} {}
};

class TestRtc {
 public:
  bool enabled = false;
  bool voltageLow = false;
  rtc_datetime_t value;
  uint32_t writes = 0;

  bool isEnabled() const { return enabled; }
  bool getDateTime(rtc_datetime_t *out) const {
    if (!enabled || out == nullptr) {
      return false;
    }
    *out = value;
    return true;
  }
  bool getVoltLow() const { return voltageLow; }
  void setDateTime(const rtc_datetime_t *in) {
    if (in != nullptr) {
      value = *in;
      writes++;
    }
  }
};

class TestM5 {
 public:
  TestRtc Rtc;
  board_t board = board_t::board_M5StickS3;

  board_t getBoard() const { return board; }
};

inline TestM5 M5;

}  // namespace m5

using m5::M5;

#endif
