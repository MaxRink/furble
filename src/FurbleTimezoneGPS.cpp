#include "FurbleTimezoneGPS.h"

#include <cmath>

#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "FurbleTimezone.h"
#include "protocol/UtcTime.h"

namespace Furble {
namespace TimezoneGPS {

bool service(void) {
  const uint32_t now_ms = Platform::getInstance().tick();
  auto &gps = GPS::getInstance();

  if (!gps.isEnabled() || (gps.getFix() != GPS::Fix::LIVE)
      || (gps.getSource() != GPS::SOURCE_UART)) {
    return Timezone::service(now_ms);
  }

  const GPS::status_t status = gps.getStatusSnapshot();
  if (!freshWiredFix(gps.isEnabled(), gps.getFix() == GPS::Fix::LIVE,
                     gps.getSource() == GPS::SOURCE_UART, status.position_valid, status.fix,
                     status.date_valid, status.time_valid, status.location_age, status.date_age,
                     status.time_age, GPS::FRESH_FIX_MS)
      || !std::isfinite(status.latitude) || !std::isfinite(status.longitude)) {
    return Timezone::service(now_ms);
  }

  const uint32_t epoch = UtcTime::epochSeconds(status.year, status.month, status.day, status.hour,
                                               status.minute, status.second);
  if (epoch == 0) {
    return Timezone::service(now_ms);
  }

  return Timezone::updateAuto(status.latitude, status.longitude, now_ms);
}

}  // namespace TimezoneGPS
}  // namespace Furble
