#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
// a 2x drawable.
constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 340;
constexpr int kPanelGap = 16;
constexpr uint32_t kDebounceMs = 120;
constexpr size_t kMaxButtons = 3;

struct button_t {
  const char *label;
  uint8_t gpio;
  SDL_Rect rect;
};

// The modeled board decides the button set, exactly like the scenario `btn`
// verb and the keyboard mapping in FurblePlatformSim.cpp. The GPIO numbers are
// the emulated pins M5Unified's PC build samples, so a click and the matching
// keyboard letter reach the firmware through one input path, not two.
//
// The rectangles are a top view of the physical device. Cores put A/B/C along
// the front edge; Sticks put B on the top edge, A on the front edge and PWR on
// the left side. The device outline is drawn between them.
#if defined(FURBLE_M5COREX)
constexpr button_t kButtons[] = {
    {"A", 39, {68, 228, 56, 40}},   // BtnA, front left
    {"B", 38, {132, 228, 56, 40}},  // BtnB, front middle
    {"C", 37, {196, 228, 56, 40}},  // BtnC, front right
};
constexpr size_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
constexpr SDL_Rect kBodyRect = {70, 76, 180, 140};
constexpr SDL_Rect kScreenRect = {88, 96, 144, 64};
constexpr const char *kWindowTitle = "furble buttons - Core front: A B C";
#elif defined(FURBLE_M5STICKC) || defined(FURBLE_M5STICKS3)
constexpr button_t kButtons[] = {
    {"A", 39, {128, 272, 64, 40}},     // BtnA, front/bottom edge
    {"B", 38, {128, 24, 64, 40}},      // BtnB, top edge
    {"PWR", 36, {56, 140, 44, 56}},    // BtnPWR, left side
};
constexpr size_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
constexpr SDL_Rect kBodyRect = {112, 76, 96, 184};
constexpr SDL_Rect kScreenRect = {128, 96, 64, 60};
constexpr const char *kWindowTitle = "furble buttons - Stick: B top, A front, PWR left";
#else
// An unmodeled board gets no control surface rather than the wrong buttons.
constexpr button_t kButtons[] = {{"", 0, {0, 0, 0, 0}}};
constexpr size_t kButtonCount = 0;
constexpr SDL_Rect kBodyRect = {0, 0, 0, 0};
constexpr SDL_Rect kScreenRect = {0, 0, 0, 0};
constexpr const char *kWindowTitle = "furble simulator buttons";
#endif

struct state_t {
  bool started;
  bool enabled;
  bool filterInstalled;
  bool closeRequested;
  bool dirty;
  bool hadPreviousFilter;
  int hover;
  int pressed;
  uint32_t windowId;
  uint32_t lastChange[kMaxButtons];
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_EventFilter previousFilter;
  void *previousFilterUserdata;
};

state_t state = {};

// Read by the UI task through uiButtonsActive(). The SDL window and renderer
// themselves stay main-thread owned.
std::atomic<bool> active {false};

