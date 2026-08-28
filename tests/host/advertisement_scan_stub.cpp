#include "Scan.h"

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
bool Scan::start(NimBLEScanCallbacks *, uint32_t, bool) {
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
