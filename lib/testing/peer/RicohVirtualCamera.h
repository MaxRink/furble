#ifndef FURBLE_HOST_RICOH_VIRTUAL_CAMERA_H
#define FURBLE_HOST_RICOH_VIRTUAL_CAMERA_H

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "MockNimBLE.h"

namespace Furble {
namespace Host {

class RicohVirtualCamera final: public NimBLEMockPeer {
 public:
  // Shared fault vocabulary. Every peer uses the same enum so a scenario file
  // can name a fault once. Ricoh has one application-level error, so Reject,
  // Busy, WrongMode, Timeout and Malformed all surface as a refused write;
  // Disconnect severs the link; StaleBond refuses the encryption handshake.
  // All eight classes are listed in ricoh-handlers.md sections 2.1 and 9.4.
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
    std::string name = "RICOH GR III";
    NimBLEAddress address = NimBLEAddress(0x223344556677ULL, 0);
    bool camera_bonded = false;
    bool accept_numeric_comparison = true;
    // LE Secure Connections MITM pairing. Zero keeps the just-works handshake
    // the older fixtures rely on. A nonzero code makes secureConnection() run
    // the real exchange: the peer calls the central's onConfirmPasskey (or
    // onPassKeyDisplay) with the code and waits for the central's answer
    // through NimBLEDevice::injectConfirmPasskey, exactly as a GR IV does.
    uint32_t pairing_code = 0;
    // Passkey display instead of numeric comparison. The peer then reads the
    // code the central returns from onPassKeyDisplay and accepts only when it
    // matches pairing_code, which is what typing the code on the camera does.
    bool passkey_display = false;
    // How long the peer waits for the central's answer before it fails the
    // handshake the way a real pairing timeout does.
    uint32_t pairing_answer_timeout_ms = 5000;
    // The camera only completes a pairing while its own pairing menu is open.
    bool in_pairing_mode = true;
    // CameraPower and OperationMode single-byte values. A GR IV in BLE
    // standby keeps the link up with power ON (0x01) while OperationMode
    // reads BLE_STARTUP (0x02) instead of CAPTURE (0x00).
    uint8_t camera_power = 0x01;
    uint8_t operation_mode = 0x00;
    bool operation_mode_read_fails = false;
    // Sleeping-body model. A body that wakes on a fresh link leaves BLE
    // standby once, after the central dropped and re-established the link,
    // and reports CAPTURE from then on. wake_delay_ms is how long it takes
    // after the new link is up before the first CAPTURE read. Off by default
    // so every existing fixture keeps the configured OperationMode across a
    // reconnect.
    bool wake_on_reconnect = false;
    uint32_t wake_delay_ms = 0;
    // GR III/IIIx manufacturer record: model code and four-byte serial. The
    // serial is emitted little-endian. Evidence: ricoh-handlers.md section 10.
    uint8_t model_code = 0x01;
    std::array<uint8_t, 4> serial = {0xec, 0x78, 0x00, 0x00};
    // Sleep wakes a GR by connecting to the Bluetooth Control advertisement.
    // Evidence: ricoh-handlers.md sections 10.1 and 11.
    bool sleep_advertising = false;
  };

  // Decoded GPS Information write. Three byte-swapped float64 values, a native
  // little-endian uint16 year, then month to second. Byte 31 is never read by
  // the camera and carries no datum. Evidence: ricoh-handlers.md section 6.
  struct GpsFix {
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t trailing = 0;
    bool valid = false;
  };

  // One row of the 16-entry drive mode table. The table encodes
  // index = driveIndex * 3 + timerIndex, with timerIndex 0 none, 1 ten
  // seconds, 2 two seconds. Evidence: ricoh-handlers.md section 5.9.1.
  struct DriveMode {
    uint8_t index = 0;
    uint8_t drive = 0;
    uint8_t self_timer_seconds = 0;
  };

  struct Write {
    std::string service;
    std::string characteristic;
    std::vector<uint8_t> payload;
  };

  struct Notification {
    std::string service;
    std::string characteristic;
    std::vector<uint8_t> payload;
  };

  RicohVirtualCamera();
  explicit RicohVirtualCamera(const Config &config);
  ~RicohVirtualCamera() override;

  NimBLEAdvertisedDevice advertisement() const;
  bool cameraBonded() const;
  void removeCameraBond();
  void setCameraPower(uint8_t power);
  void setOperationMode(uint8_t mode);
  void setOperationModeReadFails(bool fails);
  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  void clearEvents();

