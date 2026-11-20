/* Bounded RGBA8 NetSurf bitmap storage for the offline X4 Pro probe. GPL-2.0. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "netsurf/bitmap.h"

enum { BITMAP_BRIDGE_MAX_DIMENSION = 1024, BITMAP_BRIDGE_MAX_PAYLOAD = 512 * 1024 };

/* Layout is byte-wise R,G,B,A with straight (not premultiplied) alpha.
 * The browser owner must call bitmap_set_format(&bitmap_bridge_format). */
extern const bitmap_fmt_t bitmap_bridge_format;
extern struct gui_bitmap_table bitmap_bridge_table;

/* Borrowed read-only pixel view for a future plotter. Invalid after destroy;
 * no ownership transfer. Callers must not modify pixels through this view. */
typedef struct bitmap_bridge_view {
  const uint8_t* rgba;
  size_t rowstride;
  int width;
  int height;
  bool opaque;
  uint32_t revision;
} bitmap_bridge_view;

bool bitmap_bridge_describe(const void* bitmap, bitmap_bridge_view* out);

#ifdef __cplusplus
}
#endif
