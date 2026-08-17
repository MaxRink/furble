#include <memory>
#include <string>
#include <vector>

#include "CameraList.h"

namespace Furble {
namespace {

std::vector<std::unique_ptr<Camera>> cameras;
bool saved = false;

bool hasCamera(void) {
  return !cameras.empty();
}

}  // namespace

void CameraList::save(const Camera *camera) {
  if (camera != nullptr) {
    saved = true;
  }
}

void CameraList::remove(Camera *camera) {
  if (camera == nullptr) {
    return;
  }

  for (auto it = cameras.begin(); it != cameras.end(); ++it) {
    if (it->get() == camera) {
      cameras.erase(it);
      saved = false;
      return;
    }
  }
}

void CameraList::load(void) {
  cameras.clear();
  if (saved) {
    addFauxNY();
  }
}

size_t CameraList::getSaveCount(void) {
  return saved ? 1U : 0U;
}

size_t CameraList::size(void) {
  return cameras.size();
}

void CameraList::clear(void) {
  cameras.clear();
}

Camera *CameraList::last(void) {
  return hasCamera() ? cameras.back().get() : nullptr;
}

Camera *CameraList::get(size_t n) {
  return cameras.at(n).get();
}

void CameraList::addFauxNY(void) {
  if (!hasCamera()) {
    cameras.push_back(std::make_unique<Camera>("FauxNY Camera"));
  }
}

}  // namespace Furble
