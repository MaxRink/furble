#ifndef FURBLE_HOST_LUMIX_VIRTUAL_CAMERA_H
#define FURBLE_HOST_LUMIX_VIRTUAL_CAMERA_H

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "MockNimBLE.h"

namespace Furble {
namespace Host {

/**
 * Panasonic LUMIX peer for host protocol tests.
 *
 * Models the MEI0 session generation from the S5II and BGH1 traces, and the
 * APS generation that LUMIX Sync selects whenever the camera exposes
 * bb5d2a8c-5d3b-43ad-bfc3-470cad55c1ca. The class has no radio
 * implementation. NimBLEMockPeer supplies the client transport.
 *
 * LUMIX creates no LE bond. The app performs no createBond, setPin, or
 * setPasskey call, and the public S5 capture reports no SMP packet. This peer
 * therefore treats secureConnection() as a no-op on the plaintext link and
 * keeps cameraBonded() false. The camera-side registration record is a menu
 * entry, not a key, and removeRegistration() models its deletion.
 *
 * Evidence boundary: the plain MEI0 identity, the control bytes, the 16-byte
 * GPS packet, and the 10-byte clock packet are documented. The control
 * readiness value is documented as 00 only after Confirm. There is no focus
 * opcode: 01 is Confirm and 02 is Start, so a shutter write before Start is
 * refused unless Config::allow_shutter_without_start is set. The clock packet
 * has two conflicting tail layouts; Config selects which one the peer
 * accepts. No timer or thread is started, and every notification is emitted
 * synchronously. The Reject, Busy, WrongMode, and Timeout faults are synthetic
 * where the corpus documents no camera-level error value; they exist to drive
 * the controller's failure paths, not to claim a captured response.
 */
class LumixVirtualCamera final: public NimBLEMockPeer {
 public:
  enum class Fault {
    None,
    Reject,
    Busy,
    WrongMode,
    Timeout,
    Disconnect,
    Malformed,
    StaleBond,
  };

  enum class Command {
    Unknown,
    Mei0Identity,
    ControllerName,
    ApsIdentity,
    ApsName,
    ControlConfirm,
    ControlStart,
    ControlFinish,
    ControlShutterOn,
    ControlShutterOff,
    ControlRecOn,
    ControlRecOff,
    GpsPush,
    GpsStop,
    ClockWrite,
    SsidWrite,
    ConnectType,
    WifiStart,
  };

  enum class ClockLayout {
    /** App layout: int16 LE offset at [7:9], DST at [9]. */
    App,
    /** G9II public layout: zero at [7], int16 LE offset at [8:10]. */
    G9ii,
  };

  enum class MovieSemantics {
    /** 06 starts and 07 stops, the app's RecOn/RecOff names. */
    PressRelease,
    /** 06 toggles recording and 07 is accepted as a stop. */
    Toggle,
  };

  enum class IdentityPath {
    None,
    Mei0,
    Aps,
  };

  struct Config {
    std::string name = "LUMIX DC-S5II";
    NimBLEAddress address = NimBLEAddress(0x3a003a003a00ULL, 0);
    uint8_t advertisement_flags = 0x00;
    std::array<uint8_t, 5> address_fragment = {0x00, 0x00, 0x00, 0x00, 0x00};
    bool advertise_session_service = true;

    // Generation gate. Presence of the APS identity characteristic is the
    // app's isUUIDApsSupported flag. The plain MEI0 path is still advertised
    // when this is false.
    bool aps_supported = false;
    std::array<uint8_t, 4> aps_key = {0x90, 0x32, 0x78, 0x2c};
    bool aps_key_reads_zero = false;
    bool accept_plain_when_aps = false;

    // MEI0 identity. The fixed prefix is proven. The six-byte seed is a
    // per-install DLNA value, not a counter plus checksum. Config defaults to
    // Furble's seed; require_known_seed turns a different seed into a refusal.
    std::array<uint8_t, 6> mei0_seed = {0x02, 0x00, 0x00, 0x01, 0x00, 0x00};
    bool require_known_seed = false;
    bool drop_on_identity_change = false;
    uint8_t max_plain_name_bytes = 32;

