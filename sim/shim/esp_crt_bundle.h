#ifndef FURBLE_SIM_ESP_CRT_BUNDLE_H
#define FURBLE_SIM_ESP_CRT_BUNDLE_H

#include "esp_err.h"

// The optional simulator transport accepts mqtt:// only. This symbol keeps
// the production config path intact; mqtts:// is rejected by that transport.
inline esp_err_t esp_crt_bundle_attach(void *) {
  return ESP_OK;
}

#endif
