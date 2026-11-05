#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include <SDL2/SDL.h>
#include <lgfx/v1/platforms/sdl/common.hpp>

#include "driver.h"
#include "ui_buttons.h"

namespace Furble::Sim {
namespace {

// The control window is a small separate SDL window instead of a strip inside
// the panel window: M5GFX owns the panel window and exposes no place to attach
// child content to it. Drawing stays in logical coordinates, so the hit boxes
// are the same points the renderer draws even when macOS backs the window with
// a 2x drawable. The window is borderless, matches the modeled panel's content
// height, and adds a top inset equal to the panel title bar, so placing it
// flush beside the panel reads as one window: no second title bar, no gap, and
// the same outer top and bottom edge.
constexpr int kWindowWidth = 300;
constexpr int kPanelGap = 0;
constexpr uint32_t kDebounceMs = 120;
// A click that arrives between two UI samples must still produce one firmware
// press. Hold the emulated GPIO low for at least this long even when the mouse
// down and up are drained from the same event batch. M5Unified debounces the
// pin for 10 ms, so a shorter pulse can disappear.
constexpr uint32_t kMinimumPressMs = 30;
constexpr uint32_t kNoChange = UINT32_MAX;
constexpr size_t kMaxButtons = 3;
constexpr size_t kActionQueueCapacity = 64;

struct button_t {
  const char *label;
  uint8_t gpio;
  SDL_Rect rect;
};

// The modeled board decides the button set, matching the scenario `btn` verb's
// button names and the keyboard mapping in FurblePlatformSim.cpp. A click and
// the matching keyboard letter drive the same emulated GPIOs; the `btn` verb is
// a different, UI-task injection through UI::simPressButton and does not use
// these pins.
//
// The rectangles are a top view of the physical device. Cores put A/B/C along
// the front edge; Sticks put B on the top edge, A on the front edge and PWR on
// the left side. The device outline is drawn between them. B is always centered
// on the body's top edge, never on its right edge, so a control strip placed to
// the right of the panel cannot make B read as a side button. Buttons straddle
// the edge they belong to instead of floating beside the body, which is what
// makes a click target read as a physical button on that face.
#if defined(FURBLE_M5COREX)
constexpr int kWindowHeight = 240;
constexpr button_t kButtons[] = {
    {"A", 39, {58, 148, 52, 36} }, // BtnA, front left
    {"B", 38, {124, 148, 52, 36}}, // BtnB, front middle
    {"C", 37, {190, 148, 52, 36}}, // BtnC, front right
};
constexpr size_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
constexpr SDL_Rect kBodyRect = {60, 40, 180, 120};
constexpr SDL_Rect kScreenRect = {76, 56, 148, 56};
constexpr const char *kWindowTitle = "furble buttons - Core front: A B C";
#elif defined(FURBLE_M5STICKS3)
constexpr int kWindowHeight = 240;
constexpr button_t kButtons[] = {
    {"A",   39, {122, 164, 56, 40}}, // BtnA, front/bottom edge, centered
    {"B",   38, {122, 36, 56, 40} }, // BtnB, top edge, centered
    {"PWR", 36, {85, 100, 40, 40} }, // BtnPWR, left side
};
constexpr size_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
constexpr SDL_Rect kBodyRect = {105, 56, 90, 128};
constexpr SDL_Rect kScreenRect = {120, 78, 60, 48};
constexpr const char *kWindowTitle = "furble buttons - Stick: B top, A front, PWR left";
// B is a top-edge control, not a side control. Keep its centre on the body's
// centre line and keep it straddling the top edge, so a strip placed beside the
// panel cannot visually turn it into a right-side button.
static_assert(kButtons[1].rect.x + kButtons[1].rect.w / 2 == kBodyRect.x + kBodyRect.w / 2,
              "StickS3 B must stay centred on the body top edge");
static_assert(kButtons[1].rect.y < kBodyRect.y, "StickS3 B must straddle the body top edge");
#elif defined(FURBLE_M5STICKC)
constexpr int kWindowHeight = 160;
constexpr button_t kButtons[] = {
    {"A",   39, {122, 96, 56, 40}}, // BtnA, front/bottom edge, centered
    {"B",   38, {122, 24, 56, 40}}, // BtnB, top edge, centered
    {"PWR", 36, {85, 60, 40, 40} }, // BtnPWR, left side
};
constexpr size_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
constexpr SDL_Rect kBodyRect = {105, 44, 90, 72};
constexpr SDL_Rect kScreenRect = {120, 58, 60, 40};
constexpr const char *kWindowTitle = "furble buttons - Stick: B top, A front, PWR left";
// Same top-edge contract as the StickS3 layout above.
static_assert(kButtons[1].rect.x + kButtons[1].rect.w / 2
                  == kBodyRect.x + kBodyRect.w / 2,
              "StickC B must stay centred on the body top edge");
static_assert(kButtons[1].rect.y < kBodyRect.y,
              "StickC B must straddle the body top edge");
#else
// An unmodeled board gets no control surface rather than the wrong buttons.
constexpr int kWindowHeight = 240;
constexpr button_t kButtons[] = {
    {"", 0, {0, 0, 0, 0}}
};
constexpr size_t kButtonCount = 0;
constexpr SDL_Rect kBodyRect = {0, 0, 0, 0};
constexpr SDL_Rect kScreenRect = {0, 0, 0, 0};
constexpr const char *kWindowTitle = "furble simulator buttons";
#endif

struct state_t {
  bool started = false;
  bool enabled = false;
  bool filterInstalled = false;
  bool closeRequested = false;
  bool dirty = false;
  bool hadPreviousFilter = false;
  bool releasePending = false;
  int hover = -1;
  int pressed = -1;
  int contentTop = 0;
  uint32_t pressStarted = 0;
  uint32_t windowId = 0;
  uint32_t panelWindowId = 0;
  std::array<uint32_t, kMaxButtons> lastChange;
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_EventFilter previousFilter = nullptr;
  void *previousFilterUserdata = nullptr;

