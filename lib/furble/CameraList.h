#ifndef CAMERALIST_H
#define CAMERALIST_H

#include <Preferences.h>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
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

  /**
   * Strong-reference copy of the list, safe to iterate off the UI task.
   *
   * The companion service walks the saved cameras from its own task while the
   * UI task may be reloading the list. Iterating m_ConnectList directly races
   * that reload, so off-task callers take a snapshot instead.
   */
  static std::vector<std::shared_ptr<Furble::Camera>> snapshot(void);

  /**
   * Stable saved id for a camera.
   *
   * Survives a reorder or a delete of another entry, unlike the list position.
   * Returns CameraListProtocol::INDEX_ID_INVALID for a camera that is not
   * saved.
   */
  static uint8_t getCameraId(const Furble::Camera *camera);

 private:
  typedef struct {
    char name[16];
    Camera::Type type;
    uint8_t camera_id;
  } index_entry_t;

  static void fillSaveEntry(index_entry_t &entry, const Camera *camera);
  static std::vector<index_entry_t> load_index(void);
  static void save_index(std::vector<index_entry_t> &index);
  static void add_index(std::vector<index_entry_t> &index, index_entry_t &entry);

  /** Assign an unused id to every entry that has none. Requires m_Prefs open for writing. */
  static bool assignCameraIds(std::vector<index_entry_t> &index);

  /** Republish the address key to id map used by getCameraId(). */
  static void publishCameraIds(const std::vector<index_entry_t> &index);

  /**
   * List of connectable devices.
   *
   * Held by shared_ptr, not unique_ptr, so an in-flight or active connection can
   * keep its Camera alive after load() or clear() drops the list's reference.
   */
  static std::vector<std::shared_ptr<Furble::Camera>> m_ConnectList;

  /** Address key to stable saved id, republished on every load(). */
  static std::map<std::string, uint8_t> m_CameraIds;

  /** Guards m_ConnectList and m_CameraIds. A leaf lock: no callbacks run under it. */
  static std::mutex m_Mutex;

  static Preferences m_Prefs;
};
}  // namespace Furble

#endif
