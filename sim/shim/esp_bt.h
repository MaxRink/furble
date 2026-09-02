#ifndef FURBLE_SIM_ESP_BT_H
#define FURBLE_SIM_ESP_BT_H

// The simulator links the production BLE stack against MockNimBLE, which owns
// the esp_power_level_t definition. Include it so a source reaching for the
// ESP-IDF Bluetooth header sees exactly one definition of the enum.
#include <MockNimBLE.h>

#endif
