#ifndef FURBLE_GPX_H
#define FURBLE_GPX_H

#include <cstdint>

namespace Furble {
/**
 * GPX 1.1 track file writer.
 *
 * Pure file writer with no SD or settings knowledge. Every method must run on
 * the SD writer task, which owns the file handle and the card mount state.
 */
class GPX {
 public:
  typedef struct {
    double latitude;
    double longitude;
    double altitude;
    bool altitude_valid;
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

  /**
   * Append a fix to the current session file.
   *
   * Opens the session file on the first point. The period drives the fsync
   * cadence and must already be clamped to the valid range. On failure the
   * file is closed and the caller decides recovery.
   */
  bool addPoint(const point_t &point, uint16_t periodSeconds);

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

  /** Convert a point timestamp to seconds since the civil epoch. */
  static int64_t pointSeconds(const point_t &point);

  void *m_File = nullptr;
  long m_CloserOffset = 0;
  uint32_t m_Points = 0;
  int64_t m_LastPointSeconds = 0;
};
}  // namespace Furble

#endif
