/* Offline X4 Pro NetSurf font probe. GPL-2.0. No browser registration here. */
#include "FontBridge.h"

#include <limits.h>
#include <stdint.h>

#include <initializer_list>

#include "EpdFontData.h"
#include "FontCacheManager.h"
#include "FontDecompressor.h"
#include "GfxRenderer.h"
#include "GlyphRaster.h"

extern "C" {
#include "netsurf/plot_style.h"
}

namespace {
GfxRenderer* renderer = nullptr;
int body_id = 0, heading_id = 0;
int body_points = 0, heading_points = 0;

/* Reject malformed sequences rather than measuring a byte as a glyph. */
bool next_utf8(const char* s, size_t len, size_t& at, uint32_t& cp) {
  if (at >= len) return false;
  const auto* u = reinterpret_cast<const uint8_t*>(s);
  uint8_t c = u[at];
  uint32_t v;
  size_t n;
  if (c < 0x80) {
    cp = c;
    ++at;
    return true;
  }
  if (c >= 0xc2 && c <= 0xdf) {
    v = c & 0x1f;
    n = 2;
  } else if (c >= 0xe0 && c <= 0xef) {
    v = c & 0x0f;
    n = 3;
  } else if (c >= 0xf0 && c <= 0xf4) {
    v = c & 0x07;
    n = 4;
  } else
    return false;
  if (n > len - at) return false;
  for (size_t i = 1; i < n; ++i) {
    if ((u[at + i] & 0xc0) != 0x80) return false;
    v = (v << 6) | (u[at + i] & 0x3f);
  }
  if ((n == 3 && v < 0x800) || (n == 4 && v < 0x10000) || (v >= 0xd800 && v <= 0xdfff) || v > 0x10ffff) return false;
  cp = v;
  at += n;
  return true;
}

nserror select_font(const plot_font_style* style, int& id, EpdFontFamily::Style& variant, int& scale) {
  if (!renderer || !style || style->size <= 0) return NSERROR_BAD_PARAMETER;
  const int64_t pt_fixed = style->size;
  const int64_t midpoint = (static_cast<int64_t>(body_points) + heading_points) * PLOT_STYLE_SCALE * 150 / 2;
  id = pt_fixed * 96 >= midpoint ? heading_id : body_id;
  scale = native_glyph::scale(style->size, id == heading_id ? heading_points : body_points);
  if (!scale) return NSERROR_BAD_SIZE;
  variant = style->weight >= 700 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  return renderer->getFontMap().count(id) ? NSERROR_OK : NSERROR_NOT_FOUND;
}

nserror glyph_advance(int id, EpdFontFamily::Style variant, uint32_t cp, int scale, int& px) {
  if (cp < 0x20 || cp == 0x7f) return NSERROR_NOT_IMPLEMENTED;
  int32_t fp;
  int top;
  if (!renderer->getCodepointMetrics(id, cp, variant, fp, top)) return NSERROR_NOT_FOUND;
  px = native_glyph::advance(fp, scale);
  if (px < 0) return NSERROR_BAD_SIZE;
  return NSERROR_OK;
}

nserror drawable_glyph(int id, EpdFontFamily::Style variant, uint32_t cp) {
  const auto font_it = renderer->getFontMap().find(id);
  if (font_it == renderer->getFontMap().end()) return NSERROR_NOT_FOUND;
  const EpdFontFamily& family = font_it->second;
  const EpdFontData* data = family.getData(variant);
  const EpdGlyph* glyph = family.getGlyph(cp, variant);
  if (!data || !glyph) return NSERROR_NOT_FOUND;
  if (glyph->width == 0 || glyph->height == 0) return NSERROR_OK;
  if (data->groups) {
    auto* manager = renderer->getFontCacheManager();
    auto* decompressor = manager ? manager->getDecompressor() : nullptr;
    if (!decompressor || !data->glyph || glyph < data->glyph) return NSERROR_INIT_FAILED;
    const size_t index = static_cast<size_t>(glyph - data->glyph);
    if (index > UINT32_MAX) return NSERROR_BAD_SIZE;
    return decompressor->getBitmap(data, glyph, static_cast<uint32_t>(index)) ? NSERROR_OK : NSERROR_NOMEM;
  }
  return data->bitmap ? NSERROR_OK : NSERROR_INIT_FAILED;
}

bool glyph_position_fits(int id, EpdFontFamily::Style variant, uint32_t cp, int64_t x, int baseline_y, int scale) {
  const auto it = renderer->getFontMap().find(id);
  if (it == renderer->getFontMap().end()) return false;
  const EpdGlyph* glyph = it->second.getGlyph(cp, variant);
  if (!glyph) return false;
  const int64_t left = x + int64_t{glyph->left} * scale / 256;
  const int64_t top = static_cast<int64_t>(baseline_y) - int64_t{glyph->top} * scale / 256;
  return left >= INT_MIN && left + (glyph->width * scale + 255) / 256 <= INT_MAX && top >= INT_MIN &&
         top + (glyph->height * scale + 255) / 256 <= INT_MAX;
}

nserror walk_width(const plot_font_style* style, const char* s, size_t len, int& width) {
  int id, dx, scale;
  EpdFontFamily::Style variant;
  nserror err = select_font(style, id, variant, scale);
  if (err != NSERROR_OK || (!s && len != 0)) return err == NSERROR_OK ? NSERROR_BAD_PARAMETER : err;
  size_t at = 0;
  uint32_t cp;
  int sum = 0;
  while (at < len) {
    if (!next_utf8(s, len, at, cp)) return NSERROR_BAD_ENCODING;
    err = glyph_advance(id, variant, cp, scale, dx);
    if (err != NSERROR_OK) return err;
    if (sum > INT_MAX - dx) return NSERROR_BAD_SIZE;
    sum += dx;
  }
  width = sum;
  return NSERROR_OK;
}

nserror width_cb(const plot_font_style* style, const char* s, size_t len, int* out) {
  if (!out) return NSERROR_BAD_PARAMETER;
  return walk_width(style, s, len, *out);
}

nserror position_cb(const plot_font_style* style, const char* s, size_t len, int x, size_t* offset, int* actual) {
  if (!offset || !actual || (!s && len != 0)) return NSERROR_BAD_PARAMETER;
  int id, dx, scale;
  EpdFontFamily::Style variant;
  nserror err = select_font(style, id, variant, scale);
  if (err != NSERROR_OK) return err;
  size_t at = 0;
  uint32_t cp;
  int sum = 0;
  while (at < len) {
    const size_t start = at;
    if (!next_utf8(s, len, at, cp)) return NSERROR_BAD_ENCODING;
    err = glyph_advance(id, variant, cp, scale, dx);
    if (err != NSERROR_OK) return err;
    if (sum > INT_MAX - dx) return NSERROR_BAD_SIZE;
    if (x < sum + dx / 2) {
      *offset = start;
      *actual = sum;
      return NSERROR_OK;
    }
    sum += dx;
  }
  *offset = len;
  *actual = sum;
  return NSERROR_OK;
}

nserror split_cb(const plot_font_style* style, const char* s, size_t len, int x, size_t* offset, int* actual) {
  if (!offset || !actual || (!s && len != 0)) return NSERROR_BAD_PARAMETER;
  int id, dx, scale;
  EpdFontFamily::Style variant;
  nserror err = select_font(style, id, variant, scale);
  if (err != NSERROR_OK) return err;
  size_t at = 0, last_space = 0;
  uint32_t cp;
  int sum = 0, last_space_x = 0;
  while (at < len) {
    const size_t start = at;
    if (!next_utf8(s, len, at, cp)) return NSERROR_BAD_ENCODING;
    if (cp == ' ' && start > 0) {
      last_space = start;
      last_space_x = sum;
    }
    err = glyph_advance(id, variant, cp, scale, dx);
    if (err != NSERROR_OK) return err;
    if (sum > INT_MAX - dx) return NSERROR_BAD_SIZE;
    if (sum + dx > x) {
      if (last_space) {
        *offset = last_space;
        *actual = last_space_x;
        return NSERROR_OK;
      }
      sum += dx;
      while (at < len) {
        const size_t next_start = at;
        if (!next_utf8(s, len, at, cp)) return NSERROR_BAD_ENCODING;
        if (cp == ' ') {
          *offset = next_start;
          *actual = sum;
          return NSERROR_OK;
        }
        err = glyph_advance(id, variant, cp, scale, dx);
        if (err != NSERROR_OK) return err;
        if (sum > INT_MAX - dx) return NSERROR_BAD_SIZE;
        sum += dx;
      }
      *offset = len;
      *actual = sum;
      return NSERROR_OK;
    }
    sum += dx;
  }
  *offset = len;
  *actual = sum;
  return NSERROR_OK;
}
}  // namespace

