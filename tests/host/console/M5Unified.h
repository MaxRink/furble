// Host M5Unified shim for the console command suite.
//
// Only the inertial sensor probe behind 'imu status' is modelled. The sensor
// type, the enabled flag and the sample values are test settable so both the
// disabled early return and the full sample report run.
#ifndef FURBLE_HOST_CONSOLE_M5UNIFIED_H
#define FURBLE_HOST_CONSOLE_M5UNIFIED_H

#include <cstdint>

namespace m5 {

enum imu_t {
  imu_none,
  imu_unknown,
  imu_sh200q,
  imu_mpu6050,
  imu_mpu6886,
  imu_mpu9250,
  imu_bmi270,
};

class IMU_Class {
 public:
  enum sensor_mask_t {
    sensor_mask_none = 0,
    sensor_mask_accel = 1,
    sensor_mask_gyro = 2,
  };

  bool isEnabled(void) const { return m_Enabled; }
  imu_t getType(void) const { return m_Type; }
  sensor_mask_t update(void) { return m_Updated; }

  bool getAccel(float *x, float *y, float *z) const {
    *x = m_Accel[0];
    *y = m_Accel[1];
    *z = m_Accel[2];
    return m_AccelRead;
  }

  bool getGyro(float *x, float *y, float *z) const {
    *x = m_Gyro[0];
    *y = m_Gyro[1];
    *z = m_Gyro[2];
    return m_GyroRead;
  }

  // Test surface.
  void setEnabled(bool enabled) { m_Enabled = enabled; }
  void setType(imu_t type) { m_Type = type; }
  void setUpdated(sensor_mask_t mask) { m_Updated = mask; }
  void setAccel(float x, float y, float z) {
    m_Accel[0] = x;
    m_Accel[1] = y;
    m_Accel[2] = z;
  }
  void setGyro(float x, float y, float z) {
    m_Gyro[0] = x;
    m_Gyro[1] = y;
    m_Gyro[2] = z;
  }
  void setAccelRead(bool read) { m_AccelRead = read; }
  void setGyroRead(bool read) { m_GyroRead = read; }

 private:
  bool m_Enabled = true;
  imu_t m_Type = imu_mpu6886;
  sensor_mask_t m_Updated = sensor_mask_accel;
  float m_Accel[3] = {0.0f, 0.0f, 1.0f};
  float m_Gyro[3] = {0.0f, 0.0f, 0.0f};
  bool m_AccelRead = true;
  bool m_GyroRead = true;
};

}  // namespace m5

class M5HostConsole {
 public:
  m5::IMU_Class Imu;
};

extern M5HostConsole M5;

#endif
