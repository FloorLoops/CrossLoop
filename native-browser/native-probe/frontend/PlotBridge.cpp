/* Offline X4 Pro NetSurf plot bridge. GPL-2.0. */
#include "PlotBridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "FontBridge.h"
#include "GfxRenderer.h"
#include "PolygonRaster.h"

namespace {
GfxRenderer* renderer = nullptr;
std::array<int, 4> previous_clip{};  // x, y, width, height
rect clip_area{};                    // exclusive x1/y1

bool valid(const rect& r) { return r.x0 < r.x1 && r.y0 < r.y1; }

rect intersection(const rect& a, const rect& b) {
  return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

rect screen_rect() {
  if (renderer == nullptr) return {};
  return {0, 0, renderer->getScreenWidth(), renderer->getScreenHeight()};
}

void apply_clip(const rect& r) {
  clip_area = intersection(r, screen_rect());
  if (!valid(clip_area)) clip_area = {0, 0, 0, 0};
  renderer->setClipRect(clip_area.x0, clip_area.y0, clip_area.x1 - clip_area.x0, clip_area.y1 - clip_area.y0);
}

bool black_for(colour c) { return colour_lightness(c) <= 127; }
bool transparent(colour c) { return c == NS_TRANSPARENT; }

int stroke_pixels(plot_style_fixed fixed) {
  if (fixed <= 0) return 1;
  const int64_t rounded = (static_cast<int64_t>(fixed) + PLOT_STYLE_SCALE - 1) / PLOT_STYLE_SCALE;
  return rounded > 1024 ? -1 : static_cast<int>(rounded);
}

bool known_operation(plot_operation_type_t type) { return type == PLOT_OP_TYPE_NONE || type == PLOT_OP_TYPE_SOLID; }

nserror plot_clip(const redraw_context*, const rect* r) {
  if (renderer == nullptr || r == nullptr) return NSERROR_BAD_PARAMETER;
  apply_clip(*r);
  return NSERROR_OK;
}

nserror plot_arc(const redraw_context*, const plot_style_t*, int, int, int, int, int) {
  return NSERROR_NOT_IMPLEMENTED;
}
nserror plot_disc(const redraw_context*, const plot_style_t*, int, int, int) { return NSERROR_NOT_IMPLEMENTED; }

nserror plot_line(const redraw_context*, const plot_style_t* style, const rect* line) {
  if (renderer == nullptr || style == nullptr || line == nullptr) return NSERROR_BAD_PARAMETER;
  if (!known_operation(style->stroke_type)) return NSERROR_NOT_IMPLEMENTED;
  if (style->stroke_type == PLOT_OP_TYPE_NONE || transparent(style->stroke_colour) || !valid(clip_area))
    return NSERROR_OK;
  const int width = stroke_pixels(style->stroke_width);
  if (width < 1) return NSERROR_NOT_IMPLEMENTED;
  const double offset = (width % 2 == 0) ? -0.5 : 0.0;
  const double ax = static_cast<double>(line->x0) + offset;
  const double ay = static_cast<double>(line->y0) + offset;
  const double bx = static_cast<double>(line->x1) + offset;
  const double by = static_cast<double>(line->y1) + offset;
  const double radius = width / 2.0;
  const int left =
      static_cast<int>(std::clamp<double>(std::floor(std::min(ax, bx) - radius), clip_area.x0, clip_area.x1));
  const int right =
      static_cast<int>(std::clamp<double>(std::ceil(std::max(ax, bx) + radius + 1), clip_area.x0, clip_area.x1));
  const int top =
      static_cast<int>(std::clamp<double>(std::floor(std::min(ay, by) - radius), clip_area.y0, clip_area.y1));
  const int bottom =
      static_cast<int>(std::clamp<double>(std::ceil(std::max(ay, by) + radius + 1), clip_area.y0, clip_area.y1));
  const double dx = bx - ax, dy = by - ay, length_sq = dx * dx + dy * dy;
  const bool black = black_for(style->stroke_colour);
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      const double projection =
          length_sq == 0.0 ? 0.0 : std::clamp(((x - ax) * dx + (y - ay) * dy) / length_sq, 0.0, 1.0);
      const double ex = ax + projection * dx, ey = ay + projection * dy;
      const double xx = x - ex, yy = y - ey;
      if (xx * xx + yy * yy < radius * radius) renderer->drawPixel(x, y, black);
    }
  }
  return NSERROR_OK;
}

