#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "CameraList.h"

namespace Furble {

const std::string &Camera::getName(void) const {
  return m_Name;
}

}  // namespace Furble

namespace {

void check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "sim camera list check failed: %s\n", message);
    std::abort();
  }
}

}  // namespace

int main(void) {
  using Furble::Camera;
  using Furble::CameraList;

  CameraList::clear();
  CameraList::load();
  check(CameraList::getSaveCount() == 0, "fresh list has no saved cameras");

  auto first = std::make_shared<Camera>("First");
  auto second = std::make_shared<Camera>("Second");
  CameraList::addFauxNY();
  auto unsaved = CameraList::last();
  check(unsaved != nullptr, "scan adds a camera");
  check(CameraList::getCameraId(unsaved.get()) == 0, "scan camera remains unsaved");

  CameraList::save(first.get());
  CameraList::save(second.get());
  check(CameraList::getSaveCount() == 2, "two cameras are saved");
  // Saved cameras need not be in the connectable list for identity lookup.
  check(CameraList::getCameraId(first.get()) == 1, "first saved id is stable");
  check(CameraList::getCameraId(second.get()) == 2, "second saved id is stable");

  // BLE identity includes the address type. Equal address bits with public and
  // random types must remain two saved cameras, not one overwrite.
  auto publicIdentity = std::make_shared<Camera>("typed-public", 0x123456789abcULL, 0);
  auto randomIdentity = std::make_shared<Camera>("typed-random", 0x123456789abcULL, 1);
  CameraList::save(publicIdentity.get());
  CameraList::save(randomIdentity.get());
  check(CameraList::getSaveCount() == 4, "sim keeps equal address bits with distinct BLE types");
  check(CameraList::getCameraId(publicIdentity.get())
            != CameraList::getCameraId(randomIdentity.get()),
        "sim gives distinct BLE address types distinct ids");

  CameraList::clear();
  CameraList::load();
  check(CameraList::size() == 4, "reload restores all saved cameras");
  auto loadedFirst = CameraList::get(0);
  auto loadedSecond = CameraList::get(1);
  check(CameraList::getCameraId(loadedFirst.get()) == 1, "first id survives reload");
  check(CameraList::getCameraId(loadedSecond.get()) == 2, "second id survives reload");

  // Clearing the connectable list must not discard the persisted record
  // identity. Re-saving the original object after a clear is a no-op, just as
  // the production address-keyed list is.
  CameraList::clear();
  CameraList::save(first.get());
  check(CameraList::getSaveCount() == 4, "clear and re-save do not duplicate a camera");
  check(CameraList::getCameraId(first.get()) == 1,
        "clear and re-save retain the original stable id");
  CameraList::load();
  loadedFirst = CameraList::get(0);

  CameraList::remove(loadedFirst.get());
  check(CameraList::getSaveCount() == 3, "remove deletes only one saved entry");
  CameraList::clear();
  CameraList::load();
  check(CameraList::size() == 3, "reload keeps the remaining cameras");
  check(CameraList::getCameraId(CameraList::get(0).get()) == 2,
        "remaining id does not depend on list position");

  auto third = std::make_shared<Camera>("Third");
  CameraList::save(third.get());
  check(CameraList::getCameraId(third.get()) == 5, "removed ids are not reused");
  CameraList::clear();
  return 0;
}
