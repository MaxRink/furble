#ifndef FURBLE_HOST_SONY_VIRTUAL_CAMERA_H
#define FURBLE_HOST_SONY_VIRTUAL_CAMERA_H

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "MockNimBLE.h"
#include "SubscriptionTable.h"

namespace Furble {
namespace Host {

/**
 * Sony Camera Remote peer for host protocol tests.
 *
 * Models the server side of the five vendor services recovered from the
 * ILCE-7RM5 4.00 BLE host stack: Camera Remote (8000CC00), Location
 * (8000DD00), Pairing (8000EE00), DiRxTx (8000BB00), and Remote Control
 * (8000FF00). The class has no radio implementation. NimBLEMockPeer supplies
 * the client transport.
 *
 * Evidence boundary: the FF00 service and its FF01/FF02 characteristics are
 * firmware-confirmed, but no camera-side FF01 parser was recovered. The four
 * Furble opcodes and every FF02 value remain public-source inference, so FF01
 * writes are rejected unless Config::allow_unproven_ff01 is set, and FF02
 * notifications are withheld unless Config::ff02_notifications is set. The
 * 91-byte and 95-byte DD11 layouts are firmware-confirmed; the selection bit
 * lives in DD21 byte 4 bit 1.
 *
 * The peer is deterministic by construction: it starts no timer and no thread,
 * and every notification is emitted synchronously from a write, a subscribe,
 * or an explicit emit call. setScript() makes a sequence of per-command faults
 * reproducible for a host test.
 */
class SonyVirtualCamera final: public NimBLEMockPeer {
 public:
  /** One-shot fault applied to the next matching command. */
  enum class Fault {
    None,
    /** Reject the command with an ATT error, state unchanged. */
    Reject,
    /** Camera reports busy, state unchanged. The Sony ATT value is synthetic. */
    Busy,
    /** Camera refuses the command in its current mode, state unchanged. */
    WrongMode,
    /** Accept the command but emit no response or notification. */
    Timeout,
    /** Execute the command, then sever the link. */
    Disconnect,
    /** Execute the command but return a malformed notification. */
    Malformed,
    /** Model a one-sided bond: the camera deleted its pairing. */
    StaleBond,
  };

  enum class Command {
    Unknown,
    PairingRegistration,
    PairingSavedHandle,
    LocationLock,
    LocationEnable,
    LocationTimeCorrection,
    LocationAreaAdjustment,
    LocationUpdate91,
    LocationUpdate95,
    CameraPowerOff,
    CameraApoAvoidance,
    WifiOn,
    ClockSet,
    Ff01FocusDown,
    Ff01FocusUp,
    Ff01ShutterDown,
    Ff01ShutterUp,
    Ff01Unknown,
    DirxtxRecordStart,
    DirxtxRecordStop,
  };

  enum class LocationForm {
    Bytes91,
    Bytes95,
  };

  struct Config {
    std::string name = "ZV-1F";
    NimBLEAddress address = NimBLEAddress(0x2d012d012d01ULL, 0);

    // Manufacturer-data model gate. The vendor pairing scan requires tag 0x22
    // bit 6 only. Furble additionally requires bits 7 and 1, so legacy_bit1
    // models the undecoded bit that a real body may clear (spec U-31).
    std::array<uint8_t, 2> model_code = {'U', '0'};
    uint16_t protocol_version = 0x6400;
    bool pairing_supported = true;
    bool pairing_enabled = true;
    bool legacy_bit1 = true;
    bool location_supported = true;
    bool location_enabled = false;
    bool camera_on = true;
    bool wireless_power_on = true;
    bool wifi_handover_supported = true;
    bool wifi_handover_enabled = false;
    bool remote_control_supported = true;
    bool remote_control_enabled = true;
    bool image_transfer_supported = true;
    bool image_transfer_enabled = false;
    bool push_transfer_supported = false;
    bool push_transfer_enabled = false;
    bool background_transfer_supported = false;
    bool background_transfer_enabled = false;

    // Link security. camera_bonded is the camera-side SMP bond; the phone-side
    // bond lives in NimBLEDevice. A mismatch is the stale bond that Sony
    // documents as deleted by camera initialization. It is separate from the
    // EE01 application registration.
    bool camera_bonded = false;
    bool accept_numeric_comparison = true;
    bool reject_secure_connection = false;
    bool drop_on_stale_bond = false;

    // EE01 registration. The default result is 1 (success). Any other value
    // is emitted through EE03 and leaves the camera unregistered.
    // registration_creates_bond models the inference that Android bonds during
    // the vendor EE01 flow; false keeps the SMP bond independent so a test can
    // pair and register in either order.
    bool reject_pairing = false;
    uint32_t pairing_result = 1;
    bool pairing_state_notifications = true;
    bool accept_repeat_registration = true;
    bool registration_creates_bond = false;
    std::vector<uint32_t> pairing_state_sequence = {1, 2};