// 5x7 glyphs for the button labels and the TOP/FRONT orientation captions.
// The control surface has no font dependency on purpose: SDL2 alone is already
// a simulator requirement, while SDL_ttf is not.
constexpr uint8_t kGlyphA[7] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
constexpr uint8_t kGlyphB[7] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
constexpr uint8_t kGlyphC[7] = {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f};
constexpr uint8_t kGlyphE[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
constexpr uint8_t kGlyphF[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
constexpr uint8_t kGlyphL[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
constexpr uint8_t kGlyphN[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
constexpr uint8_t kGlyphO[7] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
constexpr uint8_t kGlyphP[7] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
constexpr uint8_t kGlyphR[7] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
constexpr uint8_t kGlyphT[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
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
    case 'E':
      return kGlyphE;
    case 'F':
      return kGlyphF;
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
  std::transform(setting.begin(), setting.end(), setting.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
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
    return;
  }
  const uint32_t now = SDL_GetTicks();
  if (now - state.lastChange[index] < kDebounceMs) {
    return;
  }
  if (state.pressed >= 0) {
    releasePress();
  }
  // Fail closed when the pointer cannot be captured: without capture a release
  // outside the window may never arrive, which would leave the GPIO latched.
  if (SDL_CaptureMouse(SDL_TRUE) != 0) {
    return;
  }
  state.pressed = index;
  state.lastChange[index] = now;
  lgfx::gpio_lo(kButtons[index].gpio);
  state.dirty = true;
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

void draw(void) {
  if (state.renderer == nullptr) {
    return;
  }
  SDL_SetRenderDrawColor(state.renderer, 24, 26, 30, 0xff);
  SDL_RenderClear(state.renderer);
  drawDeviceOutline();
  for (size_t index = 0; index < kButtonCount; ++index) {
    drawButton(static_cast<int>(index));
  }
  SDL_SetRenderDrawColor(state.renderer, 150, 156, 170, 0xff);
  drawCenteredText("TOP", 4, 2);
  drawCenteredText("FRONT", kWindowHeight - 22, 2);
  SDL_RenderPresent(state.renderer);
  state.dirty = false;
}

void destroyWindow(void) {
  releasePress();
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
  state.dirty = false;
  state.windowId = 0;
  active.store(false, std::memory_order_release);
}

void restoreEventFilter(void) {
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
  if (event == nullptr || !state.enabled) {
    return 1;
  }

  switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
      if (event->button.windowID != state.windowId || event->button.button != SDL_BUTTON_LEFT) {
        return 1;
      }
      press(buttonAt(event->button.x, event->button.y));
      return 0;
    case SDL_MOUSEBUTTONUP:
      // A left-button up releases the virtual button even when the pointer left
      // the control window before releasing. Let the panel keep its own mouse
      // up event when it belongs to the panel window.
      if (event->button.button != SDL_BUTTON_LEFT) {
        return 1;
      }
      if (state.pressed >= 0) {
        releasePress();
      }
      return event->button.windowID == state.windowId ? 0 : 1;
    case SDL_MOUSEMOTION: {
      if (event->motion.windowID != state.windowId) {
        return 1;
      }
      const int hover = buttonAt(event->motion.x, event->motion.y);
      if (hover != state.hover) {
        state.hover = hover;
        state.dirty = true;
      }
      return 0;
    }
    case SDL_WINDOWEVENT:
      if (event->window.windowID != state.windowId) {
        return 1;
      }
      switch (event->window.event) {
        case SDL_WINDOWEVENT_CLOSE:
          releasePress();
          state.closeRequested = true;
          return 0;
        case SDL_WINDOWEVENT_LEAVE:
        case SDL_WINDOWEVENT_FOCUS_LOST:
        case SDL_WINDOWEVENT_HIDDEN:
          releasePress();
          return 0;
        default:
          return 0;
      }
    case SDL_QUIT:
      // Let the panel execute its normal quit path, but never leave a held
      // virtual button behind while the process unwinds.
      releasePress();
      return 1;
    default:
      return 1;
  }
}

struct window_frame_t {
  int x;
  int y;
  int w;
  int h;
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
    // SDL_GetWindowSize on macOS reports the content area, not the title bar.
    // Assume a title bar when the border query is unavailable so a below/above
    // placement cannot clip the panel's bottom-edge overlays.
    top = 32;
    left = 0;
    bottom = 0;
    right = 0;
  }
  frame->x = x;
  frame->y = y;
  frame->w = w + left + right;
  frame->h = h + top + bottom;
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
  const int centeredY = frame.y + (frame.h - kWindowHeight) / 2;
  const int centeredX = frame.x + (frame.w - kWindowWidth) / 2;

  // Prefer a side-by-side layout so the panel, including its bottom-edge
  // button overlays, stays completely visible. Fall back to below, then above.
  int candidateX = frame.x + frame.w + kPanelGap;
  if (candidateX + kWindowWidth <= right) {
    *x = candidateX;
    *y = centeredY;
  } else {
    candidateX = frame.x - kPanelGap - kWindowWidth;
    if (candidateX >= usable.x) {
      *x = candidateX;
      *y = centeredY;
    } else {
      const int candidateBelow = frame.y + frame.h + kPanelGap;
      if (candidateBelow + kWindowHeight <= bottom) {
        *x = centeredX;
        *y = candidateBelow;
      } else {
        const int candidateAbove = frame.y - kPanelGap - kWindowHeight;
        if (candidateAbove >= usable.y) {
          *x = centeredX;
          *y = candidateAbove;
        } else {
          // No placement clears the panel on a very small display. Use the
          // bottom-right corner rather than covering the panel's top overlays.
          *x = right - kWindowWidth - kPanelGap;
          *y = bottom - kWindowHeight - kPanelGap;
        }
      }
    }
  }

  if (*x + kWindowWidth > right) {
    *x = right - kWindowWidth;
  }
  if (*x < usable.x) {
    *x = usable.x;
  }
  if (*y + kWindowHeight > bottom) {
    *y = bottom - kWindowHeight;
  }
  if (*y < usable.y) {
    *y = usable.y;
  }
}

bool createWindow(void) {
  // Capture the panel before this window exists. SDL_GetKeyboardFocus() would
  // otherwise start returning the button window after creation.
  SDL_Window *panel = SDL_GetKeyboardFocus();

  int x = 0;
  int y = 0;
  placeWindow(panel, &x, &y);

  state.window = SDL_CreateWindow(kWindowTitle, x, y, kWindowWidth, kWindowHeight,
                                  SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
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
  SDL_RenderSetLogicalSize(state.renderer, kWindowWidth, kWindowHeight);

  // SDL2 has a single process-level event filter rather than additive
  // filters. M5GFX does not install one, but preserve whatever was present so
  // a future host integration does not lose it.
  state.hadPreviousFilter =
      SDL_GetEventFilter(&state.previousFilter, &state.previousFilterUserdata) == SDL_TRUE;
  SDL_SetEventFilter(filterEvent, nullptr);
  state.filterInstalled = true;
  state.enabled = true;
  state.dirty = true;
  active.store(true, std::memory_order_release);

  // Restore focus to the panel: mapped a/b/c/p keys work in any window because
  // Panel_sdl maps them globally, but r/l rotation and 1-6 scaling require the
  // panel window under the cursor. Ignore failure on window managers that
  // refuse programmatic focus changes.
  if (panel != nullptr) {
    static_cast<void>(SDL_SetWindowInputFocus(panel));
  }

  std::fprintf(stderr, "SIM: on-screen buttons %s (FURBLE_SIM_BUTTONS=0 disables)\n",
               kWindowTitle);
  return true;
}

}  // namespace

bool uiButtonsInit(void) {
  if (state.started) {
    return state.enabled;
  }
  state.started = true;

  if (kButtonCount == 0) {
    return false;
  }
  if (!Sim::interactiveRun()) {
    // A script or the fuzzer owns the input stream. Keep its event sequence
    // deterministic even when the run has a visible panel.
    return false;
  }
  if (buttonsDisabledByEnv()) {
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

void uiButtonsTick(void) {
  if (!state.started) {
    uiButtonsInit();
  }
  if (!state.enabled) {
    return;
  }
  if (state.closeRequested) {
    restoreEventFilter();
    destroyWindow();
    return;
  }

  // Latch guard: a drag that leaves the window, loses capture, or misses the
  // final up event must not leave the emulated GPIO low.
  if (state.pressed >= 0) {
    if ((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) == 0 ||
        SDL_GetMouseFocus() != state.window) {
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
