#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace plot_bridge {
constexpr unsigned MAX_POLYGON_VERTICES = 8;
// Non-zero winding fill. Work scales with clipped rows and edges, not pixels
// times edges. Integer pixel centers match the renderer's existing coverage.
template <typename FillSpan>
void polygonSpans(const int* points, unsigned count, int clipLeft, int clipTop, int clipRight, int clipBottom,
                  FillSpan fill) {
  if (!points || count < 3 || count > MAX_POLYGON_VERTICES || clipLeft >= clipRight || clipTop >= clipBottom) return;
  int minY = points[1], maxY = points[1];
  for (unsigned i = 1; i < count; ++i) {
    minY = std::min(minY, points[2 * i + 1]);
    maxY = std::max(maxY, points[2 * i + 1]);
  }
  struct Crossing {
    double x;
    int winding;
  };
  std::array<Crossing, MAX_POLYGON_VERTICES> crossings;
  const int top = std::max(minY, clipTop), bottom = std::min(maxY, clipBottom);
  for (int y = top; y < bottom; ++y) {
    const double py = static_cast<double>(y) + 0.5;
    unsigned used = 0;
    for (unsigned i = 0; i < count; ++i) {
      const unsigned j = i + 1 == count ? 0 : i + 1;
      const double ax = points[2 * i], ay = points[2 * i + 1];
      const double bx = points[2 * j], by = points[2 * j + 1];
      if ((ay <= py && by > py) || (ay > py && by <= py))
        crossings[used++] = {ax + (py - ay) * (bx - ax) / (by - ay), by > ay ? 1 : -1};
    }
    std::sort(crossings.begin(), crossings.begin() + used,
              [](const Crossing& a, const Crossing& b) { return a.x < b.x; });
    int winding = 0;
    double start = 0;
    for (unsigned i = 0; i < used; ++i) {
      const int previous = winding;
      winding += crossings[i].winding;
      if (previous == 0 && winding != 0) start = crossings[i].x;
      if (previous != 0 && winding == 0) {
        const int left = static_cast<int>(
            std::clamp(std::ceil(start - 0.5), static_cast<double>(clipLeft), static_cast<double>(clipRight)));
        const int right = static_cast<int>(
            std::clamp(std::ceil(crossings[i].x - 0.5), static_cast<double>(clipLeft), static_cast<double>(clipRight)));
        if (left < right) fill(left, y, right - left);
      }
    }
  }
}
}  // namespace plot_bridge
