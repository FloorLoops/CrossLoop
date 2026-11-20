#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/errors.h"
#ifdef __cplusplus
extern "C" {
#endif
struct gui_layout_table;
struct plotter_table;

enum probe_browser_state {
  PROBE_BROWSER_COLD,
  PROBE_BROWSER_IDLE,
  PROBE_BROWSER_LOADING,
  PROBE_BROWSER_READY,
  PROBE_BROWSER_FAILED,
  PROBE_BROWSER_INIT_FAILED,
  PROBE_BROWSER_STOPPED
};
struct probe_browser_snapshot {
  enum probe_browser_state state;
  nserror error;
  bool dirty;
  int width, height, scroll_y;
  unsigned redraws;
  char title[96];
  char status[96];
};

/* Serialize every call and callback with the firmware RenderLock. Initialize
 * EngineMemory first. No callback may run on an independent timer/task.
 * Tables and the renderer they borrow must outlive final shutdown.
 * Call on a stack sized for NetSurf: css_select_style alone uses 7392 bytes
 * on Xtensa. The caller must measure the complete call-chain high-water mark. */
nserror probe_browser_init(int width, int height, struct gui_layout_table* layout, const struct plotter_table* plot,
                           uint32_t now_ms);
nserror probe_browser_open_fixture(uint32_t now_ms);
nserror probe_browser_open_url(const char* url, uint32_t now_ms);
nserror probe_browser_navigate(const char* url, uint32_t now_ms);
nserror probe_browser_reload(void);
nserror probe_browser_stop(void);
nserror probe_browser_copy_url(char* buffer, size_t capacity);
nserror probe_browser_click(int x, int y);
nserror probe_browser_back(void);
bool probe_browser_can_back(void);
nserror probe_browser_forward(void);
bool probe_browser_can_forward(void);
nserror probe_browser_zoom(unsigned percent);
unsigned probe_browser_zoom_percent(void);
bool probe_browser_can_type(void);
nserror probe_browser_replace_text(const char* text);
nserror probe_browser_submit_field(void);
nserror probe_browser_resize(int width, int height);
nserror probe_browser_pump(uint32_t now_ms);
nserror probe_browser_draw(void);
nserror probe_browser_scroll(int delta_y);
void probe_browser_close(void);
/* Final process/device lifetime only; initialization after shutdown is rejected. */
nserror probe_browser_shutdown(void);
struct probe_browser_snapshot probe_browser_snapshot(void);
#ifdef __cplusplus
}
#endif
