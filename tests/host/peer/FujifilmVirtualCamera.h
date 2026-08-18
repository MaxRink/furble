#ifndef FURBLE_HOST_FUJIFILM_VIRTUAL_CAMERA_H
#define FURBLE_HOST_FUJIFILM_VIRTUAL_CAMERA_H

#include <array>
#include <string>
#include <vector>

#include "MockNimBLE.h"

namespace Furble {
namespace Host {

/**
 * A small Fujifilm Basic peer for host protocol tests.
 *
 * It models the server side of the unencrypted Fujifilm GATT flow. The class
 * has no radio implementation. NimBLEMockPeer supplies the client transport,
 * so the same event sequence can later be driven by a real peer adapter.
 */
class FujifilmVirtualCamera final: public NimBLEMockPeer {
 public:
  struct Config {
    std::string name = "FUJIFILM X100VI";
    NimBLEAddress address = NimBLEAddress(0x112233445566ULL, 0);
    std::array<uint8_t, 4> token = {0xa1, 0xb2, 0xc3, 0xd4};
    std::vector<NimBLEUUID> advertised_services;
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

  FujifilmVirtualCamera();
  explicit FujifilmVirtualCamera(const Config &config);

  NimBLEAdvertisedDevice advertisement() const;

  /** Ask the client for a geotag update using the camera's normal notification. */
  bool requestGeotag();

  /** Replay a notification from a normalized capture. */
  bool emitNotification(const NimBLEUUID &service,
                        const NimBLEUUID &characteristic,
                        const std::vector<uint8_t> &payload,
                        bool indication = false);

  const Config &config() const;
  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  const std::vector<uint8_t> &lastGeotag() const;
  const std::string &identifier() const;
  bool connected() const;
  bool tokenAccepted() const;
  bool configured() const;
  bool geotagRequested() const;

  void clearEvents();

  bool acceptConnection(NimBLEClient &client, const NimBLEAddress &address) override;
  void disconnect(NimBLEClient &client, int reason) override;
  bool hasService(const NimBLEUUID &service) const override;
  bool hasCharacteristic(const NimBLEUUID &service,
                         const NimBLEUUID &characteristic) const override;
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

  static const NimBLEUUID &pairServiceUUID();
  static const NimBLEUUID &pairCharacteristicUUID();
  static const NimBLEUUID &identifierCharacteristicUUID();
  static const NimBLEUUID &configurationServiceUUID();
  static const NimBLEUUID &configurationNotificationUUID();
  static const NimBLEUUID &geotagRequestCharacteristicUUID();
  static const NimBLEUUID &configurationIndication1UUID();
  static const NimBLEUUID &configurationIndication2UUID();
  static const NimBLEUUID &configurationIndication3UUID();
  static const NimBLEUUID &shutterServiceUUID();
  static const NimBLEUUID &shutterCharacteristicUUID();
  static const NimBLEUUID &geotagServiceUUID();
  static const NimBLEUUID &geotagCharacteristicUUID();
  static const NimBLEUUID &advertisedServiceUUID();

 private:
  struct Subscription {
    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *remote = nullptr;
    NimBLENotifyCallback callback;
    bool notification = false;
  };

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_TokenAccepted = false;
  bool m_Configured = false;
  bool m_GeotagRequested = false;
  std::string m_Identifier;
  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
  std::vector<uint8_t> m_LastGeotag;
  std::map<std::string, Subscription> m_Subscriptions;

  static std::string key(const NimBLEUUID &service, const NimBLEUUID &characteristic);
};

}  // namespace Host
}  // namespace Furble

#endif
