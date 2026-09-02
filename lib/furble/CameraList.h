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
   * Camera's address key is present in the saved connection list.
   *
   * The connectable list holds saved cameras after load() and scan results
   * during a scan, and a scan can rediscover a camera which is already saved.
   * This reads the store, so it is the only way to tell the two apart.
   *
   * Deliberately not called isSaved(): that name is taken by a wider identity
   * rule which matches on vendor type as well as address, and this is only the
   * narrow address-key test the console's saved flag needs.
   */
  static bool isSavedAddress(const Furble::Camera *camera);

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
