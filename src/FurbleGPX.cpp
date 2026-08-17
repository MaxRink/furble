#include <cerrno>
#include <cstdio>
#include <cstring>

#include <esp_log.h>

#include <sys/stat.h>
#include <unistd.h>

#include "FurbleGPX.h"
#include "FurbleTypes.h"

namespace Furble {
namespace {

constexpr const char *GPX_DIRECTORY = "/sd/furble";
constexpr const char *GPX_HEADER =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<gpx version=\"1.1\" creator=\"furble\"\n"
    "     xmlns=\"http://www.topografix.com/GPX/1/1\"\n"
    "     xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
    "     xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1\n"
    "                         http://www.topografix.com/GPX/1/1/gpx.xsd\">\n"
    "  <trk><trkseg>";
constexpr const char *GPX_CLOSERS = "\n  </trkseg></trk>\n</gpx>\n";
constexpr const char *GPX_SEGMENT_BREAK = "\n  </trkseg>\n  <trkseg>";
constexpr uint16_t FSYNC_PERIOD_SECONDS = 10;
constexpr uint32_t FSYNC_EVERY_POINTS = 5;
constexpr int64_t SEGMENT_GAP_SECONDS = 30;

}  // namespace

GPX &GPX::getInstance(void) {
  static GPX instance;
  return instance;
}

bool GPX::isOpen(void) const {
  return m_File != nullptr;
}

/** Days from civil algorithm, Howard Hinnant, public domain. */
int64_t GPX::pointSeconds(const point_t &point) {
  int64_t y = point.year;
  const int64_t m = point.month;
  const int64_t d = point.day;
  y -= (m <= 2) ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + doe - 719468;
  return ((days * 24 + point.hour) * 60 + point.minute) * 60 + point.second;
}

void GPX::closeFile(void) {
  if (m_File != nullptr) {
    fclose(static_cast<FILE *>(m_File));
    m_File = nullptr;
  }
  m_CloserOffset = 0;
  m_Points = 0;
  m_LastPointSeconds = 0;
}

bool GPX::flush(bool durable) {
  auto *file = static_cast<FILE *>(m_File);
  if ((file == nullptr) || (fflush(file) != 0)) {
    return false;
  }

  return !durable || (fsync(fileno(file)) == 0);
}

bool GPX::writeClosers(void) {
  auto *file = static_cast<FILE *>(m_File);
  if ((file == nullptr) || (fputs(GPX_CLOSERS, file) == EOF) || (fflush(file) != 0)) {
    return false;
  }

  const long end = ftell(file);
  return (end >= 0) && (ftruncate(fileno(file), end) == 0);
}

void GPX::fail(const char *operation) {
  ESP_LOGE(LOG_TAG, "GPX %s failed: %s", operation, strerror(errno));
  closeFile();
}

bool GPX::open(const point_t &point) {
  if ((mkdir(GPX_DIRECTORY, 0777) != 0) && (errno != EEXIST)) {
    fail("create directory");
    return false;
  }

  char path[64];
  snprintf(path, sizeof(path), "%s/%04u%02u%02u-%02u%02u%02u.gpx", GPX_DIRECTORY,
           static_cast<unsigned>(point.year), static_cast<unsigned>(point.month),
           static_cast<unsigned>(point.day), static_cast<unsigned>(point.hour),
           static_cast<unsigned>(point.minute), static_cast<unsigned>(point.second));

  auto *file = fopen(path, "w");
  if (file == nullptr) {
    fail("open file");
    return false;
  }

  m_File = file;
  m_Points = 0;
  m_LastPointSeconds = 0;
  if ((fputs(GPX_HEADER, file) == EOF) || ((m_CloserOffset = ftell(file)) < 0) || !writeClosers()
      || !flush(true)) {
    fail("write header");
    return false;
  }

  ESP_LOGI(LOG_TAG, "GPX logging to %s", path);
  return true;
}

bool GPX::addPoint(const point_t &point, uint16_t periodSeconds) {
  if (!isOpen() && !open(point)) {
    return false;
  }

  auto *file = static_cast<FILE *>(m_File);
  if (fseek(file, m_CloserOffset, SEEK_SET) != 0) {
    fail("seek point");
    return false;
  }

  // a long gap or a backwards time step means the receiver was off or lost
  // its clock, start a fresh track segment either way
  const int64_t seconds = pointSeconds(point);
  const int64_t delta = seconds - m_LastPointSeconds;
  if ((m_LastPointSeconds != 0) && ((delta > SEGMENT_GAP_SECONDS) || (delta < 0))
      && (fputs(GPX_SEGMENT_BREAK, file) == EOF)) {
    fail("write segment");
    return false;
  }

  if (fprintf(file, "\n    <trkpt lat=\"%.6f\" lon=\"%.6f\">\n", point.latitude, point.longitude)
      < 0) {
    fail("write point");
    return false;
  }
  if (point.altitude_valid && (fprintf(file, "      <ele>%.1f</ele>\n", point.altitude) < 0)) {
    fail("write point");
    return false;
  }
  if (fprintf(file,
              "      <time>%04u-%02u-%02uT%02u:%02u:%02uZ</time>\n"
              "      <sat>%lu</sat>\n"
              "    </trkpt>",
              static_cast<unsigned>(point.year), static_cast<unsigned>(point.month),
              static_cast<unsigned>(point.day), static_cast<unsigned>(point.hour),
              static_cast<unsigned>(point.minute), static_cast<unsigned>(point.second),
              static_cast<unsigned long>(point.satellites))
      < 0) {
    fail("write point");
    return false;
  }

  const long closerOffset = ftell(file);
  if ((closerOffset < 0) || !writeClosers()) {
    fail("locate closers");
    return false;
  }
  m_CloserOffset = closerOffset;

  m_Points++;
  m_LastPointSeconds = seconds;
  const bool durable =
      (periodSeconds >= FSYNC_PERIOD_SECONDS) || ((m_Points % FSYNC_EVERY_POINTS) == 0);
  if (!flush(durable)) {
    fail("flush point");
    return false;
  }

  return true;
}

void GPX::close(void) {
  if (!isOpen()) {
    return;
  }

  if ((fseek(static_cast<FILE *>(m_File), m_CloserOffset, SEEK_SET) != 0) || !writeClosers()
      || !flush(true)) {
    ESP_LOGE(LOG_TAG, "GPX close failed: %s", strerror(errno));
  }
  closeFile();
}

}  // namespace Furble
