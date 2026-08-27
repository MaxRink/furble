#include <cstdio>
#include <string>
#include <vector>

#include "BtDebugJournal.h"
#include "Camera.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLERemoteCharacteristic.h"

namespace {
class JournalCamera final: public Furble::Camera {
 public:
  JournalCamera() : Camera(Type::FAUXNY, PairType::NEW) {}

  void attach(NimBLEClient *client, const NimBLEAddress &address) {
    m_Client = client;
    m_Address = address;
  }
  bool write(const NimBLEUUID &service, const NimBLEUUID &characteristic) {
    const uint8_t value[] = {0xa1, 0xb2};
    return gattWrite(service, characteristic, value, sizeof(value), false);
  }
  bool subscribe(NimBLERemoteCharacteristic *characteristic) {
    return gattSubscribe(characteristic, nullptr, false, true);
  }

  void shutterPress() override {}
  void shutterRelease() override {}
  void focusPress() override {}
  void focusRelease() override {}
  void updateGeoData(const gps_t &, const timesync_t &) override {}
  size_t getSerialisedBytes() const override { return 0; }
  bool serialise(void *, size_t) const override { return false; }

 protected:
  bool _connect() override { return false; }
  void _disconnect() override {}
};

void collect(const Furble::BtDebugEvent &event, void *context) {
  static_cast<std::vector<Furble::BtDebugEvent> *>(context)->push_back(event);
}

bool check(bool value, const char *message) {
  if (!value) {
    fprintf(stderr, "FAIL: %s\n", message);
  }
  return value;
}
}  // namespace

int main() {
  using Furble::BtDebugEventKind;
  using Furble::BtDebugJournal;
  using Furble::Host::FujifilmVirtualCamera;

  BtDebugJournal::instance().clear();
  BtDebugJournal::instance().setEnabled(true);
  FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeerForAddress(peer.config().address, &peer);
  NimBLEClient *client = NimBLEDevice::createClient();
  JournalCamera camera;
  camera.attach(client, peer.config().address);
  client->setClientCallbacks(&camera, false);
  check(client->connect(peer.config().address), "mock peer connects through NimBLE client");

  const NimBLEUUID service = FujifilmVirtualCamera::shutterServiceUUID();
  const NimBLEUUID characteristic = FujifilmVirtualCamera::shutterCharacteristicUUID();
  check(camera.write(service, characteristic), "Camera GATT wrapper emits a real write event");
  auto *remote = new NimBLERemoteCharacteristic(client, service, characteristic);
  check(camera.subscribe(remote), "Camera GATT wrapper emits a real subscribe event");

  std::vector<Furble::BtDebugEvent> events;
  BtDebugJournal::instance().dump(0, collect, &events);
  bool sawConnect = false;
  bool sawWrite = false;
  bool sawSubscribe = false;
  for (const auto &event : events) {
    sawConnect |= event.kind == BtDebugEventKind::GAP_CONNECT;
    sawWrite |= event.kind == BtDebugEventKind::GATT && std::string(event.operation) == "write"
                && !event.response && event.payload_length == 2;
    sawSubscribe |= event.kind == BtDebugEventKind::GATT
                    && std::string(event.operation) == "subscribe" && event.response
                    && event.payload_length == 2 && event.payload[0] == 1;
  }
  bool hasIdentity = false;
  for (const auto &event : events) {
    hasIdentity |= event.kind == BtDebugEventKind::GAP_CONNECT && event.address[0] != '\0'
                   && event.identity[0] != '\0';
  }
  check(sawConnect && hasIdentity, "connect callback records negotiated address identity");
  check(sawWrite, "typed journal records write operation and response mode");
  check(sawSubscribe, "typed journal records CCCD value and response mode");
  client->disconnect();
  NimBLEDevice::deleteClient(client);
  delete remote;
  BtDebugJournal::instance().setEnabled(false);
  return 0;
}