    // Optional services and characteristics.
    bool info_service_present = true;
    bool control_service_present = true;
    bool location_service_present = true;
    bool clock_service_present = true;
    bool wifi_service_present = true;
    bool state_service_present = true;

    // Session handshake. The app sends Confirm 01, waits for readiness 00,
    // then Start 02. A body may reject a bare ShutterOn 04, so that is the
    // default. Set allow_shutter_without_start to model a permissive body.
    bool require_confirm = true;
    bool require_registration_for_control = true;
    bool allow_start_without_confirm = false;
    bool allow_shutter_without_start = false;
    bool allow_finish_without_start = true;
    // The app sends Finish on exit. true clears the session on link loss;
    // false keeps the started state so the next session must Confirm.
    bool release_on_disconnect = false;

    // Readiness notifications from the public S5 report. The clock request
    // payload itself was not captured, so the peer emits the reported 0x01.
    bool readiness_on_subscribe = true;
    bool gps_requires_readiness = false;
    bool clock_requires_readiness = false;

    ClockLayout clock_layout = ClockLayout::G9ii;
    bool accept_app_clock_layout = true;

    MovieSemantics movie_semantics = MovieSemantics::PressRelease;

    // Camera identity values. The model and vendor read formats are simple
    // ASCII fixtures because the wire encoding is not fully captured.
    std::string vendor_name = "Panasonic";
    std::string camera_model = "DC-S5M2";
    std::string camera_ssid = "DC-S5M2-000000";
    std::string camera_name = "LUMIX";
    std::string ap_password = "12345678";
    std::array<uint8_t, 4> capability = {0x02, 0x00, 0x00, 0x00};

    // Fault and state fixtures.
    bool busy = false;
    bool reject_secure_connection = false;
  };

  struct Write {
    std::string service;
    std::string characteristic;
    std::string command;
    std::vector<uint8_t> payload;
    bool response = false;
    bool accepted = false;
  };

  struct Notification {
    std::string service;
    std::string characteristic;
    std::vector<uint8_t> payload;
  };

  struct ScriptStep {
    Command command = Command::Unknown;
    Fault fault = Fault::None;
  };

  LumixVirtualCamera();
  explicit LumixVirtualCamera(const Config &config);
  ~LumixVirtualCamera() override;

  NimBLEAdvertisedDevice advertisement() const;
  const Config &config() const;

  /** Camera advertises the session service that Furble's matcher requires. */
  bool advertisesSessionService() const;
  bool apsPresent() const;

  bool cameraBonded() const;
  bool staleBond() const;
  bool bondSupported() const;
  bool registered() const;
  bool configured() const;
  bool secured() const;
  IdentityPath identityPath() const;
  const std::vector<uint8_t> &mei0Identity() const;
  const std::vector<uint8_t> &apsIdentity() const;
  const std::string &controllerName() const;
  const std::string &writtenSsid() const;
  uint8_t connectType() const;
  bool wrongIdentityPath() const;

  /**
   * Camera-side registration deletion. LUMIX has no LE bond, so this clears
   * the application registration record and returns the peer to the
   * unregistered session state. It never touches the NimBLE bond store.
   */
  void removeRegistration();
  /** LUMIX has no LE bond, so this only records the unsupported request. */
  void removeCameraBond();

  bool confirmed() const;
  bool sessionStarted() const;
  bool sessionFinished() const;
  bool staleSession() const;
  bool shutterHeld() const;
  bool recording() const;
  uint8_t lastControlCommand() const;

  bool locationReady() const;
  bool clockReady() const;
  bool gpsStopped() const;
  const std::vector<uint8_t> &lastGps() const;
  const std::vector<uint8_t> &lastClock() const;
  int16_t clockOffsetMinutes() const;
  uint8_t clockDst() const;
  ClockLayout lastClockLayout() const;

