#ifndef FURBLE_HOST_RICOH_VIRTUAL_CAMERA_H
#define FURBLE_HOST_RICOH_VIRTUAL_CAMERA_H

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MockNimBLE.h"

namespace Furble {
namespace Host {

class RicohVirtualCamera final: public NimBLEMockPeer {
 public:
  struct Config {
    std::string name = "RICOH GR III";
    NimBLEAddress address = NimBLEAddress(0x223344556677ULL, 0);
    bool camera_bonded = false;
    bool accept_numeric_comparison = true;
    // CameraPower and OperationMode single-byte values. A GR IV in BLE
    // standby keeps the link up with power ON (0x01) while OperationMode
    // reads BLE_STARTUP (0x02) instead of CAPTURE (0x00).
    uint8_t camera_power = 0x01;
    uint8_t operation_mode = 0x00;
    bool operation_mode_read_fails = false;
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

  static const NimBLEUUID &shootingFlavorCharacteristicUUID();
  static const NimBLEUUID &operationRequestCharacteristicUUID();
  static const NimBLEUUID &cameraServiceUUID();
  static const NimBLEUUID &powerCharacteristicUUID();

 private:
  struct Subscription {
    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *remote = nullptr;
    NimBLENotifyCallback callback;
    bool notification = false;
  };

  static bool matches(const NimBLEUUID &left, const NimBLEUUID &right);
  static std::string key(const NimBLEUUID &service, const NimBLEUUID &characteristic);
  void armFlappyDrop(NimBLEClient &client);
  void requestFlappyCancel();
  void cancelFlappyTimer();

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
  std::map<std::string, Subscription> m_Subscriptions;

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
};

}  // namespace Host
}  // namespace Furble

#endif
