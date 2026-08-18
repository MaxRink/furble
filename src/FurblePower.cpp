#include <esp_log.h>

#include "FurblePower.h"
#include "FurbleTypes.h"

namespace Furble {

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

  uint32_t count = ++lock.count;
  ESP_LOGI(LOG_TAG, "'%s' acquired '%s' power lock, held %u times.", owner, lock.name,
           (unsigned int)count);
}

void Power::release(LockType type, const char *owner) {
  auto &lock = getLock(type);

  if (lock.handle == nullptr) {
    return;
  }

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
  ESP_LOGI(LOG_TAG, "'%s' released '%s' power lock, held %u times.", owner, lock.name,
           (unsigned int)count);
}

uint32_t Power::getCount(LockType type) const {
  return getLock(type).count;
}

const char *Power::getName(LockType type) const {
  return getLock(type).name;
}

Power::Lock::Lock(LockType type, const char *owner) : m_Type(type), m_Owner(owner) {
  Power::getInstance().acquire(m_Type, m_Owner);
}

Power::Lock::~Lock() {
  Power::getInstance().release(m_Type, m_Owner);
}

}  // namespace Furble
