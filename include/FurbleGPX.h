#ifndef FURBLE_GPX_H
#define FURBLE_GPX_H

#include <cstdint>

namespace Furble {
class GPX {
 public:
  typedef struct {
    double latitude;
    double longitude;
    double altitude;
    uint32_t satellites;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
  } point_t;

  static GPX &getInstance(void);

  GPX(GPX const &) = delete;
  GPX(GPX &&) = delete;
  GPX &operator=(GPX const &) = delete;
  GPX &operator=(GPX &&) = delete;

  /** Append a fix to the current session file. */
  bool addPoint(const point_t &point);

  /** Close the current track with valid GPX closing tags. */
  void close(void);

  /** Return whether a track file is open. */
  bool isOpen(void) const;

 private:
  GPX() = default;

  bool open(const point_t &point);
  bool writeClosers(void);
  bool flush(bool durable);
  void fail(const char *operation);
  void closeFile(void);

  void *m_File = nullptr;
  long m_CloserOffset = 0;
  uint32_t m_Points = 0;
  uint8_t m_Failures = 0;
};
}  // namespace Furble

#endif
