#ifndef FURBLE_HOST_DJI_VIRTUAL_CAMERA_H
#define FURBLE_HOST_DJI_VIRTUAL_CAMERA_H

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
 * DJI Osmo Action peer for host protocol tests.
 *
 * Models the DJI R SDK framing on the FFF0 service, the FFF4 notification
 * characteristic, and the FFF5 write characteristic. The class has no radio
 * implementation. NimBLEMockPeer supplies the client transport.
 *
 * Evidence boundary: the frame layout and CRC parameters are public-source,
 * and the official demo is a complete controller sample. The camera-side SMP
 * requirement is unknown. The demo performs no SMP call and uses unauthenticated
 * ATT for every operation, while Furble calls secureConnection() and requires
 * an encrypted bonded link. Config::smp_open_link_only models the risk that a
 * camera accepts only the open-link demo flow and Furble therefore fails.
 *
 * The camera's device ID encoding is unresolved. The official table gives
 * 0xFF33 for Action 4 and 0xFF44 for Action 5 Pro, while another official
 * source writes 0x33FF0000 and 0x44FF0000. Config::device_id_encoding selects
 * which bytes the camera reports. Both are modelled, and the peer never
 * assumes that a valid CRC or an accepted write produced a photo.
 *
 * No timer or thread is started. Status pushes are emitted synchronously from
 * the command that changes state, from a subscription, or from an explicit
 * emit call. The Reject, Busy, WrongMode, and Timeout faults are synthetic
 * where the corpus documents no camera-level error value; the common return
 * codes 1 and 2 are the closest documented values.
 */
