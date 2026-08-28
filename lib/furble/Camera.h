#ifndef CAMERA_H
#define CAMERA_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

#include <NimBLEAddress.h>
#include <NimBLEAttValue.h>
#include <NimBLEClient.h>
#include <NimBLEConnInfo.h>
#include <NimBLEDevice.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLEUUID.h>

#include "FurbleTypes.h"

#define MAX_NAME (64)

namespace Furble {

class NikonBase;

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
    PANASONIC_LUMIX = 10,
    DJI_OSMO = 11,
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

  /** True when the peer dropped the link while the last handshake ran. */
  bool peerDroppedDuringConnect(void) const {
    return m_PeerDroppedDuringConnect.load(std::memory_order_acquire);
  }

  /**
   * Wrapper for protected pure virtual Camera::_disconnect();
   */
  void disconnect(void);

  /**
   * Request cancellation of an in-flight connect attempt.
   *
   * Camera::connect() holds m_Mutex for the whole attempt, so a teardown that
   * needs that mutex (Camera::disconnect() from the target task) blocks until
   * the attempt unwinds. The long vendor waits inside _connect(), such as the
   * 25 s Fujifilm registration wait, poll connectCancelled() and abort
   * promptly once this is set, which bounds that block.
   *
   * Lock-free and safe from any task. Control::disconnect() sets it for every
   * target; Control::connectAll(bool) clears it when the user starts a new
   * connect cycle. Without this token a registration wait for an attempt that
   * began inactive could not be cancelled at all: its only abort inputs were
   * m_Connected (the camera holds the link up) and the isActive() flag (only
   * honoured when the attempt began active), the plan 148 teardown wedge.
   */
  void cancelConnect(void);

  /** Re-arm connect attempts after a cancelled cycle. */
  void clearConnectCancel(void);

  /**
   * Clear stale connection liveness before a fresh connect.
   *
   * A Camera in the persistent CameraList outlives its Control::Target. If a
   * prior session ended without onDisconnect firing (camera powered off, so the
   * disconnect callback is delayed to the supervision timeout, or a connect that
   * came up at the link level but failed registration), m_Connected can be left
   * stale-true on the list object. isConnected() then reports connected on the
   * next connect and connectAll() short-circuits to active without any BLE work.
   *
   * Called from Control::addActive() at the start of a user requested connect,
   * where the camera is not part of a live controlled session, so clearing the
   * flag is safe. Does not touch m_Client (it may already be self-deleted); the
   * flag is the only liveness guard, matching the connect and disconnect paths.
   */
  void resetConnectionState(void);

  /**
   * Reclaim an orphaned NimBLE client whose link never came down.
   *
   * disconnect() only issues an asynchronous ble_gap_terminate. For a live peer
   * the terminate completes and onDisconnect self-deletes the client. For a
   * powered-off or out-of-range peer the terminate can stall forever, so
   * onDisconnect never fires: the client is orphaned, isConnected() stays true,
   * and any teardown that waits on isConnected() clearing never finishes.
   *
   * Called from the control task once the target task has stopped and a short
   * drain bound has elapsed with the link still up. It frees the client through
   * NimBLEDevice::deleteClient() (a no-op if the client already self-deleted, so
   * it is safe even if onDisconnect raced in) and clears the connected flag, so
   * the teardown completes deterministically and a follow-up connect is not
   * blocked. Every live-link m_Client dereference is guarded by m_Connected,
   * which this clears, so freeing the client cannot race a reader.
   */
  void reclaimClient(void);

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

  /**
   * Refresh the cached connection parameter and RSSI snapshot.
   *
   * Blocks on the HCI transport, so call it from a control-plane task, never
   * from the UI task. Rate limited internally.
   */
  void updateConnStats(void);

  /** Read the cached connection parameters and RSSI snapshot. */
  bool getConnParams(uint16_t &interval, uint16_t &latency, uint16_t &timeout, int &rssi) const;

  /** Classify the cached connection parameters. */
  ConnProfile getConnProfile(void) const;

  /** Format a connection profile for diagnostics. */
  static const char *connProfileName(ConnProfile profile);

#if defined(FURBLE_CONSOLE)
  /** Enable or disable the debug GATT journal for vendor camera links. */
  static bool gattJournalSetEnabled(bool enabled);

