#ifndef FURBLE_BOOT_SCREEN_H
#define FURBLE_BOOT_SCREEN_H

#include <cstdint>

namespace Furble {

/**
 * Power-on boot splash.
 *
 * Drawn straight to the panel with M5GFX, so it is available the moment
 * Platform::init() brings the display up, well before LVGL exists. app_main
 * calls step() after each real init stage, so the progress bar tracks actual
 * boot progress rather than a timer. The main menu takes over the screen when
 * the UI constructor runs LVGL, so there is nothing to tear down.
 *
 * Every method is a no-op when the BOOT_SPLASH setting is off, so the caller
 * can leave the hooks in place unconditionally. Nothing here is interactive,
 * touches LVGL, or holds a mutex.
 */
class BootScreen {
 public:
  BootScreen() = delete;
  ~BootScreen() = delete;

  /**
   * Draw the splash and prime the progress bar.
   *
   * Reads the BOOT_SPLASH setting, so call it after Settings::init() and after
   * Platform::init() has brought the display up. totalStages is the number of
   * step() calls the caller will make before finish(); it sizes the bar.
   */
  static void begin(uint8_t totalStages);

  /** Advance the progress bar one stage and show its label. */
  static void step(const char *label);

  /**
   * Fill the bar, show "Ready" and hold briefly so the splash does not flash
   * past. The short hold is skipped on boots that already took longer than the
   * minimum and in the simulator. Runs on the boot task, holds no mutex.
   */
  static void finish(void);

  /** True once the splash has been drawn, for scripted simulator assertions. */
  static bool wasShown(void);

  /** Number of stages advanced so far, for scripted simulator assertions. */
  static uint8_t stepsShown(void);

 private:
  static void drawProgress(const char *label);

  static bool s_Enabled;
  static bool s_Shown;
  static uint8_t s_Total;
  static uint8_t s_Step;
  static uint32_t s_StartedAt;

  static int32_t s_Width;
  static int32_t s_Height;
  static int32_t s_BarX;
  static int32_t s_BarY;
  static int32_t s_BarWidth;
  static int32_t s_BarHeight;
  static int32_t s_LabelY;
};

}  // namespace Furble

#endif
