#include <memory>
#include <cstdint>
#include <string>
#include <vector>

#include "CameraList.h"

namespace Furble {
namespace {

std::vector<std::shared_ptr<Camera>> cameras;
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

std::vector<std::shared_ptr<Camera>> CameraList::snapshot(void) {
  return cameras;
}

std::shared_ptr<Camera> CameraList::last(void) {
  return hasCamera() ? cameras.back() : nullptr;
}

std::shared_ptr<Camera> CameraList::get(size_t n) {
  return cameras.at(n);
}

uint8_t CameraList::getCameraId(const Camera *camera) {
  if (camera == nullptr) {
    return 0;
  }
  for (size_t index = 0; index < cameras.size(); ++index) {
    if (cameras[index].get() == camera) {
      // The simulator has no persisted NVS identity, but its list order is
      // stable for a scenario and mirrors the production non-zero ID contract.
      return static_cast<uint8_t>(index + 1U);
    }
  }
  return 0;
}

void CameraList::addFauxNY(void) {
  // Append a fresh test camera. Every caller either guards on an empty list or
  // clears first, except the multi-connect scenario path which deliberately
  // seeds a second camera.
  cameras.push_back(std::make_shared<Camera>("FauxNY Camera"));
}

}  // namespace Furble
