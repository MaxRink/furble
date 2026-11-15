// LUMIX teardown survives a self-deleting client.
//
// The GPS stop packet is written from _disconnect(). A camera that drops the
// link at that instant makes NimBLE free its self-deleting client before the
// teardown sequence finishes. Without a lifetime guard the following Finish
// write dereferences freed memory; with it, the sequence re-checks the link and
// stops cleanly.

#include <iostream>

#include "Device.h"
#include "Lumix.h"
#include "LumixVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "lumix-teardown-drop";

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);

  Furble::Host::LumixVirtualCamera::Config config;
  config.drop_on_gps_stop = true;
  Furble::Host::LumixVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::Lumix camera(&advertisement);

  if (!camera.connect(Furble::TxPower::P3, 2000)) {
    std::cerr << "FAIL: LUMIX connect\n";
    return 1;
  }
  if (!peer.sessionStarted()) {
    std::cerr << "FAIL: LUMIX session did not start\n";
    return 1;
  }

  camera.disconnect();

  if (!peer.gpsStopped()) {
    std::cerr << "FAIL: the GPS stop packet was not written before the drop\n";
    return 1;
  }
  if (camera.isConnected()) {
    std::cerr << "FAIL: the camera still reports connected after the drop\n";
    return 1;
  }

  NimBLEDevice::resetMock();
  std::printf("LUMIX teardown drop: PASS\n");
  return 0;
}
