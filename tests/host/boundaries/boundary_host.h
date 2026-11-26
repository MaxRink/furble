#ifndef FURBLE_BOUNDARY_HOST_H
#define FURBLE_BOUNDARY_HOST_H

#include <cstdint>
#include <string>
#include <vector>

namespace BoundaryHost {

/**
 * A GPIO level change with the host virtual microsecond clock.
 *
 * Only esp_rom_delay_us() advances that clock, so a requested busy-wait shows
 * up as the interval between two edges. It is not wall-clock time and does not
 * prove the device waited, only that production asked the ROM for the delay.
 */
struct GpioEdge {
  int level;
  uint64_t micros;
};

// What currently drives a pad. An RMT channel muxes the pad to its own signal,
// so a plain GPIO write no longer reaches it until the pad is returned.
constexpr int GPIO_PAD_GPIO = 0;
constexpr int GPIO_PAD_PERIPHERAL = 1;

void serviceTimers();
uint32_t timerFireCount();
void advanceTick(uint32_t ms);
uint32_t tick();
uint32_t wakeCalls();
uint32_t psramAllocations();
int gpioDirection(int pin);
int gpioLevel(int pin);
std::vector<GpioEdge> gpioEdges(int pin);
// What currently drives the pad: GPIO_PAD_GPIO (the plain GPIO output path) or
// GPIO_PAD_PERIPHERAL (an RMT-style peripheral signal).
int gpioPad(int pin);
// Record that a peripheral took the pad, which is what rmt_new_tx_channel()
// and rmt_tx_switch_gpio() do on the device.
void setGpioPadPeripheral(int pin);
bool logContains(const char *needle);
// Power source the production Feedback sees through Platform. The default is
// VBUS present, which is the unclamped case.
void setBatteryPower(int16_t vbus_millivolts, bool charging);

}  // namespace BoundaryHost

#endif