    // Optional services. Each vendor service has a presence switch so a test
    // can model an omitted service as well as a present one.
    bool cc00_present = true;
    bool location_service_present = true;
    bool pairing_service_present = true;
    bool control_service_present = true;
    bool dirxtx_service_present = true;
    bool ee02_present = true;

    // Location behaviour. DD21 byte 4 bit 1 selects the 95-byte form in the
    // vendor application. require_91_byte_location models a body that only
    // accepts the base form (capture-gated, spec U-14).
    bool timezone_supported = true;
    bool require_91_byte_location = false;
    // No automatic lock release is documented. true keeps a basic test clean;
    // false models the lock leak that a missing DD30 = 00 write can leave.
    bool release_lock_on_disconnect = true;
    uint8_t lock_owner = 0;  // 0 free, 1 this peer, 2 another device
    uint8_t time_correction = 0;
    uint8_t area_adjustment = 0;
    bool emit_location_on_subscribe = true;

    // FF00 is firmware-confirmed but the opcodes are not. Both switches stay
    // off so no unproven behaviour is reported as success.
    bool allow_unproven_ff01 = false;
    bool ff02_notifications = false;
    bool emit_ff02_on_subscribe = false;

    // DiRxTx record control is app-static for livestreaming only. It is not a
    // still shutter, and the exact characteristic is unknown, so writes are
    // rejected unless this switch is set.
    bool allow_unproven_dirxtx = false;

    // CC13 negative offset sign handling is inference (spec U-19).
    bool accept_negative_clock_offset = false;

    // Identity strings used by CC0A, CC0B, and EE04. Their wire encodings are
    // not fully captured, so these are explicit test fixtures, not claims.
    std::string model_name = "ZV-1F";
    std::string firmware_version = "2.01";
    std::array<uint8_t, 4> saved_pair_handle = {0x00, 0x00, 0x00, 0x00};
    bool allow_unproven_ee02 = false;

    // Values reported by the CC09 status notification. Only the identifier
    // and payload bytes are proven; the TLV length framing is unknown.
    uint8_t cc09_wifi_status = 0;
    uint8_t cc09_wifi_error = 0;
    uint8_t cc09_image_transfer = 0;
    uint8_t cc09_remote_control = 1;
    bool emit_cc09_on_subscribe = false;
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

  SonyVirtualCamera();
  explicit SonyVirtualCamera(const Config &config);
  ~SonyVirtualCamera() override;

  NimBLEAdvertisedDevice advertisement() const;
  const Config &config() const;

  /** Vendor pairing gate: tag 0x22 bit 6. */
  bool vendorPairingGate() const;
  /** Furble matcher gate: tag 0x22 bits 7, 6, and 1 at the fixed offsets. */
  bool centralModelGate() const;

  bool cameraBonded() const;
  bool registered() const;
  bool configured() const;
  bool secured() const;
  bool staleBond() const;
  bool secureAttempted() const;
  bool pairingInProgress() const;
  uint32_t pairingResult() const;

  /**
   * Camera-side deletion. The camera record is cleared while the phone-side
   * NimBLE bond is left in place, which is the one-sided stale bond that Sony
   * documents for camera initialization.
   */
  void removeCameraBond();

  bool cameraPowerOn() const;
  bool wifiOn() const;
  bool apoAvoided() const;
  bool focusHeld() const;
  bool shutterHeld() const;
  bool recording() const;

  uint8_t lockOwner() const;
  bool locationLocked() const;
  bool locationTransferEnabled() const;
  uint8_t timeCorrection() const;
  uint8_t areaAdjustment() const;
  LocationForm lastLocationForm() const;
  const std::vector<uint8_t> &lastLocation() const;
  const std::vector<uint8_t> &lastClock() const;

  uint8_t lastAttError() const;
  uint16_t lastFf01Opcode() const;
  Fault lastFault() const;

  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  void clearEvents();

  /**
   * Deliver a notification through the callback the central subscribed with.
   * Returns false when no subscription is held for the characteristic.
   */
  bool emitNotification(const NimBLEUUID &service,
                        const NimBLEUUID &characteristic,
                        const std::vector<uint8_t> &payload);

  /**
   * FF02 remote-commander status. Returns false without a subscription, and
   * false while Config::ff02_notifications is off because every FF02 value is
   * public-source inference.
   */
  bool emitFf02(uint8_t command, uint8_t value);

  /** DD01 location availability notification, N-01 or N-02. */
  bool emitLocationAvailability(bool available);

