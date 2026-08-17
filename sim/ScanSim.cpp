#include "CameraList.h"
#include "Scan.h"

#include <utility>

namespace Furble {

Scan &Scan::getInstance(void) {
  static Scan instance;
  return instance;
}

void Scan::setMode(Mode) {}

void Scan::setTimeout(uint32_t) {}

void Scan::start(std::function<void(void *)> scan_callback,
                 void *scan_result_private_data,
                 std::function<void(void *)>) {
  m_Active = true;
  m_ResultPending = true;
  m_ScanResultCallback = std::move(scan_callback);
  m_ScanResultPrivateData = scan_result_private_data;
}

void Scan::stop(void) {
  m_Active = false;
  m_ResultPending = false;
  m_ScanResultCallback = nullptr;
  m_ScanResultPrivateData = nullptr;
}

bool Scan::isActive(void) const {
  return m_Active;
}

void Scan::clear(void) {}

void Scan::update(void) {
  if (!m_Active || !m_ResultPending) {
    return;
  }

  m_ResultPending = false;
  // The UI seeds the FauxNY test camera when FAUXNY is enabled. Do not add a
  // second row when the fake scan completes.
  if (CameraList::size() == 0) {
    CameraList::addFauxNY();
    if (m_ScanResultCallback) {
      m_ScanResultCallback(m_ScanResultPrivateData);
    }
  }
}

}  // namespace Furble
