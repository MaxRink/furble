#ifndef FURBLE_HOST_NIKON_REMOTE_VIRTUAL_CAMERA_H
#define FURBLE_HOST_NIKON_REMOTE_VIRTUAL_CAMERA_H

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "MockNimBLE.h"

namespace Furble {
namespace Host {

/**
 * A Nikon Remote (ML-L7 style) peer for host protocol tests.
 *
 * The class models the Remote profile of the Nikon DE00 service from
 * spec/nikon.md sections 2 to 7 and analysis/nikon-b600-handlers.md. The
 * profile is the B600 branch 1 set 2080, 2082, 2083, 2084, 2086, 2087, with
 * branch 2 adding 2081 and 2085 when Config::full_profile is set.
 *
 * The four-stage pairing exchange is two writes and two indications on 2087.
 * The firmware-static handler answers stage 1 with 02 plus sixteen zero bytes
 * and stage 3 with 04 plus sixteen zero bytes: the camera serial is copied to
 * the Smart auth buffer, not into the Remote response. The peer models that
 * proven behaviour rather than the serial-bearing shape Furble's validator
 * tolerates.
 *
 * The 2083 shutter characteristic takes [key id, operation]. Key 2 with
 * operation 2 is a short press, operation 3 is a long press, and operation 0
 * is a no-op: the handler returns without action, so it is not a release.
 * Operation 1 and any other operation also return without action.
 */
class NikonRemoteVirtualCamera final: public NimBLEMockPeer {
 public:
  /**
   * Camera-side fault for the next command.
   *
   * Reject, Busy, WrongMode and Malformed fail the write. The Nikon Remote
   * role has no documented application-level error message, so no response
   * byte is invented.
   * Timeout accepts a pairing stage write and withholds the stage indication,
   * which is the shape the 10 second per-stage wait expires on.
   * Disconnect severs the link with a supervision-timeout drop.
   * StaleBond fails secureConnection() with the queued-disconnect shape.
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
    std::string name = "NIKON COOLPIX B600";
    NimBLEAddress address = NimBLEAddress(0x2B3C4D5E6F01ULL, 0);
    /** First four bytes of the client device ID, echoed in the reconnect form. */
    std::array<uint8_t, 4> client_id = {0x5a, 0x11, 0x22, 0x33};
    /** Capability bitmap, bits 0 to 4 named (section 2.2). */
    uint8_t capability = 0x00;
    /** Camera holds a paired client and advertises the 7-byte reconnect form. */
    bool paired = false;
    bool camera_bonded = false;
    bool accept_numeric_comparison = true;
    /**
     * Nikon SMP parameters and whether an LE bond is required are unknown
     * (U-36), and SnapBridge never requests one on the LSS path. The default
     * needs no bond; set this to model a body that demands one.
     */
    bool require_bond = false;
    /**
     * Model gate. When false the DE00 service is still present for the common
     * characteristic set, but the Remote pair characteristic 2087 is absent,
     * so a Remote client cannot select this role. This models a body whose
     * manual documents no ML-L7 style remote.
     */
    bool remote_role_supported = true;
    /** Branch 2 profile: adds 2081 and 2085 to the Remote set. */
    bool full_profile = false;
    /** 2082 is a proven 32-byte gate, but its downstream consumer is unknown. */
    bool accept_key_map_write = true;
    /** Key IDs other than 2 have firmware cases but no proven meaning. */
    bool allow_unproven_keys = false;
    /** 2080, 2081, 2082, 2084, 2085, 2086 and 2087 read payloads are unknown. */
    std::vector<uint8_t> remote_info;     // 2080
    std::vector<uint8_t> capture_status;  // 2081
    std::vector<uint8_t> key_map;         // 2082
    std::vector<uint8_t> key_state;       // 2084
    std::vector<uint8_t> remote_info2;    // 2085
    std::vector<uint8_t> remote_info3;    // 2086
    std::vector<uint8_t> pair_info;       // 2087
    /** 2001 power control values are unknown (U-27). */
    std::vector<uint8_t> power_control;
    /** 2004 connection configuration layout is partly unknown (U-16). */
    std::vector<uint8_t> connection_configuration;
    /** 2005 connection establishment bits 2 to 6 are unknown (U-17). */
    std::vector<uint8_t> connection_establishment;
    /** 200a cable attachment payload is unknown (U-26). */
    std::vector<uint8_t> cable_attachment;
    /** 2009 feature bitmap value is not in the corpus (U-28). */
    bool feature_bitmap_known = false;
    std::array<uint8_t, 4> feature_bitmap = {0x00, 0x00, 0x00, 0x00};
    /** 2004 write gate is 102 bytes, but the layout is partly unknown. */
    bool accept_connection_configuration = false;
    /** 2001 write values are unknown. */
    bool allow_unproven_power_values = false;
    /** 2005 bits 2 to 6 are unknown. */
    bool allow_unproven_connection_bits = false;
    /**
     * 2081 has a length-4 notification branch in the sender, but the B600
     * allowlist rejects it before that branch (firmware-static), so the
     * notification path is dead unless a test opts in.
     */
    bool allow_capture_status_notification = false;
    uint8_t battery = 100;
    std::array<uint8_t, 8> serial = {'N', 'i', 'k', 'o', 'n', 'C', 'a', 'm'};
    std::string model = "COOLPIX B600";
    std::string firmware = "1.1";
    std::string software = "1.1";
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

