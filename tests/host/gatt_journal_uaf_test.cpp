// AddressSanitizer regression for the GATT journal use-after-free.
//
// The crash, found on hardware 2026-08-28: pressing the shutter while a Ricoh
// GR IV dropped the link into standby crashed furble with LoadProhibited on
// EXCVADDR 0xfefefefe. Backtrace: Ricoh::shutterPress -> Ricoh::captureAllowed
// -> Camera::gattRead -> journalRecord -> NimBLEUUID::toString ->
// ble_uuid_to_str on freed memory.
//
// Root cause: the pointer-based Camera::gattRead and Camera::gattWrite read
// the service and characteristic UUIDs from the NimBLERemoteCharacteristic
// AFTER readValue()/writeValue() returned, inside the FURBLE_CONSOLE
// journalRecord call. When the peer drops the link during the operation and
// the reconnect path rediscovers services, NimBLE frees the remote attribute
// objects, so the post-operation dereference lands on freed memory.
//
// The fix snapshots both UUIDs by value BEFORE the operation and journals the
// copies, so the characteristic pointer is never dereferenced again after the
// operation returns.
//
// This binary compiles lib/furble/Camera.cpp with -fsanitize=address and
// FURBLE_CONSOLE, so the journal path is real, not compiled out. The peer's
// faultNextOperation hook frees the client's cached remote service and
// characteristic objects (NimBLEClient::dropServiceCache) while the read or
// write is in flight. With the fix both operations complete cleanly. Restoring
// the post-operation characteristic->getUUID() dereference in either wrapper
// makes ASan abort with heap-use-after-free in the journal path, so the test
// is mutation proven.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "BtDebugJournal.h"
#include "Camera.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLERemoteCharacteristic.h"

const char *LOG_TAG = "gatt-journal-uaf";

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "  FAIL: %s\n", message);
    g_Failures++;
  }
  return condition;
}

// Minimal Camera fixture exposing the protected pointer-based GATT wrappers
// under test, in the style of camera_journal_event_test.
class GattCamera final: public Furble::Camera {
 public:
  GattCamera() : Camera(Type::FAUXNY, PairType::NEW) {}

  void attach(NimBLEClient *client, const NimBLEAddress &address) {
    m_Client = client;
    m_Address = address;
  }
  bool read(NimBLERemoteCharacteristic *characteristic, NimBLEAttValue &value) {
    return gattRead(characteristic, value);
  }
  bool write(NimBLERemoteCharacteristic *characteristic, const uint8_t *data, size_t length) {
    return gattWrite(characteristic, data, length, false);
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

// Fetch the shutter characteristic through the client's normal discovery
// path so the returned pointer is owned by the cached remote service, exactly
// the object dropServiceCache() frees.
NimBLERemoteCharacteristic *shutterCharacteristic(NimBLEClient *client) {
  using Furble::Host::FujifilmVirtualCamera;
  NimBLERemoteService *service = client->getService(FujifilmVirtualCamera::shutterServiceUUID());
  if (service == nullptr) {
    return nullptr;
  }
  return service->getCharacteristic(FujifilmVirtualCamera::shutterCharacteristicUUID());
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
  GattCamera camera;
  camera.attach(client, peer.config().address);
  client->setClientCallbacks(&camera, false);
  check(client->connect(peer.config().address), "mock peer connects through NimBLE client");

  const std::string characteristic_uuid =
      FujifilmVirtualCamera::shutterCharacteristicUUID().toString();

  // gattRead: the mid-operation free lands between readValue() and the journal
  // call. The fixed code journals pre-operation UUID snapshots; the unfixed
  // code dereferences the freed characteristic and ASan aborts here.
  NimBLERemoteCharacteristic *characteristic = shutterCharacteristic(client);
  check(characteristic != nullptr, "shutter characteristic resolves through the service cache");
  peer.faultNextOperation([](NimBLEClient &faulted) { faulted.dropServiceCache(); });
  NimBLEAttValue value;
  check(camera.read(characteristic, value), "gattRead survives a mid-operation attribute free");

  // gattWrite: same window, same free, through the write wrapper. The cache
  // was dropped above, so rediscover a fresh characteristic first.
  characteristic = shutterCharacteristic(client);
  check(characteristic != nullptr, "shutter characteristic rediscovers after the cache drop");
  peer.faultNextOperation([](NimBLEClient &faulted) { faulted.dropServiceCache(); });
  const uint8_t payload[] = {0x01, 0x02};
  check(camera.write(characteristic, payload, sizeof(payload)),
        "gattWrite survives a mid-operation attribute free");

  // Guard against a vacuous run: both operations must have reached the
  // journal with the pre-operation characteristic UUID snapshot intact.
  std::vector<Furble::BtDebugEvent> events;
  BtDebugJournal::instance().dump(0, collect, &events);
  bool sawRead = false;
  bool sawWrite = false;
  for (const auto &event : events) {
    if (event.kind != BtDebugEventKind::GATT) {
      continue;
    }
    if (std::string(event.characteristic_uuid) == characteristic_uuid) {
      sawRead |= std::string(event.operation) == "read";
      sawWrite |= std::string(event.operation) == "write";
    }
  }
  check(sawRead, "journal records the read with the snapshotted characteristic UUID");
  check(sawWrite, "journal records the write with the snapshotted characteristic UUID");

  client->disconnect();
  NimBLEDevice::deleteClient(client);
  BtDebugJournal::instance().setEnabled(false);

  if (g_Failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", g_Failures);
    return 1;
  }
  printf("gatt journal uaf regression passed\n");
  return 0;
}
