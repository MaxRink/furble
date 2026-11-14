#include "FurblePlatform.h"

namespace Furble {

Platform &Platform::getInstance() {
  static Platform instance;
  return instance;
}

void Platform::restart(void) {}

}  // namespace Furble