  /** Print records not yet sent to the live console stream. */
  static void gattJournalDrain(void);

  /** Print the most recent journal records. */
  static void gattJournalDump(size_t count);

  /** Drop all buffered journal records. */
  static void gattJournalClear(void);
#endif

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

  /** Gate Fujifilm Secure's initial registration parameter request. */
  void setFujifilmSecureRegistration(bool in_progress);

  /** Request the bounded live profile after Secure registration. */
  bool requestFujifilmSecureFastProfile();

  /** Confirm the controller applied the exact Secure FAST profile. */
  bool confirmFujifilmSecureFastProfile();

  /**
   * Has cancelConnect() been requested for the current connect cycle?
   *
   * Vendor _connect() implementations poll this inside their long waits so a
   * user disconnect aborts the attempt within one poll interval.
   */
  bool connectCancelled(void) const;

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

  using gatt_notify_cb = std::function<void(NimBLERemoteCharacteristic *, uint8_t *, size_t, bool)>;

  /** Write through the single vendor GATT journal seam. */
  bool gattWrite(NimBLERemoteCharacteristic *characteristic,
                 const uint8_t *data,
                 size_t length,
                 bool response);

  /** Write a characteristic addressed by service and characteristic UUID. */
  bool gattWrite(const NimBLEUUID &service,
                 const NimBLEUUID &characteristic,
                 const uint8_t *data,
                 size_t length,
                 bool response);

  /** Write an attribute value addressed by service and characteristic UUID. */
  bool gattWrite(const NimBLEUUID &service,
                 const NimBLEUUID &characteristic,
                 const NimBLEAttValue &value,
                 bool response = false);

  /** Read through the single vendor GATT journal seam. */
  bool gattRead(NimBLERemoteCharacteristic *characteristic, NimBLEAttValue &value);

  /** Read a characteristic addressed by service and characteristic UUID. */
  bool gattRead(const NimBLEUUID &service, const NimBLEUUID &characteristic, NimBLEAttValue &value);

  /**
   * Subscribe through the single vendor notification journal seam.
   *
   * The CCCD descriptor write defaults to acknowledged (response = true), the
   * proven behaviour every vendor relies on. Only Fujifilm passes
   * response = false to request an unacknowledged write. An unacknowledged
   * write cannot block waiting for an ATT write response that a camera holding
   * a stale session never sends, so a Fujifilm subscribe can never hang the
   * connect. The notify callback is registered locally before the write, so
   * notifications still arrive even without the write response.
   */
  bool gattSubscribe(NimBLERemoteCharacteristic *characteristic,
                     gatt_notify_cb callback,
                     bool indicate = false,
                     bool response = true);

  const PairType m_PairType;
  NimBLEAddress m_Address = NimBLEAddress {};
  NimBLEClient *m_Client = nullptr;
  std::string m_Name;
  // Read lock-free by isConnected() from the UI task on every render, so it must
  // be atomic. Written by the NimBLE onConnect/onDisconnect callbacks and the
  // destructor without holding m_Mutex, so the atomic also removes a latent race.
  // It is the liveness guard for m_Client: onDisconnect clears it before NimBLE
  // frees the self-deleting client, so a true read means the client is alive.
  std::atomic<bool> m_Connected = false;
  // Set before a failed live-link teardown. NimBLE delivers the disconnect
  // callback asynchronously and performs self-delete after that callback
  // returns, so the connect task must not detach/delete the client first.
  std::atomic<bool> m_ClientDeleteOnDisconnect = false;
#if defined(FURBLE_CONSOLE)
  uint32_t m_DebugAttemptId = 0;
#endif
  bool m_Paired = false;

