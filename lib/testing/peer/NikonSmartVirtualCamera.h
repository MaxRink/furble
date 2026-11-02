#ifndef FURBLE_HOST_NIKON_SMART_VIRTUAL_CAMERA_H
#define FURBLE_HOST_NIKON_SMART_VIRTUAL_CAMERA_H

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
 * A Nikon Smart (SnapBridge LSS style) peer for host protocol tests.
 *
 * The class models the Smart profile of the Nikon DE00 service from
 * spec/nikon.md sections 3 to 7 and analysis/nikon-b600-handlers.md section 5.
 * The four-stage exchange on 2000 is the LsSec/Blowfish challenge response:
 *
 *   stage 1 write   01, N1 (8), client device ID D (8)
 *   stage 2 indica  02, N2 (8), H(salt + N2 + N1) (8)
 *   stage 3 write   03, N1 (8), H(salt + N1 + N2) (8)
 *   stage 4 indica  04, N2 (8), camera serial (8)
 *
 * The camera picks one of the eight Nikon salts. The peer uses
 * Config::salt_index and Config::camera_nonce so the exchange is deterministic.
 * The default nonce matches the vendor test vector, so a host test can compare
 * the emitted hash against the values recorded in the corpus.
 *
 * 2008 is the LSS control point. The firmware accepts two-byte writes at the
 * GATT layer and then rejects them in the application handler. The peer
 * reproduces that: a write returns true, changes no state, and never emits the
 * final 01 00 that Furble waits for.
 *
 * 2020 and 2021 are absent from the B600 image (U-35) and the 2021 response
 * surface is capture-gated (U-23). They exist only with
 * Config::has_control_service set, and every 2021 write is refused while
 * Config::capture_gated_control is true.
 */
