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
bool failCommit = false;
bool failSet = false;
bool tearSet = false;
size_t tearLength = 0;
int failReadSlot = -1;

int slotFor(const char *key) {
  if (std::strcmp(key, "furble_slot0") == 0)
    return 0;
  if (std::strcmp(key, "furble_slot1") == 0)
    return 1;
  return -1;
}
}  // namespace

extern "C" esp_err_t nvs_open(const char *, uint8_t, nvs_handle_t *handle) {
  if (handle == nullptr)
    return ESP_FAIL;
  *handle = 1;
  return ESP_OK;
}

extern "C" void nvs_close(nvs_handle_t) {}

extern "C" esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length) {
  if ((handle != 1) || (length == nullptr))
    return ESP_FAIL;
  const int slot = slotFor(key);
  if (slot == failReadSlot) {
    failReadSlot = -1;
    return ESP_FAIL;
  }
  if (slot < 0 || !committed[slot].present)
    return ESP_ERR_NVS_NOT_FOUND;
  if (out == nullptr) {
    *length = committed[slot].length;
    return ESP_OK;
  }
  if (*length < committed[slot].length)
    return ESP_ERR_NVS_INVALID_LENGTH;
  std::memcpy(out, committed[slot].bytes.data(), committed[slot].length);
  *length = committed[slot].length;
  return ESP_OK;
}

extern "C" esp_err_t nvs_set_blob(nvs_handle_t handle,
                                  const char *key,
                                  const void *value,
                                  size_t length) {
  if ((handle != 1) || (value == nullptr) || (length > 64))
    return ESP_FAIL;
  const int slot = slotFor(key);
  if (slot < 0)
    return ESP_FAIL;
  if (failSet) {
    failSet = false;
    return ESP_FAIL;
  }
  committed[slot].bytes.fill(0);
  const size_t copied = tearSet ? (tearLength < length ? tearLength : length) : length;
  std::memcpy(committed[slot].bytes.data(), value, copied);
  committed[slot].length = length;
  committed[slot].present = true;
  const bool torn = tearSet;
  tearSet = false;
  tearLength = 0;
  return torn ? ESP_FAIL : ESP_OK;
}

extern "C" esp_err_t nvs_commit(nvs_handle_t handle) {
  if (handle != 1)
    return ESP_FAIL;
  if (failCommit) {
    failCommit = false;
    return ESP_FAIL;
  }
  return ESP_OK;
}

namespace FakeNvs {
void reset() {
  committed = {};
  failCommit = false;
  failSet = false;
  tearSet = false;
  tearLength = 0;
  failReadSlot = -1;
}

void reboot() {
  failCommit = false;
  failSet = false;
  tearSet = false;
  tearLength = 0;
  failReadSlot = -1;
}

void failNextCommit() {
  failCommit = true;
}

void failNextSet() {
  failSet = true;
}

void tearNextSet(size_t length) {
  tearSet = true;
  tearLength = length;
}

void failNextRead(uint8_t slot) {
  failReadSlot = slot;
}

void truncateSlot(uint8_t slot, size_t length) {
  if (slot < committed.size() && committed[slot].present)
    committed[slot].length = length;
}
}  // namespace FakeNvs
