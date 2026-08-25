#include "nvs.h"

#include <array>
#include <cstring>

namespace {
struct Blob {
  std::array<uint8_t, 64> bytes {};
  size_t length = 0;
  bool present = false;
};

std::array<Blob, 2> committed;
std::array<Blob, 2> staged;
bool stagedValid = false;
bool failCommit = false;

int slotFor(const char *key) {
  if (std::strcmp(key, "furble_slot0") == 0) return 0;
  if (std::strcmp(key, "furble_slot1") == 0) return 1;
  return -1;
}
}  // namespace

extern "C" esp_err_t nvs_open(const char *, uint8_t, nvs_handle_t *handle) {
  if (handle == nullptr) return ESP_FAIL;
  *handle = 1;
  return ESP_OK;
}

extern "C" void nvs_close(nvs_handle_t) {}

extern "C" esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out,
                                   size_t *length) {
  if ((handle != 1) || (length == nullptr)) return ESP_FAIL;
  const int slot = slotFor(key);
  if (slot < 0 || !committed[slot].present) return ESP_ERR_NVS_NOT_FOUND;
  if (out == nullptr) {
    *length = committed[slot].length;
    return ESP_OK;
  }
  if (*length < committed[slot].length) return ESP_ERR_NVS_INVALID_LENGTH;
  std::memcpy(out, committed[slot].bytes.data(), committed[slot].length);
  *length = committed[slot].length;
  return ESP_OK;
}

extern "C" esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
                                   size_t length) {
  if ((handle != 1) || (value == nullptr) || (length > 64)) return ESP_FAIL;
  const int slot = slotFor(key);
  if (slot < 0) return ESP_FAIL;
  staged[slot].bytes.fill(0);
  std::memcpy(staged[slot].bytes.data(), value, length);
  staged[slot].length = length;
  staged[slot].present = true;
  stagedValid = true;
  return ESP_OK;
}

extern "C" esp_err_t nvs_commit(nvs_handle_t handle) {
  if (handle != 1) return ESP_FAIL;
  if (failCommit) {
    failCommit = false;
    stagedValid = false;
    return ESP_FAIL;
  }
  if (stagedValid) {
    for (size_t index = 0; index < committed.size(); index++) {
      if (staged[index].present) committed[index] = staged[index];
    }
  }
  stagedValid = false;
  return ESP_OK;
}

namespace FakeNvs {
void reset() {
  committed = {};
  staged = {};
  stagedValid = false;
  failCommit = false;
}

void reboot() {
  staged = {};
  stagedValid = false;
  failCommit = false;
}

void failNextCommit() {
  failCommit = true;
}

void truncateSlot(uint8_t slot, size_t length) {
  if (slot < committed.size() && committed[slot].present) committed[slot].length = length;
}
}  // namespace FakeNvs
