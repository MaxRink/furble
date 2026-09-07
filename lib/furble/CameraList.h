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
   * Save camera to the persisted catalog.
   */
  static void save(const std::shared_ptr<Furble::Camera> &camera);

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
   * Is this camera already in the saved list?
   *
   * Used to refuse a second pairing of a camera the user already saved.
   * Pairing it again does not replace the old entry when the body advertises a
   * resolvable private address, as Fujifilm Secure does: the address moved, so
   * the index gains a second record for one camera and the saved reconnect
   * picks whichever the index happens to hold.
   *
   * Identity is CameraListProtocol::sameSavedIdentity(), not the raw index
   * key. Reads the saved records into a local vector, so it is safe to call
   * while the connect list holds live scan results.
   */
  static bool isSaved(const Furble::Camera *camera);

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

  /** Strong-reference copy of the persisted camera catalog. */
  static std::vector<std::shared_ptr<Furble::Camera>> savedSnapshot(void);

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

  /** Rebuild every saved camera from NVS into the supplied vector. */
  static void loadSaved(std::vector<std::shared_ptr<Furble::Camera>> &out);
  static std::vector<index_entry_t> load_index(void);
  static bool save_index(std::vector<index_entry_t> &index);
  static void add_index(std::vector<index_entry_t> &index, index_entry_t &entry);
  static std::vector<std::shared_ptr<Furble::Camera>> deserialize(
      const std::vector<index_entry_t> &index);
  static bool ensureSavedLoaded(void);

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

  /** Persisted cameras. Scans never add to this catalog. */
  static std::vector<std::shared_ptr<Furble::Camera>> m_SavedList;

  /** Address key to stable saved id. */
  static std::map<std::string, uint8_t> m_CameraIds;

  /** Guards m_ConnectList and m_CameraIds. A leaf lock: no callbacks run under it. */
  static std::mutex m_Mutex;
  /** Serializes complete Preferences transactions, including lazy loads. */
  static std::mutex m_PersistenceMutex;
  static bool m_SavedInitialized;

  static Preferences m_Prefs;
};
}  // namespace Furble

#endif
