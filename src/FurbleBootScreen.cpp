#include <cstring>

#include <esp_system.h>

#include <M5Unified.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "FurbleBootScreen.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {

bool BootScreen::s_Enabled = false;
bool BootScreen::s_Shown = false;
uint8_t BootScreen::s_Total = 1;
uint8_t BootScreen::s_Step = 0;
uint32_t BootScreen::s_StartedAt = 0;

int32_t BootScreen::s_Width = 0;
int32_t BootScreen::s_Height = 0;
int32_t BootScreen::s_BarX = 0;
int32_t BootScreen::s_BarY = 0;
int32_t BootScreen::s_BarWidth = 0;
int32_t BootScreen::s_BarHeight = 0;
int32_t BootScreen::s_LabelY = 0;

namespace {

// Keep the splash visible at least this long so a fast boot does not flash the
// wordmark past. Real boots spend longer than this in BLE init, so the pad in
// finish() is usually zero. Skipped entirely in the simulator.
constexpr uint32_t MIN_VISIBLE_MS = 700;

// A dark slate background with a bright accent bar reads on every panel class.
constexpr uint8_t BG_R = 8, BG_G = 10, BG_B = 16;
constexpr uint8_t BAR_R = 0, BAR_G = 168, BAR_B = 220;
constexpr uint8_t FRAME_R = 60, FRAME_G = 64, FRAME_B = 76;

/** Short board name for the boot info block. */
const char *boardName(void) {
  switch (M5.getBoard()) {
    case m5::board_t::board_M5StickC:
      return "StickC";
    case m5::board_t::board_M5StickCPlus:
      return "StickC+";
    case m5::board_t::board_M5StickCPlus2:
      return "StickC+2";
    case m5::board_t::board_M5StickS3:
      return "StickS3";
    case m5::board_t::board_M5Stack:
      return "Core";
    case m5::board_t::board_M5StackCore2:
      return "Core2";
    case m5::board_t::board_M5Tough:
      return "Tough";
    default:
      return "M5";
  }
}

/** Human readable last reset reason. */
const char *resetReason(void) {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return "Power on";
    case ESP_RST_EXT:
      return "External";
    case ESP_RST_SW:
      return "Software";
    case ESP_RST_PANIC:
      return "Panic";
    case ESP_RST_INT_WDT:
      return "Int WDT";
    case ESP_RST_TASK_WDT:
      return "Task WDT";
    case ESP_RST_WDT:
      return "Watchdog";
    case ESP_RST_DEEPSLEEP:
      return "Deep sleep";
    case ESP_RST_BROWNOUT:
      return "Brownout";
    default:
      return "Unknown";
  }
}

}  // namespace

void BootScreen::begin(uint8_t totalStages) {
  s_Enabled = Settings::load<Settings::BOOT_SPLASH>();
  s_Shown = false;
  s_Step = 0;
  s_Total = (totalStages == 0) ? 1 : totalStages;
  s_StartedAt = Platform::getInstance().tick();

  if (!s_Enabled) {
    return;
  }

  auto &display = M5.Display;
  s_Width = display.width();
  s_Height = display.height();
  if ((s_Width <= 0) || (s_Height <= 0)) {
    // No usable panel (headless build), stay a silent no-op.
    s_Enabled = false;
    return;
  }

  // Scale the wordmark to the panel: size 2 on the 80 px StickC, size 3 on the
  // 135 px sticks, larger on the 320 px cores. drawString clips to the panel,
  // and the info lines below always use the compact base font.
  const uint8_t wordSize = (s_Width >= 320) ? 4 : (s_Width >= 130) ? 3 : 2;

  // Layout is proportional so it fits 80x160, 135x240 and 320x240.
  const int32_t wordY = s_Height / 4;
  const int32_t infoY = s_Height / 2;
  const int32_t lineH = 12;
  s_BarHeight = (s_Height >= 200) ? 12 : 8;
  s_BarWidth = (s_Width * 3) / 4;
  s_BarX = (s_Width - s_BarWidth) / 2;
  s_BarY = (s_Height * 82) / 100;
  s_LabelY = s_BarY - lineH - 2;

  const auto bg = display.color565(BG_R, BG_G, BG_B);
  const auto frame = display.color565(FRAME_R, FRAME_G, FRAME_B);

  display.fillScreen(bg);
  display.setTextColor(display.color565(240, 244, 250), bg);

  // Wordmark: the furble identity, no bespoke art needed.
  display.setTextDatum(middle_center);
  display.setTextSize(wordSize);
  display.drawString(FURBLE_STR, s_Width / 2, wordY);

  // Boot info block: version, board, reset reason. Small and centered.
  display.setTextSize(1);
  display.setTextColor(display.color565(150, 158, 172), bg);
  display.drawString(FURBLE_VERSION, s_Width / 2, infoY);
  display.drawString(boardName(), s_Width / 2, infoY + lineH);
  display.drawString(resetReason(), s_Width / 2, infoY + (2 * lineH));

  // Empty progress bar frame.
  display.drawRect(s_BarX, s_BarY, s_BarWidth, s_BarHeight, frame);

  s_Shown = true;
  drawProgress("Starting");
}

void BootScreen::drawProgress(const char *label) {
  auto &display = M5.Display;
  const auto bg = display.color565(BG_R, BG_G, BG_B);
  const auto bar = display.color565(BAR_R, BAR_G, BAR_B);

  // Fill the bar interior proportional to completed stages.
  const int32_t inner = s_BarWidth - 2;
  int32_t filled = (inner * s_Step) / s_Total;
  if (filled < 0) {
    filled = 0;
  } else if (filled > inner) {
    filled = inner;
  }
  display.fillRect(s_BarX + 1, s_BarY + 1, inner, s_BarHeight - 2, bg);
  if (filled > 0) {
    display.fillRect(s_BarX + 1, s_BarY + 1, filled, s_BarHeight - 2, bar);
  }

  // Stage label above the bar, clear its band first so it does not overprint.
  display.fillRect(0, s_LabelY - 6, s_Width, 14, bg);
  display.setTextDatum(middle_center);
  display.setTextSize(1);
  display.setTextColor(display.color565(220, 226, 236), bg);
  display.drawString(label, s_Width / 2, s_LabelY);
}

void BootScreen::step(const char *label) {
  if (!s_Enabled) {
    return;
  }
  if (s_Step < s_Total) {
    s_Step++;
  }
  drawProgress(label == nullptr ? "" : label);
}

void BootScreen::finish(void) {
  if (!s_Enabled) {
    return;
  }

  s_Step = s_Total;
  drawProgress("Ready");

#if !defined(FURBLE_SIM)
  // Pad only if the boot outran the minimum visible time. Runs on the boot
  // task before the UI exists, so it blocks nothing and holds no mutex.
  const uint32_t elapsed = Platform::getInstance().tick() - s_StartedAt;
  if (elapsed < MIN_VISIBLE_MS) {
    vTaskDelay(pdMS_TO_TICKS(MIN_VISIBLE_MS - elapsed));
  }
#endif
}

bool BootScreen::wasShown(void) {
  return s_Shown;
}

uint8_t BootScreen::stepsShown(void) {
  return s_Step;
}

}  // namespace Furble
