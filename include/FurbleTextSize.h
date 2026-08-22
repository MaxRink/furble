#ifndef FURBLE_TEXT_SIZE_H
#define FURBLE_TEXT_SIZE_H

#include <cstdint>

namespace Furble {

// Board-conditional UI text size policy, shared by the settings default, the
// Text size roller and the font mapping. It is kept dependency free so the
// host tests can evaluate the per-board policy without the NVS or LVGL headers
// that a compiled FurbleSettings.cpp or FurbleUI.cpp pulls in.
//
// The values match Settings::text_size_t: 0 Small, 1 Normal, 2 Large. A
// static_assert in FurbleSettings.cpp pins that correspondence so this header
// stays in sync with the enum.
//
// The 80x160 M5StickC panel is too small to carry the Large font without
// clipping, so that board caps the selectable size at Normal and defaults a
// fresh device to Small, matching the primary user's preference for smaller
// text on the tiny screen. Every other board keeps all three sizes and the
// Normal default.
namespace TextSizePolicy {

constexpr uint8_t SMALL = 0;
constexpr uint8_t NORMAL = 1;
constexpr uint8_t LARGE = 2;

#if defined(FURBLE_M5STICKC)
constexpr uint8_t DEFAULT = SMALL;
constexpr uint8_t MAX = NORMAL;
#else
constexpr uint8_t DEFAULT = NORMAL;
constexpr uint8_t MAX = LARGE;
#endif

// The number of sizes offered on this board, so Small is index 0 up to MAX.
constexpr uint8_t COUNT = MAX + 1;

// Clamp a stored or selected size to the range this board supports, so a value
// migrated in from another board or forced past the board maximum can never
// select a font that overflows the panel at render time.
constexpr uint8_t clamp(uint8_t size) {
  return (size > MAX) ? MAX : size;
}

}  // namespace TextSizePolicy

}  // namespace Furble

#endif