class NikonSmartVirtualCamera final: public NimBLEMockPeer {
 public:
  /**
   * Camera-side fault for the next command.
   *
   * Reject, Busy, WrongMode and Malformed fail the write. The Smart role has
   * no documented application-level error message, so no response byte is
   * invented.
   * Timeout accepts an auth stage write and withholds the stage indication,
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
    std::string name = "NIKON Z 6III";
    NimBLEAddress address = NimBLEAddress(0x3C4D5E6F7001ULL, 0);
    /** First four bytes of the client device ID, echoed in the reconnect form. */
    std::array<uint8_t, 4> client_id = {0x66, 0x77, 0x88, 0x99};
    /** Eight-byte client device ID D sent in stage 1. */
    std::array<uint8_t, 8> device_id = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    /** Camera nonce N2. The default matches the corpus test vector. */
    std::array<uint8_t, 8> camera_nonce = {0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    /** Salt index 0 to 7. The camera chooses it; the peer fixes it for tests. */
    uint8_t salt_index = 0;
    /** Capability bitmap, bits 0 to 4 named (section 2.2). */
    uint8_t capability = 0x04;
    /** Camera holds a paired client and advertises the 7-byte reconnect form. */
    bool paired = false;
    bool camera_bonded = false;
    bool accept_numeric_comparison = true;
    /**
     * Nikon SMP parameters and whether an LE bond is required are unknown
     * (U-36), and SnapBridge never requests one on the LSS path. The default
     * needs no bond.
     */
    bool require_bond = false;
    /**
     * Model gate. When false the DE00 service is present for the common set,
     * but 2000 is absent, so a Smart client cannot select this role.
     */
    bool smart_role_supported = true;
    /**
     * 2020 and 2021 are not in the B600 GATT table. SnapBridge binds them, so a
     * Z6III class body may expose them, but that is not firmware-proven.
     */
    bool has_control_service = false;
    /**
     * 2021 is the capture-gated control path. The request opcodes are
     * app-static, the response data for most opcodes is unknown, and whether
     * key events work without the Classic bond is unknown (U-37). While this
     * is true every 2021 write is refused.
     */
    bool capture_gated_control = true;
    /** 2021 opcode 0x14 and 0x15 function and control values are unknown. */
    bool allow_unproven_control_values = false;
    /** The Classic continuation has no proven BLE trigger (U-37). */
    bool allow_classic_continuation = false;
    /** 2004 layout is partly unknown (U-16). */
    bool accept_connection_configuration = false;
    /** 2001 values are unknown (U-27). */
    bool allow_unproven_power_values = false;
    /** 2005 bits 2 to 6 are unknown (U-17). */
    bool allow_unproven_connection_bits = false;
    /** 2009 value is not in the corpus (U-28). */
    bool feature_bitmap_known = false;
    std::array<uint8_t, 4> feature_bitmap = {0x00, 0x00, 0x00, 0x00};
    /** Unknown read payloads, supplied by the test rather than invented. */
    std::vector<uint8_t> category_info;      // 2080
    std::vector<uint8_t> capture_status;     // 2081
    std::vector<uint8_t> power_control;      // 2001
    std::vector<uint8_t> connection_config;  // 2004
    std::vector<uint8_t> connection_state;   // 2005
    std::vector<uint8_t> cable_attachment;   // 200a
    uint8_t battery = 100;
    std::array<uint8_t, 8> serial = {'0', '0', '0', '0', '0', '0', '0', '1'};
    std::string camera_name = "NIKON Z 6III";
    std::string model = "NIKON Z 6III";
    std::string firmware = "2.01";
    std::string software = "2.01";
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

  NikonSmartVirtualCamera();
  explicit NikonSmartVirtualCamera(const Config &config);
  ~NikonSmartVirtualCamera() override;

  /** Pairing form with no manufacturer data, or the 99 03 reconnect form. */
  NimBLEAdvertisedDevice advertisement() const;

  const Config &config() const;
  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  void clearEvents();
  bool connected() const;

  bool paired() const;
  bool authenticated() const;
  bool cameraBonded() const;
  void setCameraBonded(bool bonded);
  void setPaired(bool paired);
  /**
   * Camera-side pairing deletion. Clears the stored client, the in-flight
   * exchange and the authentication state, and reverts the advertisement to
   * the pairing form. The central's bond store is untouched.
   */
  void removeCameraBond();

  /** Last auth stage byte seen on 2000, 0 before any write. */
  uint8_t authStage() const;
  const std::array<uint8_t, 8> &clientNonce() const;
  const std::array<uint8_t, 8> &deviceId() const;
  const std::array<uint8_t, 8> &cameraNonce() const;
  uint8_t saltIndex() const;
  bool hashRejected() const;

  bool timeSynced() const;
  const std::vector<uint8_t> &lastTimePacket() const;
  const std::vector<uint8_t> &lastLocationPacket() const;
  bool controlPointWriteSeen() const;
  uint16_t controlRequest() const;
  bool remoteControlMode() const;
  const std::vector<std::array<uint8_t, 2>> &keyEvents() const;
  size_t captureGatedRejections() const;
  bool classicRequested() const;
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
  /**
   * 2000 auth indication. Stage 2 carries the camera nonce and
   * H(salt + N2 + N1); stage 4 carries the camera nonce and the serial.
   * Returns false when stage 1 has not run.
   */
  bool emitAuthStage(uint8_t stage);
  /** 2008 two-byte control point request. */
  bool emitControlPointRequest(uint16_t request);
  /** 2008 time request: bits 0 to 3 set, which is 01 00 on the wire. */
  bool emitTimeRequest();
  /** 2008 location request: bits 4 to 7, 1 on or 2 on with GPS. */
  bool emitLocationRequest(bool with_gps);
  /** 2008 connection request: bits 8 to 11 set, which is 00 01 on the wire. */
  bool emitConnectionRequest();
  /** 2020 control status indication, only with the control service present. */
  bool emitControlStatus(const std::vector<uint8_t> &payload);
  /** 2021 four-byte control response, only with the control service present. */
  bool emitControlResponse(uint8_t opcode, uint8_t code);

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
  static const NimBLEUUID &authCharacteristicUUID();
  static const NimBLEUUID &controlPointCharacteristicUUID();
  static const NimBLEUUID &timeCharacteristicUUID();
  static const NimBLEUUID &locationCharacteristicUUID();
  static const NimBLEUUID &featureCharacteristicUUID();
  static const NimBLEUUID &categoryInfoCharacteristicUUID();
  static const NimBLEUUID &captureStatusCharacteristicUUID();
  static const NimBLEUUID &controlStatusCharacteristicUUID();
  static const NimBLEUUID &controlCharacteristicUUID();
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
  bool handleAuth(const std::vector<uint8_t> &value, bool suppress_response);
  bool handleControl(const std::vector<uint8_t> &value);
  std::array<uint8_t, 8> expectedStage2Hash() const;
  std::array<uint8_t, 8> expectedStage3Hash(const std::array<uint8_t, 8> &nonce) const;

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_Paired = false;
  // The radio task calls advertisement() while the camera task runs pairing and
  // mutates m_Paired and the client ID. Both sides take this lock.
  mutable std::recursive_mutex m_AdvertisementMutex;
  bool m_Authenticated = false;

  uint8_t m_AuthStage = 0;
  std::array<uint8_t, 8> m_ClientNonce {};
  std::array<uint8_t, 8> m_DeviceId {};
  uint8_t m_SaltIndex = 0;
  bool m_HashRejected = false;

  bool m_TimeSynced = false;
  std::vector<uint8_t> m_LastTimePacket;
  std::vector<uint8_t> m_LastLocationPacket;
  bool m_ControlPointWriteSeen = false;
  uint16_t m_ControlRequest = 0;
  bool m_RemoteControlMode = false;
  std::vector<std::array<uint8_t, 2>> m_KeyEvents;
  size_t m_CaptureGatedRejections = 0;
  bool m_ClassicRequested = false;
  std::string m_ClientName;

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
