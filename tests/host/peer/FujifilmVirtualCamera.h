#ifndef FURBLE_HOST_FUJIFILM_VIRTUAL_CAMERA_H
#define FURBLE_HOST_FUJIFILM_VIRTUAL_CAMERA_H

#include <array>
#include <string>
#include <utility>
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
    bool secure = false;
    std::array<uint8_t, 5> serial = {0x01, 0x02, 0x03, 0x04, 0x05};
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

  // Model a stale-session reconnect. When enabled the camera still holds the
  // CCCD subscriptions from the previous session, so an acknowledged CCCD
  // subscribe write (response = true) never gets its ATT write response and, on
  // real hardware, blocks the connect. The mock returns false for that write to
  // stand in for the block. An unacknowledged subscribe write (response = false)
  // is accepted, which is the bounded path the fix uses.
  void setStaleSubscribeSession(bool stale);

  // Fault injection for adversarial connect and command error paths.
  //
  // suppressService models a camera that does not expose a GATT service at all,
  // so m_Client->getService() returns nullptr. This stands in for an out-of-spec
  // or firmware-variant peer, or a partial GATT discovery. A vendor connect that
  // only logs a null service and then dereferences it crashes here.
  //
  // suppressCharacteristic models a service that is present but missing one
  // characteristic, so getCharacteristic() returns nullptr. A vendor connect
  // that tolerates the null instead of failing reports a connected camera whose
  // command path is silently dead.
  //
  // failWrite models an ATT write that the peer rejects (returns an error
  // status). A handshake write that fails must abort the connect and reclaim the
  // client without leaking it from the fixed-size pool.
  void suppressService(const NimBLEUUID &service);
  void suppressCharacteristic(const NimBLEUUID &service, const NimBLEUUID &characteristic);
  void failWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic);

  // dropLinkOnWrite models a supervision-timeout link loss that lands in the
  // middle of the connect handshake: when the central writes the named
  // characteristic the peer severs the link and delivers onDisconnect inline,
  // then reports the write as failed. The half-finished _connect must unwind
  // cleanly, leave the camera disconnected, and reclaim the client rather than
  // leaking it or dereferencing a torn-down link.
  void dropLinkOnWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic);

  // dropLinkDuringConnect models the peer resetting (a power-cycle) in the middle
  // of the connect handshake. When the central writes the named characteristic
  // the peer completes the write normally, then severs the link with an inline
  // self-deleting drop (NimBLEClient::mockDropLinkSelfDelete): onDisconnect fires
  // and, if the client is armed for delete-on-disconnect, the client is freed
  // right there, exactly as the NimBLE host task frees a setSelfDelete client.
  // Unlike dropLinkOnWrite the write still reports success, so _connect() keeps
  // going and performs its next m_Client dereference. If the connect path does
  // not own the client lifetime for the whole handshake, that dereference lands
  // on the freed client, which is the mid-connect use-after-free this guards.
  void dropLinkDuringConnect(const NimBLEUUID &service, const NimBLEUUID &characteristic);

  // Clear every injected fault (suppressed services and characteristics, failed
  // writes, mid-handshake drops and the stale-subscribe flag). The fuzz harness
  // reuses one persistent peer across many lifecycle operations and calls this to
  // return the peer to a healthy baseline between operations.
  void clearFaults();

  const Config &config() const;
  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  const std::vector<uint8_t> &lastGeotag() const;
  const std::string &identifier() const;
  bool connected() const;
  void setSecureConnectionResult(bool result);
  void setRequireLongConnParamsAfterIdentifier(bool require);
  void setDelayRegistrationConnParamsUntilFastRequest(bool delay);
  void dropLinkOnSubscribe(const NimBLEUUID &service, const NimBLEUUID &characteristic);
  void requestConnParamsDuringConnect(const ble_gap_upd_params &params);
  bool registrationConnParamsAccepted() const;
  bool tokenAccepted() const;
  bool configured() const;
  bool geotagRequested() const;
  size_t accessAfterDrop() const;

  void clearEvents();

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

  bool isServiceSuppressed(const NimBLEUUID &service) const;
  bool isCharacteristicSuppressed(const NimBLEUUID &service,
                                  const NimBLEUUID &characteristic) const;
  bool isWriteFailed(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;
  bool isDropOnWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;
  bool isDropDuringConnect(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;
  bool isDropOnSubscribe(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_SecureConnectionResult = true;
  bool m_RequireLongConnParamsAfterIdentifier = false;
  bool m_DelayRegistrationConnParamsUntilFastRequest = false;
  bool m_ConnParamsNegotiated = false;
  bool m_RequestConnParamsDuringConnect = false;
  ble_gap_upd_params m_RegistrationConnParams {};
  bool m_RegistrationConnParamsAccepted = false;
  bool m_StaleSubscribeSession = false;
  std::vector<NimBLEUUID> m_SuppressedServices;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_SuppressedCharacteristics;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_FailedWrites;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_DropOnWrite;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_DropDuringConnect;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_DropOnSubscribe;
  mutable size_t m_AccessAfterDrop = 0;
  bool m_DroppedLink = false;
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