nserror plot_rectangle(const redraw_context*, const plot_style_t* style, const rect* r) {
  if (renderer == nullptr || style == nullptr || r == nullptr) return NSERROR_BAD_PARAMETER;
  if (!known_operation(style->fill_type) || !known_operation(style->stroke_type)) return NSERROR_NOT_IMPLEMENTED;
  if (!valid(*r) || !valid(clip_area)) return NSERROR_OK;
  const bool fill = style->fill_type == PLOT_OP_TYPE_SOLID && !transparent(style->fill_colour);
  const bool stroke = style->stroke_type == PLOT_OP_TYPE_SOLID && !transparent(style->stroke_colour);
  const int width = stroke_pixels(style->stroke_width);
  if (stroke && width < 1) return NSERROR_NOT_IMPLEMENTED;
  if (fill) {
    const rect area = intersection(*r, clip_area);
    if (valid(area))
      renderer->fillRect(area.x0, area.y0, area.x1 - area.x0, area.y1 - area.y0, black_for(style->fill_colour));
  }
  if (stroke) {
    const int64_t x0 = r->x0, y0 = r->y0, x1 = r->x1, y1 = r->y1;
    const int64_t w = width;
    const rect edges[4] = {{r->x0, r->y0, r->x1, static_cast<int>(std::min<int64_t>(y1, y0 + w))},
                           {r->x0, static_cast<int>(std::max<int64_t>(y0, y1 - w)), r->x1, r->y1},
                           {r->x0, r->y0, static_cast<int>(std::min<int64_t>(x1, x0 + w)), r->y1},
                           {static_cast<int>(std::max<int64_t>(x0, x1 - w)), r->y0, r->x1, r->y1}};
    for (const rect& edge : edges) {
      const rect area = intersection(edge, clip_area);
      if (valid(area))
        renderer->fillRect(area.x0, area.y0, area.x1 - area.x0, area.y1 - area.y0, black_for(style->stroke_colour));
    }
  }
  return NSERROR_OK;
}

/* Bounded, allocation-free non-zero-winding fill. Polygon vertex counts beyond
 * the bound fail explicitly before drawing rather than silently losing ink. */
nserror plot_polygon(const redraw_context*, const plot_style_t* style, const int* points, unsigned int n) {
  if (renderer == nullptr || style == nullptr || (n != 0 && points == nullptr)) return NSERROR_BAD_PARAMETER;
  if (!known_operation(style->fill_type)) return NSERROR_NOT_IMPLEMENTED;
  if (style->fill_type == PLOT_OP_TYPE_NONE || transparent(style->fill_colour) || n < 3 || !valid(clip_area))
    return NSERROR_OK;
  if (n > plot_bridge::MAX_POLYGON_VERTICES) return NSERROR_NOT_IMPLEMENTED;
  const bool black = black_for(style->fill_colour);
  plot_bridge::polygonSpans(points, n, clip_area.x0, clip_area.y0, clip_area.x1, clip_area.y1,
                            [black](int x, int y, int width) { renderer->fillRect(x, y, width, 1, black); });
  return NSERROR_OK;
}

nserror plot_path(const redraw_context*, const plot_style_t*, const float*, unsigned int, const float[6]) {
  return NSERROR_NOT_IMPLEMENTED;
}
nserror plot_bitmap(const redraw_context*, bitmap*, int, int, int, int, colour, bitmap_flags_t) {
  return NSERROR_NOT_IMPLEMENTED;
}
nserror plot_text(const redraw_context*, const plot_font_style_t* style, int x, int y, const char* text,
                  size_t length) {
  if (renderer == nullptr || style == nullptr || (length != 0 && text == nullptr)) return NSERROR_BAD_PARAMETER;
  if (transparent(style->foreground) || length == 0 || !valid(clip_area)) return NSERROR_OK;
  return font_bridge_draw(style, x, y, text, length, clip_area.x0, clip_area.y0, clip_area.x1, clip_area.y1,
                          black_for(style->foreground));
}
}  // namespace

extern "C" nserror plot_bridge_bind(void* bound_renderer) {
  if (bound_renderer == nullptr) return NSERROR_BAD_PARAMETER;
  plot_bridge_reset();
  renderer = static_cast<GfxRenderer*>(bound_renderer);
  previous_clip = renderer->getClipRect();
  plot_bridge_reset_clip();
  return NSERROR_OK;
}

extern "C" void plot_bridge_reset(void) {
  if (renderer != nullptr) {
    renderer->setClipRect(previous_clip[0], previous_clip[1], previous_clip[2], previous_clip[3]);
  }
  renderer = nullptr;
  clip_area = {};
}

extern "C" void plot_bridge_reset_clip(void) {
  if (renderer != nullptr) apply_clip(screen_rect());
}

extern "C" const plotter_table plot_bridge_table = {
    .clip = plot_clip,
    .arc = plot_arc,
    .disc = plot_disc,
    .line = plot_line,
    .rectangle = plot_rectangle,
    .polygon = plot_polygon,
    .path = plot_path,
    .bitmap = plot_bitmap,
    .text = plot_text,
    .group_start = nullptr,
    .group_end = nullptr,
    .flush = nullptr,
    .option_knockout = false,
};
