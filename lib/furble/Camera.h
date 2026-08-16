#ifndef CAMERA_H
#define CAMERA_H

#include <atomic>
#include <cstdint>
#include <mutex>

#include <NimBLEAddress.h>
#include <NimBLEClient.h>
#include <NimBLEConnInfo.h>
#include <NimBLEDevice.h>

#include "FurbleTypes.h"

#define MAX_NAME (64)

namespace Furble {

/**
 * Represents a single target camera.
 */
class Camera: public NimBLEClientCallbacks {
 public:
  /**
   * Camera types.
   */
  enum class Type : uint32_t {
    FUJIFILM_BASIC = 1,
    CANON_EOS_SMART = 2,
    CANON_EOS_REMOTE = 3,
    MOBILE_DEVICE = 4,  // Deprecated, no longer supported
    FAUXNY = 5,
    NIKON = 6,
    SONY = 7,
    FUJIFILM_SECURE = 8,
    RICOH = 9,
  };

  enum class PairType : uint8_t {
    NEW = 1,
    SAVED = 2,
  };

  enum class SecurityMode : uint8_t {
    SECURE_DISPLAY_YESNO = BLE_HS_IO_DISPLAY_YESNO,
    SECURE_KEYBOARD_DISPLAY = BLE_HS_IO_KEYBOARD_DISPLAY,
  };

  enum class ConnProfile : uint8_t {
    FAST,
    IDLE,
    PEER,
  };

  /**
   * GPS data type.
   */
  typedef struct _gps_t {
    double latitude;
    double longitude;
    double altitude;
    unsigned int satellites;
  } gps_t;

  /**
   * Time synchronisation type.
   */
  typedef struct _timesync_t {
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    unsigned int centisecond;
  } timesync_t;

  Camera();
  ~Camera();

  /**
   * Wrapper for protected pure virtual Camera::_connect().
   *
   * @param[in] power ESP32 transmit power level.
   * @param[in] timeout Connection timeout in milliseconds.
   *
   * @return true iff the connect succeeded.
   */
  bool connect(esp_power_level_t power, uint32_t timeout);

  /**
   * Wrapper for protected pure virtual Camera::_disconnect();
   */
  void disconnect(void);

  /**
   * Send a shutter button press command.
   */
  virtual void shutterPress(void) = 0;

  /**
   * Send a shutter button release command.
   */
  virtual void shutterRelease(void) = 0;

  /**
   * Send a focus button press command.
   */
  virtual void focusPress(void) = 0;

  /**
   * Send a focus button release command.
   */
  virtual void focusRelease(void) = 0;

  /**
   * Update geotagging data.
   */
  virtual void updateGeoData(const gps_t &gps, const timesync_t &timesync) = 0;

  virtual size_t getSerialisedBytes(void) const = 0;
  virtual bool serialise(void *buffer, size_t bytes) const = 0;

  /**
   * Checks if the client is still connected.
   */
  virtual bool isConnected(void) const;

  /**
   * Get the latest connection RSSI in dBm.
   *
   * @return RSSI in dBm, or 0 when no sample is available.
   */
  int8_t getRssi(void) const;

  /**
   * Camera is active (ie. connect() has succeeded previously).
   */
  bool isActive(void) const;

  /**
   * Set camera activity state.
   */
  void setActive(bool active);

  const Type &getType(void) const;

  const std::string &getName(void) const;

  const NimBLEAddress &getAddress(void) const;

  /** Get connection progress percentage (0-100). */
  uint8_t getConnectProgress(void) const;

  /** Enable or disable adaptive connection parameters. */
  void setConnSaverEnabled(bool enabled);

  /** Record shutter or focus activity and whether the control is held. */
  void noteConnActivity(bool held);

  /** Request the idle profile after the inactivity threshold has elapsed. */
  void maybeSetIdle(void);

  /** Request a connection profile on the live connection. */
  bool setConnProfile(ConnProfile profile);

  /** Read the live connection parameters and RSSI. */
  bool getConnParams(uint16_t &interval, uint16_t &latency, uint16_t &timeout, int &rssi) const;

  /** Classify the live connection parameters. */
  ConnProfile getConnProfile(void) const;

  /** Format a connection profile for diagnostics. */
  static const char *connProfileName(ConnProfile profile);

 protected:
  Camera(Type type, PairType pairType);
  std::atomic<uint8_t> m_Progress;

  /**
   * Connect to the target camera such that it is ready for shutter control.
   *
   * This should include connection and pairing as needed for the target
   * device.
   *
   * @return true if the client is now ready for shutter control
   */
  virtual bool _connect(void) = 0;

  /**
   * Disconnect from the target.
   */
  virtual void _disconnect(void) = 0;

  /**
   * BLE security IO capability for this camera type.
   *
   * @return the IO capability to advertise during pairing.
   */
  virtual SecurityMode securityMode() const { return m_SecurityModeDefault; }

  const PairType m_PairType;
  NimBLEAddress m_Address = NimBLEAddress {};
  NimBLEClient *m_Client = nullptr;
  std::string m_Name;
  bool m_Connected = false;
  bool m_Paired = false;

 private:
  /** Called on connection success. */
  void onConnect(NimBLEClient *pDevice) override final;

  /** Called on disconnect. */
  void onDisconnect(NimBLEClient *pDevice, int reason) override final;

  /** Accept connection parameter changes requested by the peer. */
  bool onConnParamsUpdateRequest(NimBLEClient *pClient,
                                 const ble_gap_upd_params *params) override final;

  static constexpr uint16_t m_FastMinInterval = BLE_GAP_INITIAL_CONN_ITVL_MIN;
  static constexpr uint16_t m_FastMaxInterval = BLE_GAP_INITIAL_CONN_ITVL_MAX;
  static constexpr uint16_t m_FastLatency = 1;
  static constexpr uint16_t m_FastTimeout = (2 * BLE_GAP_INITIAL_SUPERVISION_TIMEOUT);
  static constexpr uint16_t m_IdleMinInterval = 400;
  static constexpr uint16_t m_IdleMaxInterval = 800;
  static constexpr uint16_t m_IdleLatency = 1;
  static constexpr uint16_t m_IdleTimeout = 3200;

  // These are the pre-connect values. Live updates use the profile constants above.
  uint16_t m_MinInterval = m_FastMinInterval;
  uint16_t m_MaxInterval = m_FastMaxInterval;
  // allow a packet to skip
  uint16_t m_Latency = m_FastLatency;
  // double the disconnect timeout
  uint16_t m_Timeout = m_FastTimeout;
  const Type m_Type;

  static constexpr SecurityMode m_SecurityModeDefault = SecurityMode::SECURE_DISPLAY_YESNO;

  mutable std::mutex m_Mutex;

  esp_power_level_t m_Power = ESP_PWR_LVL_P3;
  bool m_FromScan = false;
  bool m_Active = false;

  static constexpr uint32_t m_ConnSaverIdleMs = 10 * 1000;
  static constexpr uint32_t m_ConnParamsUpdateGuardMs = 3 * 1000;

  mutable std::mutex m_ConnParamsMutex;
  bool m_ConnSaverEnabled = false;
  bool m_ShutterHeld = false;
  uint32_t m_LastConnActivityMs = 0;
  ConnProfile m_LastRequestedProfile = ConnProfile::FAST;
  uint32_t m_LastRequestMs = 0;
  bool m_LastRequestValid = false;
  bool m_LastRequestSucceeded = false;
  bool m_PeerOverride = false;
};
}  // namespace Furble

#endif