  NikonRemoteVirtualCamera();
  explicit NikonRemoteVirtualCamera(const Config &config);
  ~NikonRemoteVirtualCamera() override;

  /** Pairing form with no manufacturer data, or the 99 03 reconnect form. */
  NimBLEAdvertisedDevice advertisement() const;

  const Config &config() const;
  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  void clearEvents();
  bool connected() const;

  bool paired() const;
  bool cameraBonded() const;
  void setCameraBonded(bool bonded);
  /** Store a paired client ID without an exchange. */
  void setPaired(bool paired);
  /**
   * Camera-side pairing deletion. Clears the paired record and the in-flight
   * exchange, and reverts the advertisement to the pairing form. The central's
   * bond store is untouched so a stale-bond reconnect can be tested.
   */
  void removeCameraBond();

  /** Last pairing stage byte seen on 2087, 0 before any write. */
  uint8_t pairingStage() const;
  const std::array<uint8_t, 8> &pairingTimestamp() const;
  const std::array<uint8_t, 8> &pairingDevice() const;
  size_t shortPresses() const;
  size_t longPresses() const;
  size_t noOps() const;
  uint8_t lastKeyId() const;
  uint8_t lastKeyOperation() const;
  const std::vector<uint8_t> &lastKeyMap() const;
  const std::vector<uint8_t> &lastTimePacket() const;
  const std::vector<uint8_t> &lastLocationPacket() const;
  bool controlPointWriteSeen() const;
  bool smartAuthWriteRejected() const;
  size_t rejectedWrites() const;

  void setFault(Fault fault);
  Fault fault() const;
  void clearFault();

  /**
   * Deterministic scripted mode. Each command write consumes one queued Fault
   * value; the sticky fault is ignored while scripting. An empty queue means
   * normal handling.
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
  /** 2087 pairing indication: the stage byte plus sixteen zero bytes. */
  bool emitPairingStage(uint8_t stage);
  /** 2084 six-byte remote key or status indication. */
  bool emitRemoteIndication(const std::vector<uint8_t> &payload);
  /** 2081 capture status notification, only in the full profile. */
  bool emitCaptureStatus(const std::vector<uint8_t> &payload);
  /** 2008 two-byte control point request: 01 00 time, 10 00 or 20 00 location. */
  bool emitControlPointRequest(uint16_t request);

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

  static const NimBLEUUID &serviceUUID();
  static const NimBLEUUID &pairingCharacteristicUUID();
  static const NimBLEUUID &shutterCharacteristicUUID();
  static const NimBLEUUID &remoteIndicationCharacteristicUUID();
  static const NimBLEUUID &keyMapCharacteristicUUID();
  static const NimBLEUUID &remoteInfoCharacteristicUUID();
  static const NimBLEUUID &remoteInfo2CharacteristicUUID();
  static const NimBLEUUID &captureStatusCharacteristicUUID();
  static const NimBLEUUID &batteryCharacteristicUUID();
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
  bool hasRemoteSet() const;
  bool handlePairing(const std::vector<uint8_t> &value, bool suppress_response);
  bool handleCommonWrite(const NimBLEUUID &characteristic, const std::vector<uint8_t> &value);

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_Paired = false;
  // The radio task calls advertisement() while the camera task runs pairing and
  // mutates m_Paired and the client ID. Both sides take this lock.
  mutable std::recursive_mutex m_AdvertisementMutex;

  uint8_t m_PairingStage = 0;
  std::array<uint8_t, 8> m_PairingTimestamp {};
  std::array<uint8_t, 8> m_PairingDevice {};

  size_t m_ShortPresses = 0;
  size_t m_LongPresses = 0;
  size_t m_NoOps = 0;
  uint8_t m_LastKeyId = 0;
  uint8_t m_LastKeyOperation = 0;
  std::vector<uint8_t> m_LastKeyMap;
  std::vector<uint8_t> m_LastTimePacket;
  std::vector<uint8_t> m_LastLocationPacket;
  std::string m_ClientName;
  bool m_ControlPointWriteSeen = false;
  bool m_SmartAuthWriteRejected = false;
  size_t m_RejectedWrites = 0;

  Fault m_Fault = Fault::None;
  bool m_Scripted = false;
  std::deque<Fault> m_Script;

  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
  std::map<std::string, Subscription> m_Subscriptions;
};

}  // namespace Host
}  // namespace Furble

#endif
