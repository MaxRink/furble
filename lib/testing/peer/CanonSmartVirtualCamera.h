#ifndef FURBLE_HOST_CANON_SMART_VIRTUAL_CAMERA_H
#define FURBLE_HOST_CANON_SMART_VIRTUAL_CAMERA_H

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "MockNimBLE.h"
#include "SubscriptionTable.h"

namespace Furble {
namespace Host {

/**
 * A Canon EOS Smart (Camera Connect style) peer for host protocol tests.
 *
 * The class models the server side of the Canon Smart role from the private
 * corpus (spec/canon.md, sections 2 to 7, and analysis/canon-firmware.md). It
 * has no radio implementation: NimBLEMockPeer supplies the client transport,
 * so the same event sequence can later be driven by a real peer adapter.
 *
 * Scope is the Smart role only, service 00010000. The Remote role is a
 * different advertisement, a different service and a different pairing
 * exchange, and is modelled by CanonRemoteVirtualCamera.
 *
 * Evidence policy: a command whose bytes are documented is modelled. A
 * command that the corpus marks unproven, unknown or capture-gated is either
 * rejected or gated behind a Config flag that defaults to the conservative
 * value. The comment on each flag names the unknown it closes.
 */
class CanonSmartVirtualCamera final: public NimBLEMockPeer {
 public:
  /**
   * Camera-side fault for the next command.
   *
   * None      normal handling.
   * Reject    the command is refused and the write reports failure.
   * Busy      the camera is busy; the write reports failure. The error
   *           characteristic 00030002 exists, but its category and code byte
   *           values are not in the corpus, so the peer does not invent one.
   *           Use emitError() to script a specific payload.
   * WrongMode the command is illegal in the current camera mode. Real mode
   *           legality is also enforced independently of this fault.
   * Timeout   the write is accepted, the command is applied, and the response
   *           notification or indication is withheld. This is the shape a
   *           Camera Connect style timeout takes.
   * Disconnect the write severs the link with a supervision-timeout drop
   *           (reason 0x08) and reports failure.
   * Malformed the frame length or opcode is not the documented one.
   * StaleBond secureConnection() fails after the link is up, with the
   *           disconnect event left queued (the rc=520 class).
   */
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

  struct Config {
    std::string name = "Canon EOS RP";
    NimBLEAddress address = NimBLEAddress(0x1A2B3C4D5E01ULL, 0);
    /** Adv product ID, little-endian on air. EOS RP is 0x32E2. */
    uint16_t product_id = 0x32E2;
    /**
     * Model gate. A body with no Smart role (a manual that documents no
     * Camera Connect Bluetooth function) does not advertise 00010000 and does
     * not expose the Smart services.
     */
    bool smart_supported = true;
    /**
     * Stored client UUID. All zero means the camera has no registered client
     * and advertises the 19-byte ADV-01 pairing form. A non-zero value means
     * the camera advertises the 6-byte ADV-02 reconnect form.
     */
    std::array<uint8_t, 16> client_uuid {};
    /** Camera-side bond and registration state. */
    bool camera_bonded = false;
    /** Just Works versus a refused pairing confirmation. */
    bool accept_numeric_comparison = true;
    /**
     * 00010006 pairing result. true indicates 02 accepted, false indicates 03
     * rejected. The corpus names no other value (U-04), so 03 is the only
     * rejection byte the peer emits.
     */
    bool pairing_accept = true;
    /**
     * Whether a second 00010006 request from an already registered client is
     * answered. The camera response to an unnecessary pair request is not
     * documented (section 7.5), so the default refuses the write.
     */
    bool accept_pair_while_registered = false;
    /** Optional services. Each is independently present in discovery. */
    bool has_wifi = true;
    bool has_gps = true;
    bool has_livestream = true;
    /** 0001000b capability mask, 4 bytes little-endian. */
    std::array<uint8_t, 4> capability = {0x58, 0x02, 0x00, 0x00};
    /** 00010005 power state: 0 auto power off, 1 on, 2 power switch off. */
    uint8_t power_state = 0x01;
    /** GPS source capability byte for 00040001, bits 0 to 4 documented. */
    uint8_t gps_source_capability = 0x10;
    /**
     * Furble writes a one-byte 01 to 00040002 to enable GPS. The acceptance of
     * that form is not proven (U-18) and the app uses the 8-byte 01 or 02, so
     * the default rejects it.
     */
    bool accept_one_byte_gps_enable = false;
    /**
     * 0001000a 02 is listed in the app command table but no call site uses it
     * (U-06), so the default rejects it.
     */
    bool accept_identity_failure_marker = false;
    /** 0001000a 05 client types other than 02 are not documented (U-47). */
    bool allow_unproven_client_types = false;
    /** 00030020 mask bits 9 to 23 are not demonstrated (U-25). */
    bool allow_unproven_button_bits = false;
    /**
     * Reads whose payload layout is unknown: 00020001 handover state, 00030001
     * feature and status, 00080001 livestream feature. The peer returns an
     * empty value unless the matching Config payload below is non-empty.
     * Notify-only characteristics are never readable; their values are
     * delivered by notification.
     */
    std::vector<uint8_t> handover_state;
    std::vector<uint8_t> feature_status;
    std::vector<uint8_t> livestream_feature;
    /** Device Information strings. The per-model values are not captured. */
    std::string manufacturer = "Canon Inc.";
    std::string firmware = "1.6.0";
    std::string software = "1.0.0";
    std::string serial = "000000000000";
  };

