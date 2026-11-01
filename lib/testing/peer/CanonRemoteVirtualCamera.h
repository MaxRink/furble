#ifndef FURBLE_HOST_CANON_REMOTE_VIRTUAL_CAMERA_H
#define FURBLE_HOST_CANON_REMOTE_VIRTUAL_CAMERA_H

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "MockNimBLE.h"

namespace Furble {
namespace Host {

/**
 * A Canon EOS Remote (BR-E1 style) peer for host protocol tests.
 *
 * The class models service 00050000 from spec/canon.md sections 3.2, 4.2, 5.7,
 * 5.8 and 7.6. Remote is a separate role from Smart: separate service,
 * separate pairing exchange, no GPS, no mode surface and no documented
 * notification.
 *
 * The corpus documents no camera-to-controller application message on this
 * role, no pairing result value and no control acknowledgement. The only
 * acknowledgement any artifact proves is the ATT Write Response to a write
 * request. The peer therefore emits no notification at all rather than
 * inventing one, and a test asserts on writes and on the recorded control
 * bitmask.
 */
class CanonRemoteVirtualCamera final: public NimBLEMockPeer {
 public:
  /**
   * Camera-side fault for the next command.
   *
   * Reject, Busy, WrongMode and Malformed fail the write. Remote has no
   * documented application-level error message, so a fault is surfaced as an
   * ATT failure instead of an invented response byte.
   * Timeout accepts the write and records the command, but the effect is
   * dropped. There is no documented response to withhold, so this models a
   * camera that silently ignores the command.
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
    std::string name = "Canon EOS R6 Mark II";
    NimBLEAddress address = NimBLEAddress(0x1A2B3C4D5E02ULL, 0);
    bool camera_bonded = false;
    bool accept_numeric_comparison = true;
    /**
     * Model gate: bodies whose manual documents no remote-controller Bluetooth
     * function (EOS M6 and PowerShot G9 X Mark II) do not advertise 00050000.
     */
    bool remote_supported = true;
    /**
     * Remote advertisement manufacturer data. The form and product ID are not
     * documented (U-40), so the default advertises the service UUID only, which
     * is what every public peer filters on.
     */
    bool include_manufacturer_data = false;
    uint16_t product_id = 0x330B;
    /**
     * BR-M5 writes the name with surrounding spaces and no 03 prefix. Whether
     * a camera accepts that form is unknown (U-38), so the default rejects it.
     */
    bool accept_unprefixed_name = false;
    /** Control bits 1:0 and mode value 0 are not documented (CR-03). */
    bool allow_unproven_control_bits = false;
    /** Furble and every public peer secure the link before the name write. */
    bool require_secure = true;
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

  CanonRemoteVirtualCamera();
  explicit CanonRemoteVirtualCamera(const Config &config);
  ~CanonRemoteVirtualCamera() override;

  NimBLEAdvertisedDevice advertisement() const;

  const Config &config() const;
  const std::vector<Write> &writes() const;
  /**
   * Always empty. No manual, public peer or Furble source documents an
   * asynchronous message on 00050002 or 00050003, so the peer never emits one.
   */
  const std::vector<Notification> &notifications() const;
  void clearEvents();
  bool connected() const;
  bool secured() const;

  bool cameraBonded() const;
  void setCameraBonded(bool bonded);
  /** Camera-side pairing deletion. The central's bond store is untouched. */
  void removeCameraBond();

  const std::string &registeredName() const;
  /** The last control bitfield written to 00050003. */
  uint8_t lastControl() const;
  bool shutterHeld() const;
  bool focusHeld() const;
  /** -1 wide, 0 idle, 1 tele. */
  int zoomDirection() const;
  /** 0 none, 1 immediate, 2 two-second delay, 3 movie. */
  uint8_t controlMode() const;
  size_t shutterPresses() const;
  size_t focusPresses() const;
  size_t releases() const;
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

  static const NimBLEUUID &remoteServiceUUID();
  static const NimBLEUUID &nameCharacteristicUUID();
  static const NimBLEUUID &controlCharacteristicUUID();

 private:
  static bool matches(const NimBLEUUID &left, const NimBLEUUID &right);

  Fault consumeFault();
  bool handleControl(uint8_t value);

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_Secured = false;
  std::string m_RegisteredName;

  uint8_t m_LastControl = 0;
  bool m_ShutterHeld = false;
  bool m_FocusHeld = false;
  int m_ZoomDirection = 0;
  uint8_t m_ControlMode = 0;
  size_t m_ShutterPresses = 0;
  size_t m_FocusPresses = 0;
  size_t m_Releases = 0;

  size_t m_RejectedWrites = 0;

  Fault m_Fault = Fault::None;
  bool m_Scripted = false;
  std::deque<Fault> m_Script;

  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
};

}  // namespace Host
}  // namespace Furble

#endif