extern "C" {
struct gui_layout_table font_bridge_layout = {
    .width = width_cb,
    .position = position_cb,
    .split = split_cb,
};

nserror font_bridge_begin(void* ptr, int body, int heading, int body_pt, int heading_pt) {
  font_bridge_close();
  if (!ptr || !body || !heading || body_pt <= 0 || heading_pt <= body_pt) return NSERROR_BAD_PARAMETER;
  auto* r = static_cast<GfxRenderer*>(ptr);
  if (!r->ensureFontLoaded(body) || !r->ensureFontLoaded(heading)) return NSERROR_NOT_FOUND;
  for (int id : {body, heading}) {
    const auto it = r->getFontMap().find(id);
    if (it == r->getFontMap().end()) return NSERROR_NOT_FOUND;
    for (auto variant : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
      const EpdFontData* data = it->second.getData(variant);
      if (!data || !data->glyph || !data->bitmap || data->glyphMissCtx || data->vectorBitmapHandler)
        return NSERROR_NOT_IMPLEMENTED;
      if (data->groups && (!r->getFontCacheManager() || !r->getFontCacheManager()->getDecompressor()))
        return NSERROR_INIT_FAILED;
    }
  }
  renderer = r;
  body_id = body;
  heading_id = heading;
  body_points = body_pt;
  heading_points = heading_pt;
  return NSERROR_OK;
}

void font_bridge_close(void) {
  renderer = nullptr;
  body_id = heading_id = body_points = heading_points = 0;
}

nserror font_bridge_draw(const plot_font_style* style, int x, int baseline_y, const char* s, size_t len, int x0, int y0,
                         int x1, int y1, bool black) {
  if (!renderer || (!s && len != 0) || x1 < x0 || y1 < y0) return NSERROR_BAD_PARAMETER;
  if (renderer->isFontCacheScanning()) return NSERROR_INVALID;
  int id, dx, scale, sum = 0;
  EpdFontFamily::Style variant;
  nserror err = select_font(style, id, variant, scale);
  if (err != NSERROR_OK) return err;
  /* Validate the full span before changing any pixels. */
  size_t at = 0;
  uint32_t cp;
  while (at < len) {
    if (!next_utf8(s, len, at, cp)) return NSERROR_BAD_ENCODING;
    err = glyph_advance(id, variant, cp, scale, dx);
    if (err != NSERROR_OK) return err;
    err = drawable_glyph(id, variant, cp);
    if (err != NSERROR_OK) return err;
    if (!glyph_position_fits(id, variant, cp, static_cast<int64_t>(x) + sum, baseline_y, scale))
      return NSERROR_BAD_SIZE;
    if (sum > INT_MAX - dx) return NSERROR_BAD_SIZE;
    sum += dx;
  }
  if (x > INT_MAX - sum) return NSERROR_BAD_SIZE;
  const auto old = renderer->getClipRect();
  const int64_t old_right = static_cast<int64_t>(old[0]) + old[2];
  const int64_t old_bottom = static_cast<int64_t>(old[1]) + old[3];
  const int left = old[0] > x0 ? old[0] : x0;
  const int top = old[1] > y0 ? old[1] : y0;
  const int right = old_right < x1 ? static_cast<int>(old_right) : x1;
  const int bottom = old_bottom < y1 ? static_cast<int>(old_bottom) : y1;
  if (right <= left || bottom <= top) return NSERROR_OK;
  renderer->setClipRect(left, top, right - left, bottom - top);
  at = 0;
  int cursor = x;
  while (at < len) {
    next_utf8(s, len, at, cp);  // prevalidated above
    glyph_advance(id, variant, cp, scale, dx);
    err = drawable_glyph(id, variant, cp);
    if (err != NSERROR_OK) {
      renderer->setClipRect(old[0], old[1], old[2], old[3]);
      return err;
    }
    const auto& family = renderer->getFontMap().find(id)->second;
    const auto* data = family.getData(variant);
    const auto* glyph = family.getGlyph(cp, variant);
    if (glyph->width && glyph->height) {
      const auto* bitmap = renderer->getGlyphBitmap(data, glyph);
      if (!bitmap) {
        renderer->setClipRect(old[0], old[1], old[2], old[3]);
        return NSERROR_NOMEM;
      }
      // Consume the decompressor's borrowed bitmap before any other lookup.
      native_glyph::draw(bitmap, data->is2Bit, glyph->width, glyph->height, cursor + glyph->left * scale / 256,
                         baseline_y - glyph->top * scale / 256, scale, left, top, right, bottom, black,
                         [](int px, int py, bool ink) { renderer->drawPixel(px, py, ink); });
    }
    cursor += dx;
  }
  renderer->setClipRect(old[0], old[1], old[2], old[3]);
  return NSERROR_OK;
}
}  // extern "C"