  uint8_t lastAttError() const;
  bool secureRequested() const;
  Fault lastFault() const;

  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  void clearEvents();

  bool emitNotification(const NimBLEUUID &service,
                        const NimBLEUUID &characteristic,
                        const std::vector<uint8_t> &payload);

  /** 37701b80 value 01 start, 02 stop, or 03 resend. */
  bool emitLocationReadiness(uint8_t value);
  /** 4cf487c0 clock request. The captured value is not documented; 01 is the
   * public report's "clock ready" reading. */
  bool emitClockRequest(uint8_t value = 0x01);
  /** 7be5fe6e control readiness or state value. 00 gates Start. */
  bool emitControlReadiness(uint8_t value);
  /** 18345be1 SSID or connect-type acknowledgement. */
  bool emitSsidAcknowledgement(uint8_t value = 0x00);

  void setScript(const std::vector<ScriptStep> &steps);
  void appendScriptStep(const ScriptStep &step);
  void clearScript();
  size_t scriptRemaining() const;
  void setScripted(bool enabled);
  bool scripted() const;
  void scriptNextCommand(Fault fault);
  size_t scriptedRemaining() const;
  void setNextFault(Fault fault);
  Fault pendingFault() const;
  void clearFault();
  void clearFaults();

  /** Mark the session as left in the started state by an abrupt disconnect. */
  void setStaleSession(bool stale);
  bool connected() const;
  bool secureAccepted() const;
  bool identityChanged() const;

  bool acceptConnection(NimBLEClient &client, const NimBLEAddress &address) override;
  void disconnect(NimBLEClient &client, int reason) override;
  bool hasService(const NimBLEUUID &service) const override;
  bool hasCharacteristic(const NimBLEUUID &service,
                         const NimBLEUUID &characteristic) const override;
  bool discoverCharacteristic(NimBLEClient &client,
                              const NimBLEUUID &service,
                              const NimBLEUUID &characteristic) override;
  bool canWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) const override;
  bool write(NimBLEClient &client,
             const NimBLEUUID &service,
             const NimBLEUUID &characteristic,
             const std::vector<uint8_t> &value,
             bool response) override;
  NimBLEAttValue read(NimBLEClient &client,
                      const NimBLEUUID &service,
                      const NimBLEUUID &characteristic) override;
  bool subscribe(NimBLEClient &client,
                 const NimBLEUUID &service,
                 const NimBLEUUID &characteristic,
                 bool notification,
                 NimBLERemoteCharacteristic *remote,
                 const NimBLENotifyCallback &callback,
                 bool response) override;
  bool secureConnection(NimBLEClient &client) override;
  bool updateConnectionParams(NimBLEClient &client,
                              uint16_t min_interval,
                              uint16_t max_interval,
                              uint16_t latency,
                              uint16_t timeout) override;
  int getRssi() const override;

  static const NimBLEUUID &sessionServiceUUID();
  static const NimBLEUUID &infoServiceUUID();
  static const NimBLEUUID &controlServiceUUID();
  static const NimBLEUUID &locationServiceUUID();
  static const NimBLEUUID &clockServiceUUID();
  static const NimBLEUUID &wifiServiceUUID();
  static const NimBLEUUID &stateServiceUUID();