  /**
   * CC09 camera status. The caller supplies the already framed TLV bytes
   * because the length field is not documented (spec U-17).
   */
  bool emitCameraStatus(const std::vector<uint8_t> &payload);

  /** EE03 state or result, 8-byte big-endian frame from section 6.1/6.2. */
  bool emitPairingState(uint32_t value);
  bool emitPairingResult(uint32_t value);

  /** EE02 saved-pair handle write model, used when the payload is exercised. */
  bool savedPairHandleAccepted() const;

  // Deterministic scripted mode. Steps are consumed in order only when the
  // command matches the front step. A one-shot fault takes precedence.
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

  bool connected() const;

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

  static const NimBLEUUID &cameraServiceUUID();
  static const NimBLEUUID &locationServiceUUID();
  static const NimBLEUUID &pairingServiceUUID();
  static const NimBLEUUID &controlServiceUUID();
  static const NimBLEUUID &dirxtxServiceUUID();

  static const NimBLEUUID &cc02CharacteristicUUID();
  static const NimBLEUUID &cc08CharacteristicUUID();
  static const NimBLEUUID &cc09CharacteristicUUID();
  static const NimBLEUUID &cc0aCharacteristicUUID();
  static const NimBLEUUID &cc0bCharacteristicUUID();
  static const NimBLEUUID &cc13CharacteristicUUID();
  static const NimBLEUUID &dd01CharacteristicUUID();
  static const NimBLEUUID &dd11CharacteristicUUID();
  static const NimBLEUUID &dd21CharacteristicUUID();
  static const NimBLEUUID &dd30CharacteristicUUID();
  static const NimBLEUUID &dd31CharacteristicUUID();
  static const NimBLEUUID &dd32CharacteristicUUID();
  static const NimBLEUUID &dd33CharacteristicUUID();
  static const NimBLEUUID &ee01CharacteristicUUID();
  static const NimBLEUUID &ee02CharacteristicUUID();
  static const NimBLEUUID &ee03CharacteristicUUID();
  static const NimBLEUUID &ee04CharacteristicUUID();
  static const NimBLEUUID &ff01CharacteristicUUID();
  static const NimBLEUUID &ff02CharacteristicUUID();

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
  static uint16_t readLE16(const std::vector<uint8_t> &value);
  static bool validClockFields(uint8_t month,
                               uint8_t day,
                               uint8_t hour,
                               uint8_t minute,
                               uint8_t second);
  static bool knownDirxtxCharacteristic(const NimBLEUUID &characteristic);

  Fault takeFault(Command command);
  void noteAttError(uint8_t error);
  void recordWrite(const NimBLEUUID &service,
                   const NimBLEUUID &characteristic,
                   Command command,
                   const std::vector<uint8_t> &value,
                   bool response,
                   bool accepted);

  bool handleClock(const std::vector<uint8_t> &value);
  bool handlePairingRegistration(const std::vector<uint8_t> &value);
  bool handleLocationLock(const std::vector<uint8_t> &value);
  bool handleLocationEnable(const std::vector<uint8_t> &value);
  bool handleLocationUpdate(const std::vector<uint8_t> &value);
  bool handleFf01(const std::vector<uint8_t> &value);
  bool handleDirxtx(const std::vector<uint8_t> &value);
  bool emitCc09Identifier(uint8_t identifier, const std::vector<uint8_t> &payload);
  void resetSession();

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_Secured = false;
  bool m_Registered = false;
  bool m_StaleBond = false;
  bool m_SecureAttempted = false;
  bool m_PairingInProgress = false;
  uint32_t m_PairingResult = 0;
  bool m_SavedPairHandleAccepted = false;

  bool m_PowerOn = true;
  bool m_WifiOn = false;
  bool m_ApoAvoided = false;
  bool m_FocusHeld = false;
  bool m_ShutterHeld = false;
  bool m_Recording = false;
  uint16_t m_LastFf01Opcode = 0;

  uint8_t m_LockOwner = 0;
  bool m_LocationTransferEnabled = false;
  uint8_t m_TimeCorrection = 0;
  uint8_t m_AreaAdjustment = 0;
  LocationForm m_LastLocationForm = LocationForm::Bytes95;
  std::vector<uint8_t> m_LastLocation;
  std::vector<uint8_t> m_LastClock;

  uint8_t m_LastAttError = 0;
  Fault m_LastFault = Fault::None;
  std::deque<ScriptStep> m_Script;
  bool m_Scripted = false;
  Fault m_NextFault = Fault::None;
  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
  SubscriptionTable<Subscription> m_Subscriptions;
};

}  // namespace Host
}  // namespace Furble

#endif
