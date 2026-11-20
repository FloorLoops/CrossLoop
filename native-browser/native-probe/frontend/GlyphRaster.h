#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace native_glyph {
// NetSurf size is 22:10 points; built-in font bitmaps were made at 150 DPI.
inline int scale(int32_t points1024, int nativePoints) {
  if (points1024 <= 0 || nativePoints <= 0) return 0;
  const int64_t denominator = int64_t{1024} * nativePoints * 150;
  const int64_t value = (int64_t{points1024} * 96 * 256 + denominator / 2) / denominator;
  return value >= 16 && value <= 1024 ? static_cast<int>(value) : 0;
}
inline int advance(int32_t fp4, int scale256) {
  if (fp4 < 0 || scale256 < 16 || scale256 > 1024) return -1;
  const int64_t value = ((int64_t{fp4} * scale256 / 256) + 8) / 16;
  return value <= std::numeric_limits<int>::max() ? static_cast<int>(value) : -1;
}
template <typename Pixel>
void draw(const uint8_t* bitmap, bool twoBit, int width, int height, int left, int top, int scale256, int x0, int y0,
          int x1, int y1, bool black, Pixel pixel) {
  if (!bitmap || width < 1 || height < 1 || width > 255 || height > 255 || scale256 < 16 || scale256 > 1024 ||
      x1 <= x0 || y1 <= y0)
    return;
  const int outWidth = (width * scale256 + 255) / 256;
  const int outHeight = (height * scale256 + 255) / 256;
  const int64_t clipLeft = std::max<int64_t>(left, x0), clipTop = std::max<int64_t>(top, y0);
  const int64_t clipRight = std::min<int64_t>(int64_t{left} + outWidth, x1);
  const int64_t clipBottom = std::min<int64_t>(int64_t{top} + outHeight, y1);
  const int step = (256 << 16) / scale256;
  const int origin = step / 2 - (1 << 15);
  const auto sample = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    const int pos = y * width + x;
    return twoBit ? (bitmap[pos >> 2] >> ((3 - (pos & 3)) * 2)) & 3 : ((bitmap[pos >> 3] >> (7 - (pos & 7))) & 1) * 3;
  };
  for (int64_t y = clipTop; y < clipBottom; ++y) {
    const int sy = origin + static_cast<int>(y - top) * step;
    const int by = sy >> 16, fy = (sy >> 8) & 255;
    for (int64_t x = clipLeft; x < clipRight; ++x) {
      const int sx = origin + static_cast<int>(x - left) * step;
      const int bx = sx >> 16, fx = (sx >> 8) & 255;
      const int upper = sample(bx, by) * (256 - fx) + sample(bx + 1, by) * fx;
      const int lower = sample(bx, by + 1) * (256 - fx) + sample(bx + 1, by + 1) * fx;
      if ((upper * (256 - fy) + lower * fy) * 2 >= 3 * 256 * 256)
        pixel(static_cast<int>(x), static_cast<int>(y), black);
    }
  }
}
}  // namespace native_glyph
