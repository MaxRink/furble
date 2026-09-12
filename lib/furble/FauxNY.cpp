#include <esp_random.h>

#if defined(FURBLE_SIM)
#include <mutex>
#endif

#include "FauxNY.h"

namespace Furble {

FauxNY::FauxNY(const void *data, size_t len) : Camera(Type::FAUXNY, PairType::SAVED) {
  if (len != sizeof(fauxNY_t)) {
    abort();
  }

  const auto *fauxNY = static_cast<const fauxNY_t *>(data);
  m_Name = std::string(fauxNY->name);
  m_ID = fauxNY->id;
  m_Address = NimBLEAddress(m_ID, 0);
}

FauxNY::FauxNY(void) : Camera(Type::FAUXNY, PairType::NEW) {
  m_ID = esp_random();
  m_Name = std::string("FauxNY-") + std::to_string(m_ID % 42);
  m_Address = NimBLEAddress(m_ID, 0);
}

bool FauxNY::matches(void) {
  return true;
}

bool FauxNY::_connect(void) {
  ESP_LOGI(m_FauxNYStr, "Connecting");
  m_Progress = 0;

  for (int i = 0; i < 100; i += 1) {
    // Poll the cancel token like every vendor connect does. Camera::connect()
    // holds m_Mutex for the whole attempt, so a disconnect arriving during one
    // blocks the target task's Camera::disconnect() behind this loop until it
    // unwinds. Without the poll that is the full attempt (the plan 148 wedge);
    // with it, one 25 ms slice.
    if (connectCancelled()) {
      ESP_LOGW(m_FauxNYStr, "Connect cancelled");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(25));
    m_Progress = i;
  }

  m_Connected = true;
  m_Progress = 100;

  return true;
}

void FauxNY::shutterPress(void) {
  ESP_LOGI(m_FauxNYStr, "shutterPress()");
}

void FauxNY::shutterRelease(void) {
  ESP_LOGI(m_FauxNYStr, "shutterRelease()");
}

void FauxNY::focusPress(void) {
  ESP_LOGI(m_FauxNYStr, "focusPress()");
}

void FauxNY::focusRelease(void) {
  ESP_LOGI(m_FauxNYStr, "focusRelease()");
}

#if defined(FURBLE_SIM)
namespace {
std::mutex geoMutex;
FauxNY::geo_record_t geoRecord = {};
}  // namespace

FauxNY::geo_record_t FauxNY::getGeoRecord(void) {
  const std::lock_guard<std::mutex> lock(geoMutex);
  return geoRecord;
}
#endif

void FauxNY::updateGeoData(const gps_t &gps, const timesync_t &timesync) {
  ESP_LOGI(m_FauxNYStr, "updateGeoData()");
#if defined(FURBLE_SIM)
  const std::lock_guard<std::mutex> lock(geoMutex);
  geoRecord.count++;
  geoRecord.latitude = gps.latitude;
  geoRecord.longitude = gps.longitude;
  geoRecord.hour = timesync.hour;
  geoRecord.minute = timesync.minute;
  geoRecord.second = timesync.second;
#endif
};

void FauxNY::_disconnect(void) {
  ESP_LOGI(m_FauxNYStr, "Disconnecting");
  m_Connected = false;
}

size_t FauxNY::getSerialisedBytes(void) const {
  return sizeof(fauxNY_t);
}

bool FauxNY::serialise(void *buffer, size_t bytes) const {
  if (bytes != sizeof(fauxNY_t)) {
    return false;
  }

  auto *fauxNY = static_cast<fauxNY_t *>(buffer);
  strncpy(fauxNY->name, m_Name.c_str(), MAX_NAME);
  fauxNY->address = (uint64_t)m_Address;
  fauxNY->type = m_Address.getType();
  fauxNY->id = m_ID;

  return true;
}

}  // namespace Furble
