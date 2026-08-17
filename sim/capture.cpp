#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <M5Unified.h>

#include "capture.h"

namespace Furble::Sim {
namespace {

uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

uint32_t adler32(const std::vector<uint8_t> &data) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (uint8_t byte : data) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

void appendBigEndian(std::vector<uint8_t> &output, uint32_t value) {
  output.push_back(static_cast<uint8_t>(value >> 24));
  output.push_back(static_cast<uint8_t>(value >> 16));
  output.push_back(static_cast<uint8_t>(value >> 8));
  output.push_back(static_cast<uint8_t>(value));
}

void appendChunk(std::vector<uint8_t> &png, const char type[4], const std::vector<uint8_t> &data) {
  appendBigEndian(png, static_cast<uint32_t>(data.size()));
  const size_t typeOffset = png.size();
  png.insert(png.end(), type, type + 4);
  png.insert(png.end(), data.begin(), data.end());
  appendBigEndian(png, crc32(png.data() + typeOffset, 4 + data.size()));
}

std::vector<uint8_t> deflateStore(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> compressed {0x78, 0x01};
  size_t offset = 0;
  while (offset < data.size() || offset == 0) {
    const size_t remaining = data.size() - offset;
    const uint16_t blockSize = static_cast<uint16_t>(std::min<size_t>(remaining, 65535));
    const bool finalBlock = offset + blockSize == data.size();
    compressed.push_back(finalBlock ? 0x01 : 0x00);
    compressed.push_back(static_cast<uint8_t>(blockSize));
    compressed.push_back(static_cast<uint8_t>(blockSize >> 8));
    const uint16_t inverse = static_cast<uint16_t>(~blockSize);
    compressed.push_back(static_cast<uint8_t>(inverse));
    compressed.push_back(static_cast<uint8_t>(inverse >> 8));
    compressed.insert(compressed.end(), data.begin() + offset, data.begin() + offset + blockSize);
    offset += blockSize;
    if (finalBlock) {
      break;
    }
  }
  const uint32_t checksum = adler32(data);
  compressed.push_back(static_cast<uint8_t>(checksum >> 24));
  compressed.push_back(static_cast<uint8_t>(checksum >> 16));
  compressed.push_back(static_cast<uint8_t>(checksum >> 8));
  compressed.push_back(static_cast<uint8_t>(checksum));
  return compressed;
}

}  // namespace

bool captureFrame(const std::string &path) {
  const int32_t width = M5.Display.width();
  const int32_t height = M5.Display.height();
  if (width <= 0 || height <= 0) {
    return false;
  }

  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  M5.Display.readRectRGB(0, 0, width, height, rgb.data());

  std::vector<uint8_t> scanlines;
  scanlines.reserve(static_cast<size_t>(height) * (width * 3 + 1));
  for (int32_t y = 0; y < height; ++y) {
    scanlines.push_back(0);
    const auto begin = rgb.begin() + static_cast<size_t>(y) * width * 3;
    scanlines.insert(scanlines.end(), begin, begin + width * 3);
  }

  std::vector<uint8_t> png {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
  };
  appendChunk(png, "IHDR",
              {static_cast<uint8_t>(width >> 24), static_cast<uint8_t>(width >> 16),
               static_cast<uint8_t>(width >> 8), static_cast<uint8_t>(width),
               static_cast<uint8_t>(height >> 24), static_cast<uint8_t>(height >> 16),
               static_cast<uint8_t>(height >> 8), static_cast<uint8_t>(height), 8, 2, 0, 0, 0});
  appendChunk(png, "IDAT", deflateStore(scanlines));
  appendChunk(png, "IEND", {});

  const std::filesystem::path output(path);
  std::filesystem::create_directories(output.parent_path());
  std::ofstream file(output, std::ios::binary | std::ios::trunc);
  if (!file) {
    return false;
  }
  file.write(reinterpret_cast<const char *>(png.data()), png.size());
  return file.good();
}

}  // namespace Furble::Sim