  static const NimBLEUUID &mei0CharacteristicUUID();
  static const NimBLEUUID &deviceNameCharacteristicUUID();
  static const NimBLEUUID &apsKeyCharacteristicUUID();
  static const NimBLEUUID &apsIdentityCharacteristicUUID();
  static const NimBLEUUID &apsNameCharacteristicUUID();
  static const NimBLEUUID &controlCharacteristicUUID();
  static const NimBLEUUID &controlReadinessCharacteristicUUID();
  static const NimBLEUUID &gpsCharacteristicUUID();
  static const NimBLEUUID &locationReadinessCharacteristicUUID();
  static const NimBLEUUID &clockCharacteristicUUID();
  static const NimBLEUUID &clockRequestCharacteristicUUID();
  static const NimBLEUUID &ssidCharacteristicUUID();
  static const NimBLEUUID &connectTypeCharacteristicUUID();
  static const NimBLEUUID &ssidAcknowledgementCharacteristicUUID();
  static const NimBLEUUID &wifiStartCharacteristicUUID();
  static const NimBLEUUID &capabilityCharacteristicUUID();
  static const NimBLEUUID &vendorCharacteristicUUID();
  static const NimBLEUUID &modelCharacteristicUUID();

 private:
  struct Subscription {
    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *remote = nullptr;
    NimBLENotifyCallback callback;
    bool notification = false;
    bool response = false;
  };

  static bool matches(const NimBLEUUID &left, const NimBLEUUID &right);
  static std::string key(const NimBLEUUID &service, const NimBLEUUID &characteristic);
  static const char *commandName(Command command);
  static uint16_t readLE16(const uint8_t *value);
  static uint32_t readLE32(const uint8_t *value);
  static int16_t readLEI16(const uint8_t *value);
  static bool validClockFields(uint8_t month,
                               uint8_t day,
                               uint8_t hour,
                               uint8_t minute,
                               uint8_t second);
  static bool decodeApsBlock(const std::vector<uint8_t> &encoded,
                             const std::array<uint8_t, 4> &key,
                             std::vector<uint8_t> &decoded);
  static bool printableAscii(const std::vector<uint8_t> &value);

  Fault takeFault(Command command);
  void noteAttError(uint8_t error);
  void recordWrite(const NimBLEUUID &service,
                   const NimBLEUUID &characteristic,
                   Command command,
                   const std::vector<uint8_t> &value,
                   bool response,
                   bool accepted);
  bool handleMei0(const std::vector<uint8_t> &value);
  bool handleApsIdentity(const std::vector<uint8_t> &value);
  bool handleApsName(const std::vector<uint8_t> &value);
  bool handleControl(const std::vector<uint8_t> &value);
  bool handleGps(const std::vector<uint8_t> &value);
  bool handleClock(const std::vector<uint8_t> &value);
  bool handleWifi(const NimBLEUUID &characteristic, const std::vector<uint8_t> &value);
  void resetSession();

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_Registered = false;
  bool m_WrongIdentityPath = false;
  bool m_SecureRequested = false;
  bool m_SecureAccepted = false;
  bool m_IdentityChanged = false;

  IdentityPath m_IdentityPath = IdentityPath::None;
  std::vector<uint8_t> m_Mei0Identity;
  std::vector<uint8_t> m_ApsIdentity;
  std::string m_ControllerName;
  std::string m_WrittenSsid;
  uint8_t m_ConnectType = 0;
  std::array<uint8_t, 4> m_EffectiveApsKey = {0x90, 0x32, 0x78, 0x2c};

  bool m_Confirmed = false;
  bool m_SessionStarted = false;
  bool m_SessionFinished = false;
  bool m_StaleSession = false;
  bool m_ShutterHeld = false;
  bool m_Recording = false;
  uint8_t m_LastControlCommand = 0;

  bool m_LocationReady = false;
  bool m_ClockReady = false;
  bool m_GpsStopped = false;
  std::vector<uint8_t> m_LastGps;
  std::vector<uint8_t> m_LastClock;
  int16_t m_ClockOffsetMinutes = 0;
  uint8_t m_ClockDst = 0;
  ClockLayout m_LastClockLayout = ClockLayout::G9ii;

  uint8_t m_LastAttError = 0;
  Fault m_LastFault = Fault::None;
  std::deque<ScriptStep> m_Script;
  bool m_Scripted = false;
  Fault m_NextFault = Fault::None;
  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
  std::map<std::string, Subscription> m_Subscriptions;
};

}  // namespace Host
}  // namespace Furble

#endif
