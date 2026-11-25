#ifndef FURBLE_BOUNDARY_ESP_ROM_SYS_H
#define FURBLE_BOUNDARY_ESP_ROM_SYS_H

#include <cstdint>

// Host double for the ROM busy-wait. The implementation in boundary_support.cpp
// advances the same virtual microsecond clock that timestamps the GPIO edges,
// so a test can measure a production pulse width without a real clock.
void esp_rom_delay_us(uint32_t microseconds);

#endif
