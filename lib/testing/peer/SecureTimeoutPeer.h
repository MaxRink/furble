#ifndef FURBLE_HOST_SECURE_TIMEOUT_PEER_H
#define FURBLE_HOST_SECURE_TIMEOUT_PEER_H

#include <chrono>
#include <cstdint>
#include <thread>
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
 *
 * With a non-zero block_ms the handshake also blocks for that long before
 * failing, the way NimBLE holds the connect task inside secureConnection() for
 * the whole pairing timeout (up to 30 s for rc=13 on hardware). The wait ends
 * early when the link goes down, so a cancel that terminates the link makes
 * the call return promptly, which is exactly the behaviour under test.
 */
class SecureTimeoutPeer final: public NimBLEMockPeer {
 public:
  explicit SecureTimeoutPeer(NimBLEMockPeer &inner, int reason = 520, uint32_t block_ms = 0)
      : m_Inner(inner), m_Reason(reason), m_BlockMs(block_ms) {}

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
    // Hold the caller the way NimBLE holds it, waking early if the link is
    // torn down from another task.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_BlockMs);
    while ((m_BlockMs > 0) && client.isConnected()
           && (std::chrono::steady_clock::now() < deadline)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!client.isConnected()) {
      // The link went away under the handshake, so there is nothing left to
      // mark dead. Report the failure and let the connect path unwind.
      return false;
    }
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
  uint32_t m_BlockMs;
};

}  // namespace Host
}  // namespace Furble

#endif
