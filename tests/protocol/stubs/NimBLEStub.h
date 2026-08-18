#ifndef FURBLE_PROTOCOL_NIMBLE_STUB_H
#define FURBLE_PROTOCOL_NIMBLE_STUB_H

#include <cstdint>

#define BLE_HS_CONN_HANDLE_NONE 0xffff

class NimBLEServer {};
class NimBLEConnInfo {};
class NimBLEAttValue {};
class NimBLECharacteristic {};
class NimBLEAdvertising {};
class NimBLEService {};
class NimBLEAddress {};

class NimBLEServerCallbacks {
 public:
  virtual ~NimBLEServerCallbacks() = default;
  virtual void onConnect(NimBLEServer *, NimBLEConnInfo &) {}
  virtual void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) {}
  virtual void onConfirmPassKey(NimBLEConnInfo &, uint32_t) {}
  virtual void onAuthenticationComplete(NimBLEConnInfo &) {}
};

class NimBLECharacteristicCallbacks {
 public:
  virtual ~NimBLECharacteristicCallbacks() = default;
  virtual void onRead(NimBLECharacteristic *, NimBLEConnInfo &) {}
  virtual void onWrite(NimBLECharacteristic *, NimBLEConnInfo &) {}
  virtual void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t) {}
};

#endif