 private:
  friend class NikonBase;

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
  // Idle profile: 250 to 300 ms interval. A central-initiated switch to fast
  // applies several connection events after the request, so the first shutter
  // press after a quiet period goes out at the idle interval. Fujifilm shutter
  // is two write with response operations, so that first press costs up to
  // about 1.25 s worst case. Keeping the interval moderate bounds it there
  // while still cutting radio duty by roughly 8x against the fast profile.
  static constexpr uint16_t m_IdleMinInterval = 200;
  static constexpr uint16_t m_IdleMaxInterval = 240;
  // furble is the central, so latency saves nothing on our side. A non-zero
  // value lets the camera skip events and doubles worst-case write latency.
  static constexpr uint16_t m_IdleLatency = 0;
  // 7 s (unit is 10 ms). This is the link supervision timeout, so it also bounds
  // how long a dead link (camera powered off, out of range) stays reported as
  // connected before onDisconnect fires. The earlier 16 s value made a power-off
  // take up to 16 s to detect and, because the interactive disconnect waits for
  // the link to actually drop, froze the UI for that whole window.
  //
  // Must stay at or above m_FastTimeout (512, about 5.12 s), and here sits a bit
  // above it. Supervision margin is really a count of missed connection events,
  // the timeout divided by the connection interval. The idle profile uses much
  // longer intervals (m_IdleMinInterval to m_IdleMaxInterval, 250 to 300 ms) than
  // the fast profile (tens of ms), so a given timeout buys the idle link far
  // fewer events of margin. An idle timeout below fast would therefore make the
  // idle link the twitchier of the two, the opposite of what we want, and the
  // spurious-drop risk is worst exactly where furble runs, under BT modem sleep
  // plus DFS. 7 s keeps the idle link no more drop-prone than fast (about 23
  // events of margin at a 300 ms interval) while still cutting camera-off
  // detection from 16 s to about 7 s. Never drop below m_FastTimeout. The final
  // value is subject to a multi-minute connection-stability soak on hardware.
  static constexpr uint16_t m_IdleTimeout = 700;

  // These are the pre-connect values. Live updates use the profile constants above.
  const uint16_t m_MinInterval = m_FastMinInterval;
  const uint16_t m_MaxInterval = m_FastMaxInterval;
  // allow a packet to skip
  const uint16_t m_Latency = m_FastLatency;
  // double the disconnect timeout
  const uint16_t m_Timeout = m_FastTimeout;
  const Type m_Type;

  static constexpr SecurityMode m_SecurityModeDefault = SecurityMode::SECURE_DISPLAY_YESNO;

  mutable std::mutex m_Mutex;

  esp_power_level_t m_Power = ESP_PWR_LVL_P3;
  bool m_FromScan = false;
  // Read lock-free by isActive() and written by setActive()/disconnect() without
  // a shared lock discipline, so keep it atomic to remove the latent race.
  std::atomic<bool> m_Active = false;

  // Connect cancellation token. Set lock-free by cancelConnect() from
  // Control::disconnect(), polled by the vendor waits inside _connect(), and
  // cleared by clearConnectCancel() when a new user connect cycle starts. Never
  // cleared inside Camera::connect() itself: a cancel that lands between the
  // connectAll() abort check and the attempt entering connect() must survive
  // into the attempt, or the wait becomes uncancellable again.
  std::atomic<bool> m_ConnectCancelled = false;

  static constexpr uint32_t m_ConnSaverIdleMs = 10 * 1000;
  static constexpr uint32_t m_ConnParamsUpdateGuardMs = 3 * 1000;
  static constexpr uint32_t m_ConnStatsIntervalMs = 1000;

  mutable std::mutex m_ConnParamsMutex;
  bool m_ConnSaverEnabled = false;
  bool m_ShutterHeld = false;
  bool m_ConnectInProgress = false;
  bool m_FujifilmSecureRegistration = false;
  std::atomic<bool> m_PeerDroppedDuringConnect = false;
  uint32_t m_LastConnActivityMs = 0;
  ConnProfile m_LastRequestedProfile = ConnProfile::FAST;
  uint32_t m_LastRequestMs = 0;
  bool m_LastRequestValid = false;
  bool m_LastRequestSucceeded = false;
  bool m_PeerOverride = false;

  // Cached connection statistics, refreshed by updateConnStats() so UI reads
  // never block on the HCI transport or touch a self-deleted client.
  bool m_StatsValid = false;
  uint16_t m_StatsInterval = 0;
  uint16_t m_StatsLatency = 0;
  uint16_t m_StatsTimeout = 0;
  int m_StatsRssi = 0;
  uint32_t m_LastStatsMs = 0;
};
}  // namespace Furble

#endif
