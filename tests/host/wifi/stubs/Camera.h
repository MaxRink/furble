#pragma once
namespace Furble {
class Camera {
 public:
  struct timesync_t {
    unsigned int year, month, day, hour, minute, second, centisecond;
  };
};
}  // namespace Furble
