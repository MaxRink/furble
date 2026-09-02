// Host esp_bt shim for the console command suite.
//
// esp_power_level_t is defined once, by the mock NimBLE header that every
// other console target source already sees through Camera.h. Reusing that one
// definition keeps the enumerator values identical in every translation unit,
// which matters because Settings persists TX_POWER by value.
#ifndef FURBLE_HOST_CONSOLE_ESP_BT_H
#define FURBLE_HOST_CONSOLE_ESP_BT_H

#include <MockNimBLE.h>

#endif
