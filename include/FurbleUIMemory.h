#ifndef FURBLE_UI_MEMORY_H
#define FURBLE_UI_MEMORY_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Furble {
namespace UIMemory {

/** Largest panel any supported controller has. */
inline constexpr int32_t MAX_WIDTH = 320;
inline constexpr int32_t MAX_HEIGHT = 240;

/**
 * LVGL renders in partial rows. Sixteen rows was the old fixed choice for a
 * 320x240 panel; using the same divisor on every panel keeps 16 rows on a
 * 240-line panel and scales shorter panels down in proportion.
 */
inline constexpr int32_t PARTIAL_LINES = 15;

/** Panels at or below this width are narrow enough to halve the partial rows. */
inline constexpr int32_t NARROW_PANEL_WIDTH = 160;

/**
 * Narrow panels use one draw buffer instead of two.
 *
 * The single-buffer flush callback waits for the SPI DMA before reporting the
 * flush complete, so LVGL cannot overwrite pixels still on the wire. The trade
 * is a serialised render and transfer for each band. On the StickS3 a 135x240
 * band is 2,160 bytes, about 0.216 ms of SPI time at 80 MHz, and a full-screen
 * redraw uses 30 bands. The UI does not animate full-screen content, so the
 * extra time appears only on page changes. This frees one 2,160-byte internal
 * DMA buffer on the StickS3 and one 800-byte buffer on the StickC.
 */
inline constexpr bool singleDrawBuffer(int32_t width) {
  return width <= NARROW_PANEL_WIDTH;
}

/**
 * Bytes for one LVGL partial-render buffer.
 *
 * The old code always allocated a 320x240 buffer even on the 135x240 StickS3
 * and the 80x160 StickC. Both draw buffers must stay in DMA-capable internal
 * RAM because they feed the SPI panel, so the wasted width was real internal
 * heap that the optional HTTPS server then could not use.
 *
 * Narrow panels halve the row count again. Wide panels keep two buffers:
 * displayFlush() reports the flush complete as soon as M5GFX starts the SPI
 * DMA, so LVGL renders the next band into the other buffer while the first one
 * is on the wire. Narrow panels use one buffer and wait for the DMA before the
 * ready callback, which serialises render with transfer but saves one internal
 * DMA allocation. On the StickS3 this drops the 135x240 buffer from 4,320 to
 * 2,160 bytes without changing the number of pixels transferred.
 */
inline constexpr size_t drawBufferBytes(int32_t width, int32_t height, size_t bytesPerPixel) {
  const int32_t boundedWidth = std::clamp<int32_t>(width, 1, MAX_WIDTH);
  const int32_t boundedHeight = std::clamp<int32_t>(height, PARTIAL_LINES, MAX_HEIGHT);
  int32_t lines = std::max<int32_t>(1, boundedHeight / PARTIAL_LINES);
  if (boundedWidth <= NARROW_PANEL_WIDTH) {
    lines = std::max<int32_t>(1, lines / 2);
  }
  return static_cast<size_t>(boundedWidth) * static_cast<size_t>(lines) * bytesPerPixel;
}

}  // namespace UIMemory
}  // namespace Furble

#endif
