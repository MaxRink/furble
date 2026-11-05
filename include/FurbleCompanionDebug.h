#ifndef FURBLE_COMPANION_DEBUG_H
#define FURBLE_COMPANION_DEBUG_H

#if defined(FURBLE_BLE_DEBUG)

class NimBLEServer;
class NimBLECharacteristic;

namespace Furble {

/**
 * Read-only BLE diagnostics for the dedicated debug image.
 *
 * The service is intentionally unencrypted so it still answers when companion
 * authentication or the host GATT cache is broken. It is compiled only into
 * FURBLE_BLE_DEBUG builds and is not part of the production companion contract.
 */
class CompanionDebug {
 public:
  static constexpr const char *SERVICE_UUID = "b57f4f70-087b-4740-b71d-8262cf26ebbc";
  static constexpr const char *STATUS_UUID = "b57f4f72-087b-4740-b71d-8262cf26ebbc";

  static CompanionDebug &getInstance(void);

  /** Create the service. Must run during GATT construction, not under m_Mutex. */
  void attach(NimBLEServer *server);

  /** Refresh the cached snapshot. Runs from the companion service task. */
  void update(void);

 private:
  CompanionDebug() = default;

  NimBLECharacteristic *m_Status = nullptr;
  bool m_Attached = false;
};

}  // namespace Furble

#endif  // FURBLE_BLE_DEBUG

#endif  // FURBLE_COMPANION_DEBUG_H
