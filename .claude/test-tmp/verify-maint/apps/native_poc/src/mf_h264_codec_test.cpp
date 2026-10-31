#define NOMINMAX
#include "mf_h264_codec.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

uint8_t clamp_u8(int value) {
  return static_cast<uint8_t>(std::min(255, std::max(0, value)));
}

std::vector<uint8_t> scalar_reference(const uint8_t* bgra, uint32_t width, uint32_t height,
                                      uint32_t stride) {
  const size_t yBytes = static_cast<size_t>(width) * height;
  std::vector<uint8_t> out(yBytes + static_cast<size_t>(width) * ((height + 1) / 2));
  uint8_t* yPlane = out.data();
  uint8_t* uvPlane = out.data() + yBytes;
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* row = bgra + static_cast<size_t>(y) * stride;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t* p = row + static_cast<size_t>(x) * 4;
      yPlane[static_cast<size_t>(y) * width + x] =
          clamp_u8(((47 * p[2] + 157 * p[1] + 16 * p[0] + 128) >> 8) + 16);
    }
  }
  for (uint32_t y = 0; y < height; y += 2) {
    const uint32_t y1 = std::min(y + 1, height - 1);
    const uint8_t* row0 = bgra + static_cast<size_t>(y) * stride;
    const uint8_t* row1 = bgra + static_cast<size_t>(y1) * stride;
    uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * width;
    for (uint32_t x = 0; x < width; x += 2) {
      const uint32_t x1 = std::min(x + 1, width - 1);
      const uint8_t* p00 = row0 + static_cast<size_t>(x) * 4;
      const uint8_t* p10 = row0 + static_cast<size_t>(x1) * 4;
      const uint8_t* p01 = row1 + static_cast<size_t>(x) * 4;
      const uint8_t* p11 = row1 + static_cast<size_t>(x1) * 4;
      const int r = (p00[2] + p10[2] + p01[2] + p11[2] + 2) >> 2;
      const int g = (p00[1] + p10[1] + p01[1] + p11[1] + 2) >> 2;
      const int b = (p00[0] + p10[0] + p01[0] + p11[0] + 2) >> 2;
      uv[x] = clamp_u8(((-26 * r - 87 * g + 113 * b + 128) >> 8) + 128);
      if (x + 1 < width) {
        uv[x + 1] = clamp_u8(((112 * r - 102 * g - 10 * b + 128) >> 8) + 128);
      }
    }
  }
  return out;
}

bool check_case(uint32_t width, uint32_t height, uint32_t padding) {
  const uint32_t stride = width * 4 + padding;
  std::vector<uint8_t> bgra(static_cast<size_t>(stride) * height);
  uint32_t state = 0x12345678u ^ width ^ (height << 8);
  for (auto& byte : bgra) {
    state = state * 1664525u + 1013904223u;
    byte = static_cast<uint8_t>(state >> 24);
  }
  const std::vector<uint8_t> expected = scalar_reference(bgra.data(), width, height, stride);
  std::vector<uint8_t> actual(expected.size(), 0xcd);
  if (!remote60::native_poc::bgra_to_nv12_buffer(bgra.data(), width, height, stride,
                                                  actual.data(), actual.size()) ||
      actual != expected) {
    std::cerr << "buffer conversion mismatch size=" << width << "x" << height << "\n";
    return false;
  }
  std::vector<uint8_t> wrapped;
  if (!remote60::native_poc::bgra_to_nv12(bgra.data(), width, height, stride, &wrapped) ||
      wrapped != expected) {
    std::cerr << "vector conversion mismatch size=" << width << "x" << height << "\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  const struct {
    uint32_t width;
    uint32_t height;
    uint32_t padding;
  } cases[] = {{1, 1, 0}, {2, 2, 8}, {3, 5, 4}, {4, 4, 0}, {7, 6, 12},
               {16, 9, 0}, {1919, 1079, 16}, {1920, 1080, 0}};
  for (const auto& item : cases) {
    if (!check_case(item.width, item.height, item.padding)) return 1;
  }
  std::cout << "mf_h264_codec_test: PASS\n";
  return 0;
}
