#include "FurbleUI.h"

int main() {
  // This translation unit must stay LVGL-free and link with the headless
  // interface. The real headless companion target exercises the production
  // service; this tiny target catches accidental UI-only header regressions.
  Furble::UI::notifyGestureSettingsChanged();
  return 0;
}