  state_t() { lastChange.fill(kNoChange); }
};

state_t state;

// SDL2 invokes the process event filter on the thread that pushes an event, not
// only on the thread that polls, and SDL_PushEvent calls it synchronously.
// The filter therefore never touches state: it records an action in this small
// queue, and the SDL main thread applies the actions in uiButtonsTick(). The
// queue lock is held only for the copy in and out, never across SDL calls, so a
// push from another thread (or from the main thread itself) cannot deadlock or
// race draw().
enum class action_type_t : uint8_t {
  DOWN,
  UP,
  HOVER,
  CLOSE,
  RELEASE,
};

struct action_t {
  action_type_t type;
  int x;
  int y;
};

std::mutex actionMutex;
std::array<action_t, kActionQueueCapacity> actionQueue {};
size_t actionHead = 0;
size_t actionCount = 0;
std::atomic<bool> actionOverflow {false};

// Read by the filter from any thread. The window and renderer handles stay
// main-thread owned.
std::atomic<bool> filterEnabled {false};
std::atomic<uint32_t> filterWindowId {0};

// Read by the UI task through uiButtonsActive(). The SDL window and renderer
// themselves stay main-thread owned.
std::atomic<bool> active {false};

// 5x7 glyphs for the button labels and the TOP/FRONT orientation captions.
// The control surface has no font dependency on purpose: SDL2 alone is already
// a simulator requirement, while SDL_ttf is not.
constexpr uint8_t kGlyphA[7] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
constexpr uint8_t kGlyphB[7] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
constexpr uint8_t kGlyphC[7] = {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f};
constexpr uint8_t kGlyphD[7] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
constexpr uint8_t kGlyphE[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
constexpr uint8_t kGlyphF[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
constexpr uint8_t kGlyphG[7] = {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e};
constexpr uint8_t kGlyphI[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f};
constexpr uint8_t kGlyphL[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
constexpr uint8_t kGlyphN[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
constexpr uint8_t kGlyphO[7] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
constexpr uint8_t kGlyphP[7] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
constexpr uint8_t kGlyphR[7] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
constexpr uint8_t kGlyphT[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
constexpr uint8_t kGlyphV[7] = {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04};
constexpr uint8_t kGlyphW[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11};
constexpr uint8_t kGlyphSpace[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const uint8_t *glyphFor(char c) {
  switch (c) {
    case 'A':
      return kGlyphA;
    case 'B':
      return kGlyphB;
    case 'C':
      return kGlyphC;
    case 'D':
      return kGlyphD;
    case 'E':
      return kGlyphE;
    case 'F':
      return kGlyphF;
    case 'G':
      return kGlyphG;
    case 'I':
      return kGlyphI;
    case 'L':
      return kGlyphL;
    case 'N':
      return kGlyphN;
    case 'O':
      return kGlyphO;
    case 'P':
      return kGlyphP;
    case 'R':
      return kGlyphR;
    case 'T':
      return kGlyphT;
    case 'V':
      return kGlyphV;
    case 'W':
      return kGlyphW;
    case ' ':
      return kGlyphSpace;
    default:
      return nullptr;
  }
}

int textWidth(const char *text, int scale) {
  const size_t length = std::strlen(text);
  if (length == 0) {
    return 0;
  }
  return static_cast<int>(length) * 5 * scale + static_cast<int>(length - 1) * scale;
}

void drawText(SDL_Renderer *renderer, const char *text, int x, int y, int scale) {
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    const uint8_t *glyph = glyphFor(*cursor);
    if (glyph != nullptr) {
      for (int row = 0; row < 7; ++row) {
        for (int column = 0; column < 5; ++column) {
          if ((glyph[row] & (1u << (4 - column))) == 0) {
            continue;
          }
          const SDL_Rect pixel = {
              x + column * scale,
              y + row * scale,
              scale,
              scale,
          };
          SDL_RenderFillRect(renderer, &pixel);
        }
      }
    }
    x += 6 * scale;
  }
}

void drawCenteredText(const char *text, int y, int scale) {
  drawText(state.renderer, text, (kWindowWidth - textWidth(text, scale)) / 2, y, scale);
}

void drawDeviceOutline(void) {
  SDL_SetRenderDrawColor(state.renderer, 44, 46, 52, 0xff);
  SDL_RenderFillRect(state.renderer, &kBodyRect);
  SDL_SetRenderDrawColor(state.renderer, 132, 138, 152, 0xff);
  SDL_RenderDrawRect(state.renderer, &kBodyRect);
  const SDL_Rect inner = {
      kBodyRect.x + 1,
      kBodyRect.y + 1,
      kBodyRect.w - 2,
      kBodyRect.h - 2,
  };
  SDL_SetRenderDrawColor(state.renderer, 70, 74, 84, 0xff);
  SDL_RenderDrawRect(state.renderer, &inner);

  SDL_SetRenderDrawColor(state.renderer, 18, 20, 24, 0xff);
  SDL_RenderFillRect(state.renderer, &kScreenRect);
  SDL_SetRenderDrawColor(state.renderer, 92, 98, 112, 0xff);
  SDL_RenderDrawRect(state.renderer, &kScreenRect);
}

bool buttonsDisabledByEnv(void) {
  const char *value = std::getenv("FURBLE_SIM_BUTTONS");
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  std::string setting(value);
  std::transform(setting.begin(), setting.end(), setting.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return setting == "0" || setting == "false" || setting == "no" || setting == "off";
}

bool headlessVideoDriver(void) {
  const char *driver = SDL_GetCurrentVideoDriver();
  if (driver == nullptr) {
    return true;
  }
  return std::strcmp(driver, "dummy") == 0 || std::strcmp(driver, "offscreen") == 0;
}

int buttonAt(int x, int y) {
  y -= state.contentTop;
  for (size_t index = 0; index < kButtonCount; ++index) {
    const SDL_Rect &rect = kButtons[index].rect;
    if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void releasePress(void) {
  if (state.pressed < 0) {
    return;
  }
  const int index = state.pressed;
  state.pressed = -1;
  state.releasePending = false;
  SDL_CaptureMouse(SDL_FALSE);
  if (static_cast<size_t>(index) < kButtonCount) {
    lgfx::gpio_hi(kButtons[index].gpio);
    state.lastChange[index] = SDL_GetTicks();
  }
  state.dirty = true;
}

void press(int index) {
  if (index < 0 || static_cast<size_t>(index) >= kButtonCount) {
    return;
  }
  // A mouse button generates exactly one press transition. Duplicate SDL
  // events, a stray redelivery, or a click inside the debounce window must not
  // queue a second firmware press on the same virtual button.
  if (state.pressed == index) {
    // A second down on the held button is a no-op, but it cancels a pending
    // minimum-duration release so a quick re-press cannot be cut short.
    state.releasePending = false;
    return;
  }
  const uint32_t now = SDL_GetTicks();
  if (state.lastChange[index] != kNoChange && now - state.lastChange[index] < kDebounceMs) {
    return;
  }
  if (state.pressed >= 0) {
    releasePress();
  }
  // Mouse capture is best effort. Release is also driven by any left-button up
  // (regardless of window), pointer leave, focus loss, close, and the periodic
  // latch guard, so a failed capture cannot leave the GPIO latched.
  static_cast<void>(SDL_CaptureMouse(SDL_TRUE));
  state.pressed = index;
  state.pressStarted = now;
  state.releasePending = false;
  state.lastChange[index] = now;
  lgfx::gpio_lo(kButtons[index].gpio);
  state.dirty = true;
}

void enqueueAction(const action_t &action) {
  const std::lock_guard<std::mutex> lock(actionMutex);
  if (actionCount == kActionQueueCapacity) {
    if (action.type == action_type_t::HOVER) {
      // Motion is streamed continuously; dropping one hover sample is harmless
      // and keeps press/release/close room in the queue.
      return;
    }
    actionOverflow.store(true, std::memory_order_release);
    return;
  }
  actionQueue[(actionHead + actionCount) % kActionQueueCapacity] = action;
  ++actionCount;
}

void clearActions(void) {
  const std::lock_guard<std::mutex> lock(actionMutex);
  actionHead = 0;
  actionCount = 0;
  actionOverflow.store(false, std::memory_order_release);
}

void drainActions(void) {
  std::array<action_t, kActionQueueCapacity> actions {};
  size_t count = 0;
  bool overflow = false;
  {
    const std::lock_guard<std::mutex> lock(actionMutex);
    count = actionCount;
    for (size_t index = 0; index < count; ++index) {
      actions[index] = actionQueue[(actionHead + index) % kActionQueueCapacity];
    }
    actionHead = 0;
    actionCount = 0;
    overflow = actionOverflow.exchange(false, std::memory_order_acq_rel);
  }
  if (overflow) {
    // A press or release was dropped. Fail closed rather than leave a GPIO
    // latched; subsequent actions in the drained batch can press again.
    releasePress();
  }
  for (size_t index = 0; index < count; ++index) {
    const action_t &action = actions[index];
    switch (action.type) {
      case action_type_t::DOWN:
        press(buttonAt(action.x, action.y));
        break;
      case action_type_t::UP:
      case action_type_t::RELEASE:
        if (state.pressed >= 0 && SDL_GetTicks() - state.pressStarted < kMinimumPressMs) {
          state.releasePending = true;
        } else {
          releasePress();
        }
        break;
      case action_type_t::HOVER:
      {
        const int hover = buttonAt(action.x, action.y);
        if (hover != state.hover) {
          state.hover = hover;
          state.dirty = true;
        }
        break;
      }
      case action_type_t::CLOSE:
        releasePress();
        state.closeRequested = true;
        break;
    }
  }
}

void drawButton(int index) {
  const SDL_Rect &rect = kButtons[index].rect;
  const bool pressed = state.pressed == index;
  const bool hovered = state.hover == index;

  if (pressed) {
    SDL_SetRenderDrawColor(state.renderer, 236, 176, 48, 0xff);
  } else if (hovered) {
    SDL_SetRenderDrawColor(state.renderer, 78, 84, 98, 0xff);
  } else {
    SDL_SetRenderDrawColor(state.renderer, 58, 62, 72, 0xff);
  }
  SDL_RenderFillRect(state.renderer, &rect);

  if (pressed) {
    SDL_SetRenderDrawColor(state.renderer, 0xff, 0xf4, 0xd0, 0xff);
  } else if (hovered) {
    SDL_SetRenderDrawColor(state.renderer, 176, 184, 200, 0xff);
  } else {
    SDL_SetRenderDrawColor(state.renderer, 112, 118, 132, 0xff);
  }
  SDL_RenderDrawRect(state.renderer, &rect);

  const char *label = kButtons[index].label;
  const int scale = std::strlen(label) > 1 ? 2 : 5;
  const int textX = rect.x + (rect.w - textWidth(label, scale)) / 2;
  const int textY = rect.y + (rect.h - 7 * scale) / 2 + (pressed ? 2 : 0);
  if (pressed) {
    SDL_SetRenderDrawColor(state.renderer, 26, 28, 32, 0xff);
  } else {
    SDL_SetRenderDrawColor(state.renderer, 238, 241, 246, 0xff);
  }
  drawText(state.renderer, label, textX, textY, scale);
}

void drawArrow(int centerX, int topY, int scale, bool up) {
  // Filled triangle with a short stem. Six rows of widening bars read as an
  // arrowhead at both 1x and 2x, and the stem keeps the direction obvious
  // without a font or an image asset.
  for (int row = 0; row < 6; ++row) {
    const int width = (row + 1) * scale;
    const int y = up ? topY + row * scale : topY + (5 - row) * scale;
    const SDL_Rect bar = {centerX - width / 2, y, width, scale};
    SDL_RenderFillRect(state.renderer, &bar);
  }
  const SDL_Rect stem = {
      centerX - scale / 2,
      up ? topY + 6 * scale : topY - 2 * scale,
      scale,
      2 * scale,
  };
  SDL_RenderFillRect(state.renderer, &stem);
}

void drawOrientationCaptions(void) {
  SDL_SetRenderDrawColor(state.renderer, 150, 156, 170, 0xff);
#if defined(FURBLE_M5COREX)
  // The Core's three buttons are on the front face below the screen, so this is
  // a front view. The arrows point down at the button row.
  drawCenteredText("FRONT VIEW", 2, 2);
  drawArrow(76, 4, 2, false);
  drawArrow(224, 4, 2, false);
  drawCenteredText("FRONT EDGE", kWindowHeight - 18, 2);
#elif defined(FURBLE_M5STICKS3)
  // "TOP VIEW" is centered over the body and B sits on the top edge below it.
  // The up arrows name the top of the device, and the bottom "FRONT" caption
  // with down arrows names the edge A belongs to. The layout therefore still
  // reads as a top view when the panel sits to the right of this strip.
  drawCenteredText("TOP VIEW", 2, 2);
  drawArrow(80, 2, 2, true);
  drawArrow(220, 2, 2, true);
  drawCenteredText("FRONT", kWindowHeight - 18, 2);
  drawArrow(100, kWindowHeight - 18, 2, false);
  drawArrow(200, kWindowHeight - 18, 2, false);
#elif defined(FURBLE_M5STICKC)
  drawCenteredText("TOP VIEW", 1, 1);
  drawArrow(110, 1, 1, true);
  drawArrow(190, 1, 1, true);
  drawCenteredText("FRONT", kWindowHeight - 9, 1);
  drawArrow(120, kWindowHeight - 9, 1, false);
  drawArrow(180, kWindowHeight - 9, 1, false);
#endif
}

void draw(void) {
  if (state.renderer == nullptr) {
    return;
  }
  SDL_SetRenderDrawColor(state.renderer, 24, 26, 30, 0xff);
  SDL_RenderClear(state.renderer);
  // The strip's top inset matches the panel title bar, so the two outer
  // frames share a top edge. Draw the top-view content in the same coordinate
  // space as before by offsetting the viewport.
  const SDL_Rect viewport = {0, state.contentTop, kWindowWidth, kWindowHeight};
  SDL_RenderSetViewport(state.renderer, &viewport);
  drawDeviceOutline();
  for (size_t index = 0; index < kButtonCount; ++index) {
    drawButton(static_cast<int>(index));
  }
  drawOrientationCaptions();
  SDL_RenderSetViewport(state.renderer, nullptr);
  SDL_RenderPresent(state.renderer);
  state.dirty = false;
}

void destroyWindow(void) {
  releasePress();
  clearActions();
  if (state.renderer != nullptr) {
    SDL_DestroyRenderer(state.renderer);
    state.renderer = nullptr;
  }
  if (state.window != nullptr) {
    SDL_DestroyWindow(state.window);
    state.window = nullptr;
  }
  state.enabled = false;
  state.closeRequested = false;
  state.hover = -1;
  state.contentTop = 0;
  state.dirty = false;
  state.windowId = 0;
  active.store(false, std::memory_order_release);
}

void restoreEventFilter(void) {
  filterEnabled.store(false, std::memory_order_release);
  filterWindowId.store(0, std::memory_order_release);
  if (!state.filterInstalled) {
    return;
  }
  if (state.hadPreviousFilter) {
    SDL_SetEventFilter(state.previousFilter, state.previousFilterUserdata);
  } else {
    SDL_SetEventFilter(nullptr, nullptr);
  }
  state.filterInstalled = false;
  state.previousFilter = nullptr;
  state.previousFilterUserdata = nullptr;
}

int filterEvent(void *userdata, SDL_Event *event) {
  static_cast<void>(userdata);
  if (event == nullptr || !filterEnabled.load(std::memory_order_acquire)) {
    return 1;
  }
  const uint32_t windowId = filterWindowId.load(std::memory_order_acquire);

  switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
      if (event->button.windowID != windowId || event->button.button != SDL_BUTTON_LEFT) {
        return 1;
      }
      enqueueAction({action_type_t::DOWN, event->button.x, event->button.y});
      return 0;
    case SDL_MOUSEBUTTONUP:
      // A left-button up releases the virtual button even when the pointer left
      // the control window before releasing. Let the panel keep its own mouse
      // up event when it belongs to the panel window.
      if (event->button.button != SDL_BUTTON_LEFT) {
        return 1;
      }
      enqueueAction({action_type_t::UP, 0, 0});
      return event->button.windowID == windowId ? 0 : 1;
    case SDL_MOUSEMOTION:
      if (event->motion.windowID != windowId) {
        return 1;
      }
      enqueueAction({action_type_t::HOVER, event->motion.x, event->motion.y});
      return 0;
    case SDL_WINDOWEVENT:
      if (event->window.windowID != windowId) {
        return 1;
      }
      switch (event->window.event) {
        case SDL_WINDOWEVENT_CLOSE:
          enqueueAction({action_type_t::CLOSE, 0, 0});
          return 0;
        case SDL_WINDOWEVENT_LEAVE:
        case SDL_WINDOWEVENT_FOCUS_LOST:
        case SDL_WINDOWEVENT_HIDDEN:
          enqueueAction({action_type_t::RELEASE, 0, 0});
          return 0;
        default:
          return 0;
      }
    case SDL_QUIT:
      // Let the panel execute its normal quit path, but never leave a held
      // virtual button behind while the process unwinds.
      enqueueAction({action_type_t::RELEASE, 0, 0});
      return 1;
    default:
      return 1;
  }
}

struct window_frame_t {
  // Client/content rectangle, which is what SDL_GetWindowPosition and
  // SDL_GetWindowSize report. The control strip is borderless, so aligning its
  // content with this rectangle is what makes the two windows read as one.
  int x;
  int y;
  int w;
  int h;
  int top;
  int left;
  int bottom;
  int right;
};

bool windowFrame(SDL_Window *window, window_frame_t *frame) {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int top = 0;
  int left = 0;
  int bottom = 0;
  int right = 0;
  SDL_GetWindowPosition(window, &x, &y);
  SDL_GetWindowSize(window, &w, &h);
  if (SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right) != 0) {
    // SDL_GetWindowSize reports the content area, not the title bar. Assume a
    // title bar when the border query is unavailable so an above/below
    // placement cannot clip the panel's bottom-edge overlays.
    top = 32;
    left = 0;
    bottom = 0;
    right = 0;
  }
  frame->x = x;
  frame->y = y;
  frame->w = w;
  frame->h = h;
  frame->top = top;
  frame->left = left;
  frame->bottom = bottom;
  frame->right = right;
  return true;
}

void placeWindow(SDL_Window *panel, int *x, int *y) {
  *x = kPanelGap;
  *y = kPanelGap;
  if (panel == nullptr) {
    return;
  }

  window_frame_t frame = {};
  if (!windowFrame(panel, &frame)) {
    return;
  }
  const int display = SDL_GetWindowDisplayIndex(panel);
  SDL_Rect usable = {0, 0, 0, 0};
  if (display < 0 || SDL_GetDisplayUsableBounds(display, &usable) != 0) {
    return;
  }

  const int right = usable.x + usable.w;
  const int bottom = usable.y + usable.h;
  const int fullTop = frame.y - frame.top;
  const int fullBottom = frame.y + frame.h + frame.bottom;
  const int fullLeft = frame.x - frame.left;
  const int fullRight = frame.x + frame.w + frame.right;
  const int windowHeight = kWindowHeight + frame.top;
  // Align the strip's outer top with the panel's outer top. The strip draws a
  // title-bar-height inset above its top-view content, so the two outer
  // rectangles share a top and bottom edge.
  const int alignedY = fullTop;
  const int centeredX = frame.x + (frame.w - kWindowWidth) / 2;
  const int leftX = fullLeft - kPanelGap - kWindowWidth;
  const int belowY = fullBottom + kPanelGap;
  const int aboveY = fullTop - kPanelGap - windowHeight;
  const int rightX = fullRight + kPanelGap;

  // Prefer the left of the panel, then below, then above. The right-hand side
  // is last: it is the most useful screen space to leave uncovered, and the
  // control surface does not need to consume it. Side placement is flush with
  // the panel and top-aligned, so the strip and the panel read as one window
  // even though M5GFX owns the panel window.
  if (leftX >= usable.x) {
    *x = leftX;
    *y = alignedY;
  } else if (belowY + windowHeight <= bottom) {
    *x = centeredX;
    *y = belowY;
  } else if (aboveY >= usable.y) {
    *x = centeredX;
    *y = aboveY;
  } else if (rightX + kWindowWidth <= right) {
    *x = rightX;
    *y = alignedY;
  } else {
    // No placement clears the panel on a very small display. Stay on the left
    // side rather than consuming the rightmost screen space.
    *x = usable.x + kPanelGap;
    *y = usable.y + kPanelGap;
  }

  if (*x + kWindowWidth > right) {
    *x = right - kWindowWidth;
  }
  if (*x < usable.x) {
    *x = usable.x;
  }
  if (*y + windowHeight > bottom) {
    *y = bottom - windowHeight;
  }
  if (*y < usable.y) {
    *y = usable.y;
  }
}

// Keep the strip attached when the user moves or resizes the panel window. The
// panel window is owned by M5GFX, so store only its ID and re-resolve it every
// tick; a raw pointer would dangle after Panel_sdl destroys the window. SDL2
// has no window-parenting API, so following the position is the closest safe
// approximation of one attached window.
void followPanel(void) {
  if (state.panelWindowId == 0 || state.window == nullptr) {
    return;
  }
  SDL_Window *panel = SDL_GetWindowFromID(state.panelWindowId);
  if (panel == nullptr || (SDL_GetWindowFlags(panel) & SDL_WINDOW_SHOWN) == 0) {
    return;
  }
  int desiredX = 0;
  int desiredY = 0;
  placeWindow(panel, &desiredX, &desiredY);
  int currentX = 0;
  int currentY = 0;
  SDL_GetWindowPosition(state.window, &currentX, &currentY);
  if (desiredX != currentX || desiredY != currentY) {
    SDL_SetWindowPosition(state.window, desiredX, desiredY);
  }
}

SDL_Window *findPanelWindow(void) {
  if (SDL_Window *focused = SDL_GetKeyboardFocus(); focused != nullptr) {
    return focused;
  }
  if (SDL_Window *mouse = SDL_GetMouseFocus(); mouse != nullptr) {
    return mouse;
  }

  // SDL2 has no public window enumeration. The panel is the only shown SDL
  // window in this process before the control window is created, and window
  // IDs are allocated from a small monotonic counter, so probe the low IDs for
  // a shown window. This keeps placement working when the app is not the
  // foreground application and has no keyboard or mouse focus.
  for (Uint32 id = 1; id <= 64; ++id) {
    SDL_Window *window = SDL_GetWindowFromID(id);
    if (window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_SHOWN) != 0) {
      return window;
    }
  }
  return nullptr;
}

bool createWindow(void) {
  // Find the panel before this window exists. SDL_GetKeyboardFocus() alone is
  // not enough on macOS when another application is foreground.
  SDL_Window *panel = findPanelWindow();
  state.panelWindowId = panel == nullptr ? 0 : SDL_GetWindowID(panel);

  window_frame_t frame = {};
  if (panel != nullptr && windowFrame(panel, &frame)) {
    state.contentTop = frame.top;
  }

  int x = 0;
  int y = 0;
  placeWindow(panel, &x, &y);

  // Borderless: the strip must read as part of the panel, not as a second
  // decorated window. Its top inset matches the panel title bar and its
  // content height matches the panel content, so the outer rectangles align
  // and there is no gap to break the outline.
  const int windowHeight = kWindowHeight + state.contentTop;
  state.window =
      SDL_CreateWindow(kWindowTitle, x, y, kWindowWidth, windowHeight,
                       SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI);
  if (state.window == nullptr) {
    std::fprintf(stderr, "SIM: on-screen buttons unavailable: %s\n", SDL_GetError());
    return false;
  }
  state.windowId = SDL_GetWindowID(state.window);

  state.renderer = SDL_CreateRenderer(state.window, -1, SDL_RENDERER_ACCELERATED);
  if (state.renderer == nullptr) {
    state.renderer = SDL_CreateRenderer(state.window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (state.renderer == nullptr) {
    std::fprintf(stderr, "SIM: on-screen buttons unavailable: %s\n", SDL_GetError());
    SDL_DestroyWindow(state.window);
    state.window = nullptr;
    state.windowId = 0;
    return false;
  }
  // Hit testing uses window points, which are the same units as this logical
  // surface on both 1x and 2x macOS displays. Never compare a mouse event to
  // SDL_GetRendererOutputSize(): that is the backing pixel size on Retina.
  SDL_RenderSetLogicalSize(state.renderer, kWindowWidth, windowHeight);

  // SDL2 has a single process-level event filter rather than additive
  // filters. M5GFX does not install one, but preserve whatever was present so
  // a future host integration does not lose it.
  state.hadPreviousFilter =
      SDL_GetEventFilter(&state.previousFilter, &state.previousFilterUserdata) == SDL_TRUE;
  filterWindowId.store(state.windowId, std::memory_order_release);
  filterEnabled.store(true, std::memory_order_release);
  SDL_SetEventFilter(filterEvent, nullptr);
  state.filterInstalled = true;
  state.enabled = true;
  state.dirty = true;
  active.store(true, std::memory_order_release);

  // Do not call SDL_SetWindowInputFocus() here. On macOS it can block while
  // another application owns the foreground, and the mapped a/b/c/p keys work
  // in any window because Panel_sdl maps them process-wide. Leaving focus where
  // the user had it also avoids stealing the keyboard from the panel.
  std::fprintf(stderr, "SIM: on-screen buttons %s (FURBLE_SIM_BUTTONS=0 disables)\n", kWindowTitle);
  return true;
}

bool initLocked(void) {
  if (state.started) {
    return state.enabled;
  }
  state.started = true;

  if (kButtonCount == 0) {
    return false;
  }
  if (!uiButtonsRequested()) {
    // A script or the fuzzer owns the input stream, or the user opted out.
    // Keep its event sequence deterministic even with a visible panel.
    return false;
  }
  if (headlessVideoDriver()) {
    // Scripted CI runs use SDL_VIDEODRIVER=dummy; no window is created there.
    return false;
  }
  if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
    return false;
  }
  return createWindow();
}

}  // namespace

bool uiButtonsRequested(void) {
  return Sim::interactiveRun() && !buttonsDisabledByEnv();
}

bool uiButtonsInit(void) {
  return initLocked();
}

void uiButtonsTick(void) {
  if (!state.started) {
    initLocked();
  }
  if (!state.enabled) {
    return;
  }
  drainActions();
  followPanel();
  if (state.closeRequested) {
    restoreEventFilter();
    destroyWindow();
    return;
  }

  // Latch guard: a drag that leaves the window, loses capture, or misses the
  // final up event must not leave the emulated GPIO low. A pending minimum
  // press keeps the pin low until its own deadline, then releases.
  if (state.pressed >= 0) {
    if (state.releasePending) {
      if (SDL_GetTicks() - state.pressStarted >= kMinimumPressMs) {
        releasePress();
      }
    } else if ((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) == 0
               || SDL_GetMouseFocus() != state.window) {
      releasePress();
    }
  }

  if (state.dirty) {
    draw();
  }
}

void uiButtonsShutdown(void) {
  restoreEventFilter();
  destroyWindow();
}

bool uiButtonsActive(void) {
  return active.load(std::memory_order_acquire);
}

}  // namespace Furble::Sim
