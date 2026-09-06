#ifndef CAMERALIST_H
#define CAMERALIST_H

#include <Preferences.h>
#include <memory>

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

 private:
  typedef struct {
    char name[16];
    Camera::Type type;
  } index_entry_t;

  static void fillSaveEntry(index_entry_t &entry, const Camera *camera);

  /** Rebuild every saved camera from NVS into the supplied vector. */
  static void loadSaved(std::vector<std::shared_ptr<Furble::Camera>> &out);
  static std::vector<index_entry_t> load_index(void);
  static void save_index(std::vector<index_entry_t> &index);
  static void add_index(std::vector<index_entry_t> &index, index_entry_t &entry);

  /**
   * List of connectable devices.
   *
   * Held by shared_ptr, not unique_ptr, so an in-flight or active connection can
   * keep its Camera alive after load() or clear() drops the list's reference.
   */
  static std::vector<std::shared_ptr<Furble::Camera>> m_ConnectList;

  static Preferences m_Prefs;
};
}  // namespace Furble

#endif
