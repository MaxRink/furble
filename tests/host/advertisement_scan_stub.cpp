#include "Scan.h"

#include "advertisement_scan_stub.h"

namespace {

const NimBLEAdvertisedDevice *g_Advertisement = nullptr;

}  // namespace

namespace Furble {
namespace Host {

void setScanAdvertisement(const NimBLEAdvertisedDevice *advertisement) {
  g_Advertisement = advertisement;
}

}  // namespace Host
}  // namespace Furble

namespace Furble {

class Scan::CallbackProxy {};

Scan::Scan() = default;
Scan::~Scan() = default;

Scan &Scan::getInstance(void) {
  static Scan instance;
  return instance;
}

void Scan::setMode(Mode mode) {
  m_Mode = mode;
}
void Scan::setTimeout(uint32_t timeout) {
  m_Timeout = timeout;
}
bool Scan::start(std::function<void(void *)> result,
                 void *privateData,
                 std::function<void(void *)> end) {
  m_ScanResultCallback = std::move(result);
  m_ScanEndCallback = std::move(end);
  m_ScanResultPrivateData = privateData;
  return true;
}
bool Scan::start(NimBLEScanCallbacks *callbacks, uint32_t, bool) {
  // The vendor scan callback runs on the scanning task on device. Delivering it
  // inline is enough here: the caller pushes the match onto its own queue and
  // then reads that queue, so the ordering the connect path depends on holds.
  if ((callbacks != nullptr) && (g_Advertisement != nullptr)) {
    callbacks->onResult(g_Advertisement);
  }
  return true;
}
void Scan::stop(void) {}
bool Scan::isActive(void) const {
  return false;
}
void Scan::clear(void) {}
void Scan::onResult(const NimBLEAdvertisedDevice *) {}
void Scan::onScanEnd(const NimBLEScanResults &, int) {}
void Scan::applyMode(Mode) {}

}  // namespace Furble
