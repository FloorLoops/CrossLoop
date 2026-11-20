/* Offline X4 Pro NetSurf plot bridge. GPL-2.0. */
#pragma once

#include "utils/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "netsurf/plotters.h"

/* renderer is a live GfxRenderer*. No ownership transfer. Call reset before
 * destroying it; the bridge restores the renderer's previous logical clip. */
nserror plot_bridge_bind(void* renderer);
void plot_bridge_reset(void);
void plot_bridge_reset_clip(void);

extern const struct plotter_table plot_bridge_table;

#ifdef __cplusplus
}
#endif
