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

  CameraList::clear();
  CameraList::load();
  check(CameraList::size() == 2, "reload restores both saved cameras");
  auto loadedFirst = CameraList::get(0);
  auto loadedSecond = CameraList::get(1);
  check(CameraList::getCameraId(loadedFirst.get()) == 1, "first id survives reload");
  check(CameraList::getCameraId(loadedSecond.get()) == 2, "second id survives reload");

  CameraList::remove(loadedFirst.get());
  check(CameraList::getSaveCount() == 1, "remove deletes only one saved entry");
  CameraList::clear();
  CameraList::load();
  check(CameraList::size() == 1, "reload keeps the remaining camera");
  check(CameraList::getCameraId(CameraList::get(0).get()) == 2,
        "remaining id does not depend on list position");

  auto third = std::make_shared<Camera>("Third");
  CameraList::save(third.get());
  check(CameraList::getCameraId(third.get()) == 3, "removed id is not reused");
  CameraList::clear();
  return 0;
}