  struct Write {
    std::string service;
    std::string characteristic;
    std::vector<uint8_t> payload;
    bool response = false;
  };

  struct Notification {
    std::string service;
    std::string characteristic;
    std::vector<uint8_t> payload;
    bool indication = false;
  };

  CanonSmartVirtualCamera();
  explicit CanonSmartVirtualCamera(const Config &config);
  ~CanonSmartVirtualCamera() override;

  /** ADV-01 pairing form or ADV-02 reconnect form, plus the Smart service. */
  NimBLEAdvertisedDevice advertisement() const;

  const Config &config() const;
  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  void clearEvents();
  bool connected() const;

  bool cameraBonded() const;
  bool registered() const;
  bool committed() const;
  bool secured() const;
  const std::array<uint8_t, 16> &registeredClient() const;
  const std::string &clientName() const;
  void setCameraBonded(bool bonded);
  /** Store a client registration without a 00010006 exchange. */
  void setRegistered(const std::array<uint8_t, 16> &client);
  /**
   * Camera-side pairing deletion. The camera forgets its bond and its stored
   * client, and the advertisement reverts to the all-zero pairing form. The
   * central's bond store is deliberately not touched, because the resulting
   * stale-bond reconnect is the case worth testing.
   */
  void removeCameraBond();

  uint8_t powerState() const;
  void setPowerState(uint8_t power);
  uint8_t modeState() const;
  bool afHeld() const;
  bool shutterHeld() const;
  bool movieRecording() const;
  /** -1 wide, 0 idle, 1 tele. */
  int zoomDirection() const;
  bool gpsEnabled() const;
  uint8_t gpsState() const;
  uint8_t gpsSource() const;
  bool gpsRequestPending() const;
  const std::vector<uint8_t> &lastGpsPacket() const;
  uint16_t lastButtonMask() const;
  uint8_t lastPressType() const;
  uint8_t lastModeCommand() const;
  uint8_t lastHandoverCommand() const;
  bool livestreamRequested() const;
  bool autoPowerOffInhibited() const;
  size_t rejectedWrites() const;
  size_t withheldResponses() const;

  void setFault(Fault fault);
  Fault fault() const;
  void clearFault();

  /**
   * Deterministic scripted mode.
   *
   * While enabled, each command write consumes one queued Fault value and the
   * sticky fault is ignored. An empty queue means normal handling. This lets a
   * host test replay an exact accept, busy, timeout sequence without depending
   * on wall-clock or state history.
   */
  void setScripted(bool scripted);
  bool scripted() const;
  void scriptNextCommand(Fault fault);
  size_t scriptedRemaining() const;
  void clearScript();

