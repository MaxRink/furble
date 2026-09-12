#ifndef DJI_OSMO_H
#define DJI_OSMO_H

#include <cstdint>
#include <mutex>

#include <NimBLEAdvertisedDevice.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLEUUID.h>

#include "Camera.h"

namespace Furble {

/**
 * DJI Osmo Action 4 and Action 5 Pro BLE remote.
 *
 * Uses the DJI R SDK protocol over the documented FFF0 service. The first
 * shutter press starts recording and the next press stops it. GPS push is not
 * implemented because the DJI direction does not match furble's geotag path.
 */
class DJIOsmo: public Camera {
 public:
  DJIOsmo(const void *data, size_t len);
  DJIOsmo(const NimBLEAdvertisedDevice *pDevice);

  static bool matches(const NimBLEAdvertisedDevice *pDevice);

  void shutterPress(void) override final;
  void shutterRelease(void) override final;
  void focusPress(void) override final;
  void focusRelease(void) override final;
  void updateGeoData(const gps_t &gps, const timesync_t &timesync) override final;

  size_t getSerialisedBytes(void) const override final;
  bool serialise(void *buffer, size_t bytes) const override final;

 private:
  typedef struct _dji_osmo_t {
    char name[MAX_NAME];
    uint64_t address;
    uint8_t type;
  } dji_osmo_t;

  static constexpr uint8_t FRAME_SOF = 0xAA;
  static constexpr uint16_t FRAME_LENGTH_MASK = 0x03FF;
  static constexpr uint8_t FRAME_RESPONSE_BIT = 0x20;
  static constexpr size_t FRAME_HEADER_LENGTH = 12;
  static constexpr size_t FRAME_COMMAND_LENGTH = 2;
  static constexpr size_t FRAME_TAIL_LENGTH = 4;

  static constexpr uint8_t CMD_SET_COMMON = 0x00;
  static constexpr uint8_t CMD_SET_CAMERA = 0x1D;
  static constexpr uint8_t CMD_CONNECTION = 0x19;
  static constexpr uint8_t CMD_RECORD = 0x03;
  static constexpr uint8_t CMD_STATUS_PUSH = 0x02;
  static constexpr uint8_t CMD_STATUS_SUBSCRIPTION = 0x05;

  static constexpr uint8_t CMD_NO_RESPONSE = 0x00;
  static constexpr uint8_t CMD_RESPONSE_OR_NOT = 0x01;
  static constexpr uint8_t CMD_WAIT_RESULT = 0x02;
  static constexpr uint8_t ACK_NO_RESPONSE = 0x20;

  // DJI documents these as 0xFF33 and 0xFF44. The official demo stores them
  // in little-endian uint32_t fields as 0x33FF0000 and 0x44FF0000.
  static constexpr uint32_t ACTION_4_DEVICE_ID = 0x33FF0000U;
  static constexpr uint32_t ACTION_5_DEVICE_ID = 0x44FF0000U;

  static const NimBLEUUID TARGET_SERVICE_UUID;
  static const NimBLEUUID NOTIFY_CHARACTERISTIC_UUID;
  static const NimBLEUUID WRITE_CHARACTERISTIC_UUID;

  NimBLERemoteCharacteristic *m_Notify = nullptr;
  NimBLERemoteCharacteristic *m_Write = nullptr;
  bool m_WriteWithResponse = false;

  uint32_t m_RemoteDeviceId = 0;
  uint32_t m_CameraDeviceId = 0;
  uint16_t m_NextSequence = 0;
  bool m_Recording = false;
  bool m_ProtocolReady = false;

  bool m_ConnectionRejected = false;
  bool m_CameraRequestReceived = false;
  uint16_t m_CameraRequestSequence = 0;
  uint32_t m_CameraRequestDeviceId = 0;
  uint8_t m_CameraVerifyMode = 0;
  uint16_t m_CameraVerifyData = 0;
  std::mutex m_ProtocolMutex;

  bool _connect(void) override final;
  void _disconnect(void) override final;

  /**
   * The Osmo protocol has no SMP-level comparison to show.
   *
   * DJI's own reference implementation, the Osmo-GPS-Controller-Demo this
   * vendor is built from (see plans/74-dji-osmo.md), never calls
   * esp_ble_gap_set_security_param, sets no IO capability and no
   * ESP_LE_AUTH_* requirement, and issues every GATT operation with
   * ESP_GATT_AUTH_REQ_NONE. The link is just works. What the user approves is
   * the protocol's own first-pair verification and camera approval response
   * over 0xFFF5, not a Bluetooth passkey, so there is no code to compare and a
   * prompt would only be a dead end.
   */
  bool autoAcceptPairing(void) const override final { return true; }

  static uint16_t readLE16(const uint8_t *data);
  static uint32_t readLE32(const uint8_t *data);
  static void writeLE16(uint8_t *data, uint16_t value);
  static void writeLE32(uint8_t *data, uint32_t value);
  static uint16_t crc16(const uint8_t *data, size_t length);
  static uint32_t crc32(const uint8_t *data, size_t length);
  static bool validFrame(const uint8_t *data, size_t length);
  static bool supportedCameraId(uint32_t deviceId);

  uint16_t nextSequence(void);
  bool writeFrame(uint8_t cmdSet,
                  uint8_t cmdId,
                  uint8_t cmdType,
                  const uint8_t *payload,
                  size_t payloadLength,
                  uint16_t sequence);
  bool writeCommand(uint8_t cmdSet,
                    uint8_t cmdId,
                    uint8_t cmdType,
                    const uint8_t *payload,
                    size_t payloadLength);
  bool sendConnectionRequest(void);
  bool finishProtocolConnection(void);
  bool subscribeCameraStatus(void);
  bool writeRecordCommand(bool start);
  void handleNotification(const uint8_t *data, size_t length);
  void clearProtocolState(void);
};

}  // namespace Furble

#endif
