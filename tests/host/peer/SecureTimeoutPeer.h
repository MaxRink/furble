#ifndef FURBLE_HOST_SECURE_TIMEOUT_PEER_H
#define FURBLE_HOST_SECURE_TIMEOUT_PEER_H

#include <vector>

#include "MockNimBLE.h"

namespace Furble {
namespace Host {

/**
 * Decorator peer that fails the security handshake the way a supervision
 * timeout does.
 *
 * Every operation delegates to the wrapped peer except secureConnection(),
 * which marks the link dead with the disconnect event still queued on the
 * host task and reports failure. That is exactly how rc=520 (0x200 | HCI 0x08
 * Connection Timeout) leaves the client: the connect task wakes with a failed
 * handshake while isConnected() still reads true because only the queued
 * BLE_GAP_EVENT_DISCONNECT clears the conn handle. Promoted out of
 * ricoh_secure_timeout_uaf_test so any vendor's connect path can be driven
 * through the same failure.
 */
class SecureTimeoutPeer final: public NimBLEMockPeer {
 public:
  explicit SecureTimeoutPeer(NimBLEMockPeer &inner, int reason = 520)
      : m_Inner(inner), m_Reason(reason) {}

  bool acceptConnection(NimBLEClient &client, const NimBLEAddress &address) override {
    return m_Inner.acceptConnection(client, address);
  }
  void disconnect(NimBLEClient &client, int reason) override { m_Inner.disconnect(client, reason); }
  bool hasService(const NimBLEUUID &service) const override { return m_Inner.hasService(service); }
  bool hasCharacteristic(const NimBLEUUID &service,
                         const NimBLEUUID &characteristic) const override {
    return m_Inner.hasCharacteristic(service, characteristic);
  }
  bool discoverCharacteristic(NimBLEClient &client,
                              const NimBLEUUID &service,
                              const NimBLEUUID &characteristic) override {
    return m_Inner.discoverCharacteristic(client, service, characteristic);
  }
  bool canWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) const override {
    return m_Inner.canWrite(service, characteristic);
  }
  bool write(NimBLEClient &client,
             const NimBLEUUID &service,
             const NimBLEUUID &characteristic,
             const std::vector<uint8_t> &value,
             bool response) override {
    return m_Inner.write(client, service, characteristic, value, response);
  }
  NimBLEAttValue read(NimBLEClient &client,
                      const NimBLEUUID &service,
                      const NimBLEUUID &characteristic) override {
    return m_Inner.read(client, service, characteristic);
  }
  bool subscribe(NimBLEClient &client,
                 const NimBLEUUID &service,
                 const NimBLEUUID &characteristic,
                 bool notification,
                 NimBLERemoteCharacteristic *remote,
                 const NimBLENotifyCallback &callback,
                 bool response) override {
    return m_Inner.subscribe(client, service, characteristic, notification, remote, callback,
                             response);
  }
  bool secureConnection(NimBLEClient &client) override {
    // The link died under the encryption handshake. The failure wakes the
    // connect task while the disconnect event is still queued on the host
    // task, so the client keeps reporting connected.
    client.mockMarkLinkDeadEventPending(m_Reason);
    return false;
  }
  bool updateConnectionParams(NimBLEClient &client,
                              uint16_t min_interval,
                              uint16_t max_interval,
                              uint16_t latency,
                              uint16_t timeout) override {
    return m_Inner.updateConnectionParams(client, min_interval, max_interval, latency, timeout);
  }
  int getRssi() const override { return m_Inner.getRssi(); }

 private:
  NimBLEMockPeer &m_Inner;
  int m_Reason;
};

}  // namespace Host
}  // namespace Furble

#endif
