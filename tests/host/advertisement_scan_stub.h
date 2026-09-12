#ifndef FURBLE_HOST_ADVERTISEMENT_SCAN_STUB_H
#define FURBLE_HOST_ADVERTISEMENT_SCAN_STUB_H

class NimBLEAdvertisedDevice;

namespace Furble {
namespace Host {

/**
 * Answer a saved-reconnect scan with this advertisement.
 *
 * A Fujifilm camera scans for its advertisement before every attempt once it
 * has paired at least once, so a test that reconnects after a successful
 * session has to supply one. The default nullptr keeps the stub silent, which
 * is what every scan-free test expects. The pointer is borrowed, so it has to
 * outlive the scans that use it.
 */
void setScanAdvertisement(const NimBLEAdvertisedDevice *advertisement);

}  // namespace Host
}  // namespace Furble

#endif
