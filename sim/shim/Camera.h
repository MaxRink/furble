#ifndef FURBLE_SIM_CAMERA_H
#define FURBLE_SIM_CAMERA_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <esp_bt.h>
#include <freertos/FreeRTOS.h>

#include <FurbleTypes.h>

namespace Furble {

class Camera {
 public:
  enum class Type : uint32_t {
    FAUXNY = 5,
  };

  enum class PairType : uint8_t {
    NEW = 1,
    SAVED = 2,
  };

  typedef struct _gps_t {
    double latitude;
    double longitude;
    double altitude;
    unsigned int satellites;
  } gps_t;

  typedef struct _timesync_t {
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    unsigned int centisecond;
  } timesync_t;

  explicit Camera(std::string name = "FauxNY") : m_Name {std::move(name)} {}
  virtual ~Camera() = default;

  bool connect(esp_power_level_t power, uint32_t timeout);
  void disconnect(void);
  bool isConnected(void) const;
  bool isActive(void) const;
  void setActive(bool active);
  const Type &getType(void) const;
  const std::string &getName(void) const;
  uint8_t getConnectProgress(void) const;

  void setConnectProgress(uint8_t progress);
  void shutterPress(void);
  void shutterRelease(void);
  void focusPress(void);
  void focusRelease(void);
  void updateGeoData(const gps_t &gps, const timesync_t &timesync);

 private:
  Type m_Type = Type::FAUXNY;
  std::string m_Name;
  std::atomic<uint8_t> m_Progress {0};
  bool m_Active = false;
  bool m_Connected = false;
};

}  // namespace Furble

#endif
