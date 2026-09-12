// Minimal M5Unified surface for the IMU motion engine host test. Only the two
// members the engines touch are modelled: the IMU enable flag and the internal
// I2C bus. The bus is backed by tests/host/imu_motion_encoding_test.cpp, which
// records every transaction so the test can assert the register sequence.
#ifndef FURBLE_HOST_M5UNIFIED_H
#define FURBLE_HOST_M5UNIFIED_H

#include <cstddef>
#include <cstdint>

namespace HostI2C {
bool read(uint8_t address, uint8_t reg, uint8_t *data, size_t length);
bool write(uint8_t address, uint8_t reg, const uint8_t *data, size_t length);
bool imuEnabled(void);
}  // namespace HostI2C

struct HostImu {
  bool isEnabled(void) const { return HostI2C::imuEnabled(); }
};

struct HostBus {
  bool readRegister(uint8_t address, uint8_t reg, uint8_t *data, size_t length, uint32_t) {
    return HostI2C::read(address, reg, data, length);
  }
  bool writeRegister(uint8_t address, uint8_t reg, const uint8_t *data, size_t length, uint32_t) {
    return HostI2C::write(address, reg, data, length);
  }
};

struct HostM5 {
  HostImu Imu;
  HostBus In_I2C;
};

extern HostM5 M5;

#endif
