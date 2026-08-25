#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "CameraList.h"

namespace Furble {
namespace {

std::vector<std::shared_ptr<Camera>> cameras;
struct saved_camera_t {
  std::string name;
  uint64_t address;
  uint8_t id;
};

std::vector<saved_camera_t> savedCameras;
struct camera_identity_t {
  const Camera *camera;
  uint64_t address;
  uint8_t id;
};

std::vector<camera_identity_t> cameraIds;
std::mutex camerasMutex;
uint16_t nextCameraId = 1;

uint8_t allocateCameraId(void) {
  // Keep 0 for unsaved cameras and 0xff for the all-cameras protocol value.
  if (nextCameraId > 0xfe) {
    return 0;
  }
  const uint8_t id = static_cast<uint8_t>(nextCameraId++);
  return id;
}

}  // namespace

void CameraList::save(const Camera *camera) {
  if (camera == nullptr) {
    return;
  }

  const std::lock_guard<std::mutex> lock(camerasMutex);
  const uint64_t address = camera->getAddress();
  for (const auto &entry : cameraIds) {
    if ((entry.camera == camera) || (entry.address == address)) {
      return;
    }
  }

  for (const auto &saved : savedCameras) {
    if (saved.address == address) {
      cameraIds.push_back({camera, address, saved.id});
      return;
    }
  }

  const uint8_t id = allocateCameraId();
  if (id == 0) {
    return;
  }
  savedCameras.push_back({camera->getName(), address, id});
  cameraIds.push_back({camera, address, id});
}

void CameraList::remove(Camera *camera) {
  if (camera == nullptr) {
    return;
  }

  const std::lock_guard<std::mutex> lock(camerasMutex);
  for (auto it = cameras.begin(); it != cameras.end(); ++it) {
    if (it->get() != camera) {
      continue;
    }

    for (auto idIt = cameraIds.begin(); idIt != cameraIds.end(); ++idIt) {
      if (idIt->camera == camera) {
        const uint8_t id = idIt->id;
        cameraIds.erase(idIt);
        for (auto savedIt = savedCameras.begin(); savedIt != savedCameras.end(); ++savedIt) {
          if (savedIt->id == id) {
            savedCameras.erase(savedIt);
            break;
          }
        }
        break;
      }
    }
    cameras.erase(it);
    return;
  }
}

void CameraList::load(void) {
  const std::lock_guard<std::mutex> lock(camerasMutex);
  cameras.clear();
  cameraIds.clear();
  for (const auto &saved : savedCameras) {
    auto camera = std::make_shared<Camera>(saved.name, saved.address);
    cameraIds.push_back({camera.get(), saved.address, saved.id});
    cameras.push_back(std::move(camera));
  }
}

size_t CameraList::getSaveCount(void) {
  const std::lock_guard<std::mutex> lock(camerasMutex);
  return savedCameras.size();
}

size_t CameraList::size(void) {
  const std::lock_guard<std::mutex> lock(camerasMutex);
  return cameras.size();
}

void CameraList::clear(void) {
  const std::lock_guard<std::mutex> lock(camerasMutex);
  cameras.clear();
  cameraIds.clear();
}

std::vector<std::shared_ptr<Camera>> CameraList::snapshot(void) {
  const std::lock_guard<std::mutex> lock(camerasMutex);
  return cameras;
}

std::shared_ptr<Camera> CameraList::last(void) {
  const std::lock_guard<std::mutex> lock(camerasMutex);
  return cameras.empty() ? nullptr : cameras.back();
}

std::shared_ptr<Camera> CameraList::get(size_t n) {
  const std::lock_guard<std::mutex> lock(camerasMutex);
  return cameras.at(n);
}

uint8_t CameraList::getCameraId(const Camera *camera) {
  if (camera == nullptr) {
    return 0;
  }
  const std::lock_guard<std::mutex> lock(camerasMutex);
  for (const auto &entry : cameraIds) {
    if (entry.camera == camera) {
      return entry.id;
    }
  }
  return 0;
}

void CameraList::addFauxNY(void) {
  const std::lock_guard<std::mutex> lock(camerasMutex);
  // Append a fresh test camera. Every caller either guards on an empty list or
  // clears first, except the multi-connect scenario path which deliberately
  // seeds a second camera.
  cameras.push_back(std::make_shared<Camera>("FauxNY Camera"));
}

}  // namespace Furble
