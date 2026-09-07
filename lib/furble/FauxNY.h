#ifndef FAUXNY_H
#define FAUXNY_H

#include "Camera.h"
#include "Device.h"

namespace Furble {
/**
 * FauxNY fake virtual camera
 */
class FauxNY: public Camera {
 public:
  FauxNY(const void *data, size_t len);
  FauxNY(void);

  static bool matches(void);

  void shutterPress(void) override;
  void shutterRelease(void) override;
  void focusPress(void) override;
  void focusRelease(void) override;
  void updateGeoData(const gps_t &gps, const timesync_t &timesync) override;

#if defined(FURBLE_SIM)
  /**
   * The geotag the simulated camera last received.
   *
   * The whole point of fix hold is that a camera keeps getting geotags after
   * the receiver loses its fix, so a scenario has to be able to assert what
   * actually arrived at the far end of the production GPS to camera path, not
   * just what the GPS page rendered. Observability only, the send path itself
   * is unchanged.
   */
  typedef struct {
    uint32_t count;
    double latitude;
    double longitude;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
  } geo_record_t;

  static geo_record_t getGeoRecord(void);
#endif
  size_t getSerialisedBytes(void) const override;
  bool serialise(void *buffer, size_t bytes) const override;

 protected:
  bool _connect(void) override;
  void _disconnect(void) override;

 private:
  typedef struct _fauxNY_t {
    char name[MAX_NAME]; /** Human readable device name. */
    uint64_t address;    /** Device MAC address. */
    uint8_t type;        /** Address type. */
    uint32_t id;         /** Device ID. */
  } fauxNY_t;

  static constexpr const char *m_FauxNYStr = "FauxNY";
  uint64_t m_ID;
};

}  // namespace Furble
#endif
