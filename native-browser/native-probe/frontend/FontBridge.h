/* Offline X4 Pro NetSurf font probe. C-callable boundary; no ownership transfer. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "utils/errors.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "netsurf/layout.h"

struct plot_font_style;

/* renderer is a live GfxRenderer*. Both IDs must already be registered. */
nserror font_bridge_begin(void* renderer, int body_id, int heading_id, int body_points, int heading_points);
void font_bridge_close(void);

extern struct gui_layout_table font_bridge_layout;

/* Caller supplies NetSurf plot text and an explicit clip in logical pixels.
 * The caller selects black or white glyph ink. */
nserror font_bridge_draw(const struct plot_font_style* style, int x, int baseline_y, const char* utf8, size_t byte_len,
                         int clip_x0, int clip_y0, int clip_x1, int clip_y1, bool black);

#ifdef __cplusplus
}
#endif