  // Handler-derived state, so a test can prove the peer refused a value rather
  // than silently accepting it.
  uint8_t cameraPower() const;
  bool powerWriteAccepted() const;
  bool geotagEnabled() const;
  const GpsFix &lastGps() const;
  uint8_t driveModeIndex() const;
  const DriveMode &driveMode() const;
  static const std::array<DriveMode, 16> &driveModeTable();
  size_t captureCount() const;
  bool lastOperationRequest(uint8_t &code, uint8_t &arg) const;
  bool selfTimerTriggered() const;
  const std::string &pairedDeviceName() const;

  /**
   * Emit a Capture Status notification. Starts with the documented four-byte
   * value and ends with an explicit completion call so a test can observe the
   * 1 to 0 transition.
   */
  bool emitCaptureStatus(uint8_t capturing);
  void completeCapture();
  bool capturing() const;

  // Fault injection. failWrite models an ATT write the camera refuses, which
  // on Ricoh is the only application-level error signal (the handler returns
  // false and the stack answers with application error 0x80). setBusy models
  // a capture command that fails while the link stays up. setRepairRequest
  // models a fresh pairing request from a device the camera still has in its
  // paired-device table: the camera deletes the entry, denies, and disconnects.
  // Evidence: ricoh-handlers.md sections 2.1 and 9.4.
  void failWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic);
  void setBusy(bool busy);
  void setRepairRequest(bool repair);
  void clearFaults();

  // Shared fault vocabulary, same shape as every other peer.
  void setFault(Fault fault);
  Fault fault() const;
  void clearFault();
  // Scripted mode: each command write consumes one queued fault. The sticky
  // fault is ignored while scripting, and an empty queue means normal handling.
  void setScripted(bool scripted);
  bool scripted() const;
  void scriptNextCommand(Fault fault);
  size_t scriptedRemaining() const;
  void clearScript();

  /**
   * Deliver a notification through the callback the central subscribed with,
   * mirroring FujifilmVirtualCamera::emitNotification. Returns false when no
   * subscription is held for the characteristic.
   */
  bool emitNotification(const NimBLEUUID &service,
                        const NimBLEUUID &characteristic,
                        const std::vector<uint8_t> &payload);

  // Standby flap: the autonomous GR IV BLE-standby model from the 2026-08-28
  // hardware incident. The camera accepts every BLE connect, fails
  // secureConnection() for fail_attempts attempts the way a supervision
  // timeout does (the client is left reporting connected with the disconnect
  // event queued, the rc=520 class), then completes one handshake and, after
  // drop_after_ms on its own timer, emits CameraPower 0x00 and severs the
  // link (the time-compressed ~20 s standby drop). The failure budget then
  // re-arms so a reconnect loop churns autonomously. OperationMode semantics
  // (BLE_STARTUP versus CAPTURE) are untouched. setFlappy(0, 0) disables the
  // mode and joins the drop timer; disable or destroy the peer before
  // NimBLEDevice::resetMock() frees the client its timer may reference.
  void setFlappy(uint32_t fail_attempts, uint32_t drop_after_ms);

  // Run the standby drop body directly, without the wall-clock timer. The
  // simulator runs on virtual time, where a thread sleeping on the host clock
  // would neither fire at the modelled moment nor stay deterministic, so it
  // schedules the drop itself and calls this. Returns false when no link is up.
  bool triggerStandbyDrop();

  // Sleeping-body model. A GR IV in BLE standby keeps the link up, reports
  // CameraPower ON, and reports OperationMode BLE_STARTUP. Only dropping and
  // re-establishing the link is modelled as the wake trigger, because that is
  // the sequence the plan records; a CameraPower write that wakes the body is
  // deliberately not modelled, since no source verifies it.
  void setSleepingBody(bool wake_on_reconnect, uint32_t wake_delay_ms = 0);

  // True while OperationMode reads BLE_STARTUP, which is the standby state the
  // capture gate must refuse in.
  bool sleeping() const;

  // Links this peer accepted, and the reconnects that number implies.
  size_t linkSessions() const;
  size_t reconnectCount() const;

  // OperationMode reads the central performed, so a test can prove the wake
  // polled instead of reading once.
  size_t operationModeReadCount() const;

  // Capture starts received while the body was still in BLE standby. A real GR
  // IV cold boots and can wedge its firmware on one of these, so a regression
  // that removes the gate shows up here instead of silently passing.
  size_t captureRequestsWhileAsleep() const;

  bool acceptConnection(NimBLEClient &client, const NimBLEAddress &address) override;
  void disconnect(NimBLEClient &client, int reason) override;
  void disconnectComplete() override;
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
  void onPasskeyConfirmed(bool accept) override;
  void onPasskeyEntered(uint32_t passkey) override;