class DjiVirtualCamera final: public NimBLEMockPeer {
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
    VersionQuery,
    KeyReport,
    DeviceRestart,
    GpsPush,
    ConnectionRequest,
    ConnectionResponse,
    PowerMode,
    RecordControl,
    ModeSwitch,
    StatusSubscription,
  };

  enum class DeviceIdEncoding {
    /** Table value 0xFF33, little-endian wire bytes 00 00 33 FF. */
    Table,
    /** Demo value 0x33FF0000, little-endian wire bytes 00 00 FF 33. */
    Rotated,
    /** The third candidate, 0x0000FF33, wire bytes 33 FF 00 00. */
    LowHalf,
  };

  struct Config {
    std::string name = "Osmo Action 4";
    NimBLEAddress address = NimBLEAddress(0xaa08aa08aa08ULL, 0);

    // Advertisement: AA 08 <device type> <byte 3> <state byte>, then the
    // controller-style tail. The demo requires byte 4 == 0xFA.
    uint8_t device_type = 0x14;
    uint8_t model_byte3 = 0x00;
    uint8_t state_byte = 0xfa;
    std::array<uint8_t, 6> mac = {0x08, 0xaa, 0x08, 0xaa, 0x08, 0xaa};
    bool advertise_mimo_tail = true;
    uint16_t advertisement_product_id = 0x0014;

    // Camera identity. The 16-bit SDK table value and the 32-bit wire field
    // disagree, so the encoding is explicit.
    uint32_t camera_device_id = 0xff330000U;
    DeviceIdEncoding device_id_encoding = DeviceIdEncoding::Table;
    uint32_t controller_device_id = 0x12345678U;
    bool allow_unknown_camera_id = false;
    bool strict_command_device_id = false;
    std::string product_id_string = "DJI-OsmoAction4";
    std::string sdk_version = "01.07.06.10";

    // Optional GATT surfaces.
    bool fff3_present = false;
    bool service_changed_present = false;
    bool fff3_writes_accepted = false;

    // Link security. The camera-side requirement is unknown.
    bool camera_bonded = false;
    bool accept_numeric_comparison = true;
    bool smp_open_link_only = false;
    bool smp_rejects = false;

    // Application-level connection approval.
    bool require_approval = true;
    bool auto_approve = true;
    bool camera_pairing_record = false;
    bool show_popup_when_no_record = true;
    bool reject_connection = false;
    bool send_connection_response = true;
    bool require_supported_camera_id = true;

    // Power and wake.
    bool asleep = false;
    bool wake_allowed = true;
    bool restart_drops_link = false;
    bool status_push_on_record = true;
    bool status_push_on_mode = true;
    bool push_1d06 = true;
    bool ack_gps_push = false;

    // A malformed input frame is still a completed ATT write on hardware.
    // The peer records it and emits no response; this switch controls the
    // write() return so a host test can choose either observation.
    bool att_accepts_malformed_frames = true;

    // The record-control command is the only shutter path Furble implements.
    // Whether it takes a photo in photo mode is unknown (spec U-21), so the
    // peer records the command and reports success without claiming a photo.

    // Status push fields from section 5.3.
    uint8_t camera_mode = 0x01;
    uint8_t camera_status = 0x01;
    uint8_t video_resolution = 16;
    uint8_t fps_idx = 3;
    uint8_t eis_mode = 0;
    uint16_t record_time = 0;
    uint8_t photo_ratio = 0;
    uint32_t remain_capacity_mb = 64000;
    uint32_t remain_photo_num = 9999;
    uint32_t remain_time_s = 7200;
    uint8_t user_mode = 0;
    uint8_t power_mode = 0;
    uint8_t temp_over = 0;
    uint32_t photo_countdown_ms = 0;
    uint16_t loop_record_sends = 0;
    uint8_t battery = 87;
    std::string mode_name = "Video";
    std::string mode_param = "4K30";
  };

  struct Frame {
    uint8_t command_set = 0;
    uint8_t command_id = 0;
    uint8_t command_type = 0;
    uint16_t sequence = 0;
    std::vector<uint8_t> payload;
    bool response = false;
  };

  struct Write {
    std::string characteristic;
    std::string command;
    std::vector<uint8_t> frame;
    bool accepted = false;
  };

  struct Notification {
    std::string characteristic;
    std::vector<uint8_t> payload;
  };

  struct ScriptStep {
    Command command = Command::Unknown;
    Fault fault = Fault::None;
  };

  DjiVirtualCamera();
  explicit DjiVirtualCamera(const Config &config);
  ~DjiVirtualCamera() override;

  NimBLEAdvertisedDevice advertisement() const;
  const Config &config() const;

  /** Company 0x08AA, byte 0 AA, byte 4 FA. */
  bool matchesDemoAdvertisement() const;
  /** Mimo's byte 2 device-type map: 0x14 is the Action 4 value. */
  uint8_t advertisedDeviceType() const;

  bool cameraBonded() const;
  bool staleBond() const;
  bool smpRisk() const;
  bool secureAccepted() const;

  /**
   * Camera-side deletion. The camera-side pairing record and the camera bond
   * are cleared while the controller bond stays in place, so the next
   * connection request models a camera that forgot the pair.
   */
  void removeCameraBond();

  bool protocolReady() const;
  bool connectionApproved() const;
  bool connectionRejected() const;
  bool pendingApproval() const;
  uint16_t pendingSequence() const;
  uint32_t lastCameraDeviceId() const;
  uint32_t cameraDeviceId() const;
  uint32_t controllerDeviceId() const;
  DeviceIdEncoding deviceIdEncoding() const;
  void setDeviceIdEncoding(DeviceIdEncoding encoding);
  uint32_t wireDeviceId() const;
  static bool supportedDeviceId(uint32_t deviceId);

  bool asleep() const;
  bool recording() const;
  uint8_t cameraMode() const;
  uint32_t cameraIndex() const;
  bool statusSubscribed() const;
  uint8_t pushMode() const;
  uint8_t pushFrequency() const;
  uint8_t lastReturnCode() const;
  uint8_t lastAttError() const;
  uint8_t lastKeyCode() const;
  uint16_t lastKeyValue() const;
  uint8_t lastMode() const;
  const std::vector<uint8_t> &lastGps() const;
  const Frame &lastFrame() const;
  size_t malformedFrameCount() const;
  size_t invalidCommandCount() const;
  size_t wakeAdvertisementCount() const;

  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  void clearEvents();

  bool emitNotification(const std::vector<uint8_t> &payload);
  bool emitRawNotification(const std::vector<uint8_t> &payload);

  /** Build and emit the 38-byte CmdSet 0x1D, CmdID 0x02 status push. */
  bool emitStatus();
  /** Build and emit the 46-byte CmdSet 0x1D, CmdID 0x06 new status push. */
  bool emitNewStatus();

  /** Approve or reject a pending application-level connection request. */
  bool approveConnection(bool allow);

  /** Controller wake advertisement, 11 bytes with the camera MAC reversed. */
  std::vector<uint8_t> wakeAdvertisement() const;
  /** Apply a received WKP advertisement. Returns true when it wakes the peer. */
  bool receiveWakeAdvertisement(const std::vector<uint8_t> &payload);

  void setScript(const std::vector<ScriptStep> &steps);
  void appendScriptStep(const ScriptStep &step);
  void clearScript();
  size_t scriptRemaining() const;
  void setNextFault(Fault fault);
  Fault pendingFault() const;
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

  static uint16_t crc16(const uint8_t *data, size_t length);
  static uint32_t crc32(const uint8_t *data, size_t length);
  static bool validFrame(const uint8_t *data, size_t length);
  static bool parseFrame(const uint8_t *data, size_t length, Frame &frame);
  static std::vector<uint8_t> buildFrame(uint8_t commandSet,
                                         uint8_t commandId,
                                         uint8_t commandType,
                                         uint16_t sequence,
                                         const std::vector<uint8_t> &payload);

  static const NimBLEUUID &serviceUUID();
  static const NimBLEUUID &notifyCharacteristicUUID();
  static const NimBLEUUID &writeCharacteristicUUID();
  static const NimBLEUUID &externalDataCharacteristicUUID();

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
  static uint16_t readLE16(const uint8_t *data);
  static uint32_t readLE32(const uint8_t *data);
  static void writeLE16(uint8_t *data, uint16_t value);
  static void writeLE32(uint8_t *data, uint32_t value);
  static bool knownCameraMode(uint8_t mode);
  static bool validKeyReport(const uint8_t *payload, size_t length);

  Command classify(const Frame &frame) const;
  Fault takeFault(Command command);
  void noteReturnCode(uint8_t code);
  void recordWrite(const NimBLEUUID &characteristic,
                   Command command,
                   const std::vector<uint8_t> &frame,
                   bool accepted);

  bool sendResponse(uint8_t commandSet,
                    uint8_t commandId,
                    uint8_t commandType,
                    uint16_t sequence,
                    const std::vector<uint8_t> &payload);
  bool sendStatusResponse(uint8_t commandSet,
                          uint8_t commandId,
                          uint16_t sequence,
                          uint8_t returnCode);
  std::vector<uint8_t> cameraConnectionRequest(bool allow) const;
  bool sendCameraConnectionRequest(bool allow);
  bool handleConnectionRequest(const Frame &frame);
  bool handleConnectionResponse(const Frame &frame);
  bool handleCommand(const Frame &frame);
  void resetLink();
  std::vector<uint8_t> statusPayload() const;
  std::vector<uint8_t> newStatusPayload() const;

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_StaleBond = false;
  bool m_SmpRisk = false;
  bool m_SecureAccepted = false;

  bool m_ProtocolReady = false;
  bool m_ConnectionApproved = false;
  bool m_ConnectionRejected = false;
  bool m_PendingApproval = false;
  uint16_t m_PendingSequence = 0;
  uint32_t m_PendingControllerId = 0;
  bool m_SuppressResponses = false;
  uint32_t m_LastCameraDeviceId = 0;
  uint32_t m_CameraIndex = 0;
  bool m_StatusSubscribed = false;
  uint8_t m_PushMode = 0;
  uint8_t m_PushFrequency = 0;

  bool m_Asleep = false;
  bool m_Recording = false;
  uint8_t m_CameraMode = 0x01;
  uint8_t m_LastReturnCode = 0;
  uint8_t m_LastKeyCode = 0;
  uint16_t m_LastKeyValue = 0;
  uint8_t m_LastMode = 0;
  std::vector<uint8_t> m_LastGps;
  Frame m_LastFrame;
  size_t m_MalformedFrames = 0;
  size_t m_InvalidCommands = 0;
  size_t m_WakeAdvertisements = 0;

  uint8_t m_LastAttError = 0;
  std::deque<ScriptStep> m_Script;
  Fault m_NextFault = Fault::None;
  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
  std::map<std::string, Subscription> m_Subscriptions;
};

}  // namespace Host
}  // namespace Furble

#endif
