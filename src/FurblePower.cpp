#include <esp_log.h>
#include <esp_timer.h>

#include "FurblePower.h"
#include "FurbleTypes.h"

namespace Furble {

namespace {
constexpr const char *OTHER_OWNER = "other";
}  // namespace

Power &Power::getInstance(void) {
  static Power instance;

  if (!instance.m_Init) {
    for (auto &lock : instance.m_Locks) {
      esp_err_t err = esp_pm_lock_create(lock.type, 0, lock.name, &lock.handle);
      if (err != ESP_OK) {
        // no locks means no power saving control, log and carry on
        ESP_LOGE(LOG_TAG, "Failed to create '%s' power lock (%s).", lock.name,
                 esp_err_to_name(err));
        lock.handle = nullptr;
      }
    }

    instance.m_Init = true;
  }

  return instance;
}

void Power::init(void) {
  (void)getInstance();
}

esp_err_t Power::configure(uint8_t max_freq_mhz) {
  esp_pm_config_t pm_config = {
      .max_freq_mhz = max_freq_mhz,
      .min_freq_mhz = CPU_MIN_FREQ_MHZ,
      .light_sleep_enable = true,
  };
  return esp_pm_configure(&pm_config);
}

const Power::lock_t &Power::getLock(LockType type) const {
  return m_Locks[static_cast<size_t>(type)];
}

Power::lock_t &Power::getLock(LockType type) {
  return m_Locks[static_cast<size_t>(type)];
}

void Power::acquire(LockType type, const char *owner) {
  auto &lock = getLock(type);

  if (lock.handle == nullptr) {
    return;
  }

#if defined(FURBLE_SIM)
  esp_pm_sim_set_owner(owner);
#endif
  esp_err_t err = esp_pm_lock_acquire(lock.handle);
#if defined(FURBLE_SIM)
  esp_pm_sim_set_owner(nullptr);
#endif
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "'%s' failed to acquire '%s' power lock (%s).", owner, lock.name,
             esp_err_to_name(err));
    return;
  }

  const std::lock_guard<std::mutex> guard(m_StatsMutex);
  uint32_t count = ++lock.count;
  lock.totalAcquires.fetch_add(1);
  recordOwner(lock, owner);
  if (count == 1) {
    lock.heldSinceUs.store(esp_timer_get_time());
  }
  ESP_LOGI(LOG_TAG, "'%s' acquired '%s' power lock, held %u times.", owner, lock.name,
           (unsigned int)count);
}

void Power::release(LockType type, const char *owner) {
  auto &lock = getLock(type);

  if (lock.handle == nullptr) {
    return;
  }

  const std::lock_guard<std::mutex> guard(m_StatsMutex);

  if (lock.count == 0) {
    // an unbalanced release is a bug, do not confuse esp_pm with it
    ESP_LOGE(LOG_TAG, "'%s' released unheld '%s' power lock.", owner, lock.name);
    return;
  }

#if defined(FURBLE_SIM)
  esp_pm_sim_set_owner(owner);
#endif
  esp_err_t err = esp_pm_lock_release(lock.handle);
#if defined(FURBLE_SIM)
  esp_pm_sim_set_owner(nullptr);
#endif
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "'%s' failed to release '%s' power lock (%s).", owner, lock.name,
             esp_err_to_name(err));
    return;
  }

  uint32_t count = --lock.count;
  if (count == 0) {
    const int64_t now = esp_timer_get_time();
    const int64_t heldSince = lock.heldSinceUs.load();
    if (now > heldSince) {
      lock.totalHeldUs.fetch_add(now - heldSince);
    }
    lock.heldSinceUs.store(0);
  }
  ESP_LOGI(LOG_TAG, "'%s' released '%s' power lock, held %u times.", owner, lock.name,
           (unsigned int)count);
}

uint32_t Power::getCount(LockType type) const {
  return getLock(type).count;
}

const char *Power::getName(LockType type) const {
  return getLock(type).name;
}

void Power::recordOwner(lock_t &lock, const char *owner) {
  const char *ownerName = (owner != nullptr) ? owner : OTHER_OWNER;

  // Owners are matched by pointer identity, not by string content. Every caller
  // must therefore pass a string LITERAL (or another pointer with static
  // storage duration), so repeated acquisitions from the same owner share one
  // stable address and aggregate into a single slot. A heap or stack buffer
  // holding the same characters would land in a new slot each time.
  for (size_t n = 0; n < OWNER_SLOTS - 1; n++) {
    auto &slot = lock.owners[n];
    if (slot.owner == ownerName) {
      slot.acquires++;
      return;
    }
    if (slot.owner == nullptr) {
      slot.owner = ownerName;
      slot.acquires = 1;
      return;
    }
  }

  auto &other = lock.owners[OWNER_SLOTS - 1];
  other.owner = OTHER_OWNER;
  other.acquires++;
}

Power::stats_t Power::getStats(LockType type) const {
  const auto &lock = getLock(type);
  const std::lock_guard<std::mutex> guard(m_StatsMutex);
  stats_t stats = {};

  stats.count = lock.count.load();
  stats.totalAcquires = lock.totalAcquires.load();
  stats.heldSinceUs = lock.heldSinceUs.load();
  stats.totalHeldUs = static_cast<uint64_t>(lock.totalHeldUs.load());

  if (stats.count > 0) {
    const int64_t now = esp_timer_get_time();
    if (now > stats.heldSinceUs) {
      stats.totalHeldUs += static_cast<uint64_t>(now - stats.heldSinceUs);
    }
  }

  for (size_t n = 0; n < OWNER_SLOTS; n++) {
    stats.owners[n] = {lock.owners[n].owner, lock.owners[n].acquires};
  }

  return stats;
}

Power::Lock::Lock(LockType type, const char *owner) : m_Type(type), m_Owner(owner) {
  Power::getInstance().acquire(m_Type, m_Owner);
}

Power::Lock::~Lock() {
  Power::getInstance().release(m_Type, m_Owner);
}

}  // namespace Furble
