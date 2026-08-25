#ifndef CAMERALIST_H
#define CAMERALIST_H

#include <Preferences.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "Camera.h"

namespace Furble {

class CameraList {
 public:
  CameraList();
  ~CameraList();
  /**
   * Save camera to connection list.
   */
  static void save(const Furble::Camera *camera);

  /**
   * Remove camera from connection list.
   */
  static void remove(Furble::Camera *camera);

  /**
   * Load previously connected devices.
   */
  static void load(void);

  /**
   * Get number of saved connections.
   */
  static size_t getSaveCount(void);

  /**
   * Add matching devices to the list.
   *
   * @return true if device matches
   */
  static bool match(const NimBLEAdvertisedDevice *pDevice);

  /**
   * Add FauxNY device to the list.
   */
  static void addFauxNY(void);

  /**
   * Number of connectable devices.
   */
  static size_t size(void);

  /** Return a strong-reference snapshot safe to iterate without holding the list lock. */
  static std::vector<std::shared_ptr<Furble::Camera>> snapshot(void);

  /**
   * Clear connectable devices.
   */
  static void clear(void);

  /**
   * Get last added entry.
   */
  static std::shared_ptr<Furble::Camera> last(void);

  /**
   * Retrieve device by index.
   */
  static std::shared_ptr<Furble::Camera> get(size_t n);

  /** Get the stable saved id for a camera, or zero for an unsaved camera. */
  static uint8_t getCameraId(const Furble::Camera *camera);

 private:
  typedef struct __attribute__((packed)) {
    char name[16];
    Camera::Type type;
    uint8_t camera_id;
  } index_entry_t;

  static void fillSaveEntry(index_entry_t &entry, const Camera *camera);
  static std::vector<index_entry_t> load_index(bool *migrated = nullptr);
  static bool save_index(std::vector<index_entry_t> &index);
  static void add_index(std::vector<index_entry_t> &index, index_entry_t &entry);
  static uint8_t allocateCameraId(const std::vector<index_entry_t> &index);
  static bool containsCameraId(const std::vector<index_entry_t> &index, uint8_t cameraId);

  /**
   * Raise the persisted id counter to at least one past the highest id in use.
   *
   * Called from the write paths so a delete of the highest id can never hand
   * that id back to the next saved camera. Requires m_Prefs open for writing.
   */
  static bool syncCameraIdFloor(const std::vector<index_entry_t> &index);

  /**
   * List of connectable devices.
   *
   * Held by shared_ptr, not unique_ptr, so an in-flight or active connection can
   * keep its Camera alive after load() or clear() drops the list's reference.
   */
  static std::vector<std::shared_ptr<Furble::Camera>> m_ConnectList;

  static Preferences m_Prefs;
  static std::mutex m_PrefsMutex;
  static std::mutex m_Mutex;
};
}  // namespace Furble

#endif
