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
 * access here takes the table's own lock, and lookups return a copy, so a
 * caller invokes the subscriber's callback after the lock is released. The
 * lock is a leaf: nothing else is taken under it and no callback runs under
 * it, so a callback may re-enter the camera freely.
 */
template <typename Subscription>
class SubscriptionTable {
 public:
  void set(const std::string &key, const Subscription &subscription) {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Subscriptions[key] = subscription;
  }

  std::optional<Subscription> find(const std::string &key) const {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = m_Subscriptions.find(key);
    if (found == m_Subscriptions.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  void clear(void) {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Subscriptions.clear();
  }

  /** Remove every entry and return the one under key, in one step. */
  std::optional<Subscription> takeAndClear(const std::string &key) {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    std::optional<Subscription> taken;
    const auto found = m_Subscriptions.find(key);
    if (found != m_Subscriptions.end()) {
      taken = found->second;
    }
    m_Subscriptions.clear();
    return taken;
  }

 private:
  mutable std::mutex m_Mutex;
  std::map<std::string, Subscription> m_Subscriptions;
};

}  // namespace Furble::Host

#endif