  /** Deliver a notification or indication through the central's callback. */
  bool emitNotification(const NimBLEUUID &service,
                        const NimBLEUUID &characteristic,
                        const std::vector<uint8_t> &payload,
                        bool indication = false);
  /** 00010006 pairing result indication. */
  bool emitPairResult(uint8_t result);
  /** 00010005 power state notification. */
  bool emitPowerState(uint8_t power);
  /** 00030011 mode state notification: 01 off, 03 play, 04 record, 05 movie. */
  bool emitModeState(uint8_t state);
  /** 00040003 GPS state indication: state 1 to 3, source 0 to 4. */
  bool emitGpsState(uint8_t state, uint8_t source);
  /** 00040001 GPS request notification, byte 0 flags, byte 1 sources. */
  bool emitGpsRequest(uint8_t flags, uint8_t capability);
  /** 00030002 error notification. Category and code values are unknown. */
  bool emitError(uint8_t category, uint8_t code);
  bool emitButtonState(const std::vector<uint8_t> &payload);
  bool emitShootState(const std::vector<uint8_t> &payload);
  bool emitWifiIndication(const std::vector<uint8_t> &payload);
  bool emitApConfiguration(const std::vector<uint8_t> &payload);
  bool emitLivestreamStatus(const std::vector<uint8_t> &payload);

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

  static const NimBLEUUID &primaryServiceUUID();
  static const NimBLEUUID &powerCharacteristicUUID();
  static const NimBLEUUID &pairingCharacteristicUUID();
  static const NimBLEUUID &identityCharacteristicUUID();
  static const NimBLEUUID &capabilityCharacteristicUUID();
  static const NimBLEUUID &wifiIndicationCharacteristicUUID();
  static const NimBLEUUID &wifiServiceUUID();
  static const NimBLEUUID &modeServiceUUID();
  static const NimBLEUUID &modeCharacteristicUUID();
  static const NimBLEUUID &modeStateCharacteristicUUID();
  static const NimBLEUUID &buttonCharacteristicUUID();
  static const NimBLEUUID &shutterCharacteristicUUID();
  static const NimBLEUUID &gpsServiceUUID();
  static const NimBLEUUID &gpsRequestCharacteristicUUID();
  static const NimBLEUUID &gpsControlCharacteristicUUID();
  static const NimBLEUUID &gpsStateCharacteristicUUID();
  static const NimBLEUUID &livestreamServiceUUID();
  static const NimBLEUUID &deviceInformationServiceUUID();

 private:
  struct Subscription {
    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *remote = nullptr;
    NimBLENotifyCallback callback;
    bool notification = false;
  };

  static bool matches(const NimBLEUUID &left, const NimBLEUUID &right);
  static std::string key(const NimBLEUUID &service, const NimBLEUUID &characteristic);

  Fault consumeFault();
  bool shootingMode() const;
  bool handleWrite(NimBLEClient &client,
                   const NimBLEUUID &service,
                   const NimBLEUUID &characteristic,
                   const std::vector<uint8_t> &value,
                   bool suppressResponse);

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_Secured = false;
  bool m_Registered = false;
  // advertisement() runs on the radio task while camera writes and pairing
  // mutate m_Registered, m_ClientUuid, and m_Power on the camera task.
  mutable std::recursive_mutex m_AdvertisementMutex;
  bool m_Committed = false;
  std::array<uint8_t, 16> m_ClientUuid {};
  std::string m_ClientName;

  uint8_t m_Power = 0x01;
  uint8_t m_ModeState = 0x04;
  uint8_t m_LastModeCommand = 0;
  uint8_t m_LastHandoverCommand = 0;
  bool m_AfHeld = false;
  bool m_ShutterHeld = false;
  bool m_MovieRecording = false;
  int m_ZoomDirection = 0;
  uint16_t m_ButtonMask = 0;
  uint8_t m_PressType = 0;
  bool m_AutoPowerOffInhibited = false;
  bool m_LivestreamRequested = false;

  uint8_t m_GpsState = 0x01;
  uint8_t m_GpsSource = 0x00;
  bool m_GpsRequestPending = false;
  std::vector<uint8_t> m_LastGpsPacket;

  size_t m_RejectedWrites = 0;
  size_t m_WithheldResponses = 0;

  Fault m_Fault = Fault::None;
  bool m_Scripted = false;
  std::deque<Fault> m_Script;

  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
  SubscriptionTable<Subscription> m_Subscriptions;
};

}  // namespace Host
}  // namespace Furble

#endif
