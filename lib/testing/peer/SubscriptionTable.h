#ifndef FURBLE_TESTING_PEER_SUBSCRIPTION_TABLE_H
#define FURBLE_TESTING_PEER_SUBSCRIPTION_TABLE_H

#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace Furble::Host {

/**
 * The subscriptions a virtual camera holds, keyed by service and
 * characteristic.
 *
 * The central subscribes and receives notifications on its connect task while
 * a disconnect or reset clears the table on another thread. A plain map let a
 * notification read a node that the clear had just freed (issue 451). Every
 * access here takes the table's own lock.
 *
 * A notification must also hold the lock across its callback, through
 * deliveryLock(). The callback receives the subscriber's raw
 * NimBLERemoteCharacteristic, which the client frees when it self-deletes
 * after the peer's disconnect. Holding the lock makes that clear wait for an
 * in-flight callback, and a later notification finds nothing, so the
 * characteristic outlives every callback that receives it. The real NimBLE
 * host gives the same order: it runs notifications and the disconnect event
 * on one task. The lock is recursive, so a callback may re-enter the camera
 * on its own thread. A callback must not wait on another thread that may be
 * clearing this table; production notification callbacks only record state.
 */
template <typename Subscription>
class SubscriptionTable {
 public:
  /** Hold across a lookup and the callback it reaches. */
  std::unique_lock<std::recursive_mutex> deliveryLock(void) const {
    return std::unique_lock<std::recursive_mutex>(m_Mutex);
  }

  void set(const std::string &key, const Subscription &subscription) {
    const std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_Subscriptions[key] = subscription;
  }

  std::optional<Subscription> find(const std::string &key) const {
    const std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const auto found = m_Subscriptions.find(key);
    if (found == m_Subscriptions.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  void clear(void) {
    const std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_Subscriptions.clear();
  }

  /** Remove every entry and return the one under key, in one step. */
  std::optional<Subscription> takeAndClear(const std::string &key) {
    const std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    std::optional<Subscription> taken;
    const auto found = m_Subscriptions.find(key);
    if (found != m_Subscriptions.end()) {
      taken = found->second;
    }
    m_Subscriptions.clear();
    return taken;
  }

 private:
  mutable std::recursive_mutex m_Mutex;
  std::map<std::string, Subscription> m_Subscriptions;
};

}  // namespace Furble::Host

#endif
