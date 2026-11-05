#include <cstdint>
#include <iostream>

#include "FurbleUIMemory.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

}  // namespace

int main() {
  using namespace Furble::UIMemory;

  // The old fixed buffer was 320 * 16 * 2 = 10240 bytes per draw buffer.
  check(drawBufferBytes(320, 240, 2) == 10240, "320x240 changed the Core2 draw buffer");
  // Narrow panels halve the partial rows: 135x240 keeps 8 rows, 80x160 keeps 5.
  check(drawBufferBytes(135, 240, 2) == 2160, "135x240 was not sized to the StickS3 panel");
  check(drawBufferBytes(80, 160, 2) == 800, "80x160 was not sized to the StickC panel");
  // A landscape 240x135 panel is wider than the narrow cutoff and must keep the
  // full 240/15 row count instead of growing its buffer.
  check(drawBufferBytes(240, 135, 2) == 4320, "240x135 regressed to the wide row count");
  check(drawBufferBytes(135, 240, 2) < drawBufferBytes(320, 240, 2),
        "a narrower panel did not free internal DMA memory");

  // Degenerate dimensions must still produce a usable one-line buffer.
  check(drawBufferBytes(0, 0, 2) == 2, "zero dimensions did not clamp to one line");
  check(drawBufferBytes(-1, -1, 2) == 2, "negative dimensions did not clamp to one line");
  check(drawBufferBytes(1000, 1000, 2) == 10240, "oversized dimensions were not capped");

  if (failures != 0) {
    return 1;
  }
  std::cout << "ui_memory_test: PASS\n";
  return 0;
}