  /**
   * The passkey the central returned from onPassKeyDisplay.
   *
   * This is the value NimBLE would inject and the camera keypad would have to
   * match, so a test can assert the code on furble's screen is the code the
   * stack actually used rather than a constant compiled into furble.
   */
  uint32_t lastDisplayedPasskey(void) const;
  bool updateConnectionParams(NimBLEClient &client,
                              uint16_t min_interval,
                              uint16_t max_interval,
                              uint16_t latency,
                              uint16_t timeout) override;
  int getRssi() const override;

  static const NimBLEUUID &shootingFlavorCharacteristicUUID();
  static const NimBLEUUID &shootingServiceUUID();
  static const NimBLEUUID &operationRequestCharacteristicUUID();
  static const NimBLEUUID &cameraServiceUUID();
  static const NimBLEUUID &powerCharacteristicUUID();
  static const NimBLEUUID &operationModeCharacteristicUUID();
  static const NimBLEUUID &geotagEnableCharacteristicUUID();
  static const NimBLEUUID &gpsServiceUUID();
  static const NimBLEUUID &gpsInformationCharacteristicUUID();
  static const NimBLEUUID &selfTimerCharacteristicUUID();
  static const NimBLEUUID &captureStatusCharacteristicUUID();
  static const NimBLEUUID &pairedDeviceNameCharacteristicUUID();

 private:
  struct Subscription {
    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *remote = nullptr;
    NimBLENotifyCallback callback;
    bool notification = false;
  };

  static bool matches(const NimBLEUUID &left, const NimBLEUUID &right);
  static std::string key(const NimBLEUUID &service, const NimBLEUUID &characteristic);
  bool runPairingHandshake(NimBLEClient &client);
  bool isWriteFailed(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;
  bool handleOperationRequest(const std::vector<uint8_t> &value);
  bool parseGpsFix(const std::vector<uint8_t> &value);
  Fault consumeFault();
  void armFlappyDrop(NimBLEClient &client);
  void requestFlappyCancel();
  void cancelFlappyTimer();

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
  std::map<std::string, Subscription> m_Subscriptions;
  // Handler-derived state (ricoh-handlers.md sections 5 and 8).
  bool m_PowerWriteAccepted = false;
  bool m_GeotagEnabled = false;
  GpsFix m_LastGps;
  uint8_t m_DriveModeIndex = 0;
  size_t m_CaptureCount = 0;
  bool m_Capturing = false;
  bool m_HaveOperationRequest = false;
  uint8_t m_LastOperationCode = 0;
  uint8_t m_LastOperationArg = 0;
  bool m_SelfTimerTriggered = false;
  std::string m_PairedDeviceName;
  // Sleeping-body state. m_LinkSessions counts accepted links, so the second
  // one is the reconnect the wake path performs. m_WakePending is set when a
  // fresh link is up on a body that wakes on reconnect, and m_WakeAtMs is when
  // it starts reporting CAPTURE.
  //
  // Atomic: acceptConnection() runs on the target task while a test helper
  // thread polls reconnectCount() to decide when the probe has started, so a
  // plain size_t would be a data race that ASan and UBSan cannot see and that
  // the binaries in this file are not in the TSAN set to catch either.
  std::atomic<size_t> m_LinkSessions {0};
  size_t m_OperationModeReads = 0;
  size_t m_CaptureRequestsWhileAsleep = 0;
  bool m_WakePending = false;
  uint32_t m_WakeAtMs = 0;
  // Fault state.
  bool m_Busy = false;
  bool m_RepairRequest = false;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_FailedWrites;
  Fault m_Fault = Fault::None;
  bool m_Scripted = false;
  std::deque<Fault> m_Script;

  // Standby-flap state. See FujifilmVirtualCamera for the locking model: the
  // recursive mutex lets the drop timer re-enter disconnect() through
  // mockDropLink() on its own thread, while a cancelling thread that loses the
  // race blocks until the in-flight drop finishes. m_FlappyMutex also guards
  // m_Subscriptions, which the drop timer reads through emitNotification()
  // while the central subscribes and clears on its own thread.
  bool m_FlappyEnabled = false;
  uint32_t m_FlappyFailAttempts = 0;
  uint32_t m_FlappyFailRemaining = 0;
  uint32_t m_FlappyDropAfterMs = 0;
  bool m_FlappyCancel = false;
  std::recursive_mutex m_FlappyMutex;
  std::condition_variable_any m_FlappyCv;
  std::thread m_FlappyThread;

  // MITM handshake state. secureConnection() waits on m_PairingCv for the
  // central's injected answer, which arrives on the thread that called
  // answerPairing(), so the two threads model the real host task and UI task.
  mutable std::mutex m_PairingMutex;
  std::condition_variable m_PairingCv;
  bool m_PairingAnswered = false;
  bool m_PairingAccepted = false;
  uint32_t m_LastDisplayedPasskey = 0;
};

}  // namespace Host
}  // namespace Furble

#endif
