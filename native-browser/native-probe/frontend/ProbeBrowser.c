/* Offline native frontend integration fixture. GPL-2.0. */
#include "ProbeBrowser.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "BitmapBridge.h"
#include "EngineMemory.h"
#include "HttpTransport.h"
#include "ProbeResources.h"
#include "ProbeScheduler.h"
#include "content/fetch.h"
#include "content/hlcache.h"
#include "desktop/browser_history.h"
#include "netsurf/browser_window.h"
#include "netsurf/fetch.h"
#include "netsurf/keypress.h"
#include "netsurf/misc.h"
#include "netsurf/netsurf.h"
#include "netsurf/plotters.h"
#include "netsurf/window.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"

struct gui_window {
  struct browser_window* browser;
};
static struct gui_window window;
static struct probe_browser_snapshot state;
static const struct plotter_table* plotters;
static bool initialized;
static bool saw_start;
static bool saw_content;
static bool fixture_expected;
static nserror schedule_error;

static nserror schedule_callback(int delay, void (*callback)(void*), void* context) {
  nserror result = probe_scheduler_schedule(delay, callback, context);
  if (result != NSERROR_OK && !(delay < 0 && result == NSERROR_NOT_FOUND)) schedule_error = result;
  return result;
}
static struct gui_window* create_window(struct browser_window* browser, struct gui_window* existing,
                                        gui_window_create_flags flags) {
  (void)flags;
  if (window.browser || existing) return NULL;
  window.browser = browser;
  return &window;
}
static void destroy_window(struct gui_window* gw) {
  if (gw == &window) window.browser = NULL;
}
static nserror invalidate(struct gui_window* gw, const struct rect* area) {
  (void)area;
  if (gw != &window) return NSERROR_BAD_PARAMETER;
  state.dirty = true;
  return NSERROR_OK;
}
static bool get_scroll(struct gui_window* gw, int* x, int* y) {
  if (gw != &window || !x || !y) return false;
  *x = 0;
  *y = state.scroll_y;
  return true;
}
static nserror set_scroll(struct gui_window* gw, const struct rect* area) {
  if (gw != &window || !area) return NSERROR_BAD_PARAMETER;
  state.scroll_y = area->y0 < 0 ? 0 : area->y0;
  state.dirty = true;
  return NSERROR_OK;
}
static nserror dimensions(struct gui_window* gw, int* width, int* height) {
  if (gw != &window || !width || !height) return NSERROR_BAD_PARAMETER;
  *width = state.width;
  *height = state.height;
  return NSERROR_OK;
}
static nserror event(struct gui_window* gw, enum gui_window_event value) {
  if (gw != &window) return NSERROR_BAD_PARAMETER;
  switch (value) {
    case GW_EVENT_START_THROBBER:
      saw_start = true;
      saw_content = false;
      state.state = PROBE_BROWSER_LOADING;
      break;
    case GW_EVENT_NEW_CONTENT:
      saw_content = true;
      state.dirty = true;
      break;
    case GW_EVENT_STOP_THROBBER:
      if (saw_start && saw_content) {
        if (fixture_expected && strcmp(state.title, "X4 native browser test") != 0) {
          state.state = PROBE_BROWSER_FAILED;
          state.error = NSERROR_INVALID;
        } else {
          state.state = PROBE_BROWSER_READY;
          state.dirty = true;
        }
      }
      break;
    case GW_EVENT_UPDATE_EXTENT:
    case GW_EVENT_PAGE_INFO_CHANGE:
    case GW_EVENT_REMOVE_CARET:
      break;
    default:
      return NSERROR_NOT_IMPLEMENTED;
  }
  return NSERROR_OK;
}
static void set_title(struct gui_window* gw, const char* title) {
  if (gw == &window) snprintf(state.title, sizeof(state.title), "%s", title ? title : "");
}
static void set_status(struct gui_window* gw, const char* text) {
  if (gw == &window) snprintf(state.status, sizeof(state.status), "%s", text ? text : "");
}
static const char* filetype(const char* path) {
  const char* extension = path ? strrchr(path, '.') : NULL;
  if (extension && !strcmp(extension, ".css")) return "text/css";
  if (extension && !strcmp(extension, ".html")) return "text/html";
  return "application/octet-stream";
}
static struct gui_misc_table misc_table = {.schedule = schedule_callback};
static struct gui_window_table window_table = {.create = create_window,
                                               .destroy = destroy_window,
                                               .invalidate = invalidate,
                                               .get_scroll = get_scroll,
                                               .set_scroll = set_scroll,
                                               .get_dimensions = dimensions,
                                               .event = event,
                                               .set_title = set_title,
                                               .set_status = set_status};
static struct gui_fetch_table fetch_table = {
    .filetype = filetype, .get_resource_data = probe_resource_get, .release_resource_data = probe_resource_release};
static struct netsurf_table tables = {
    .misc = &misc_table, .window = &window_table, .fetch = &fetch_table, .bitmap = &bitmap_bridge_table};

nserror probe_browser_init(int width, int height, struct gui_layout_table* layout, const struct plotter_table* plot,
                           uint32_t now_ms) {
  if (state.state != PROBE_BROWSER_COLD) return NSERROR_INIT_FAILED;
  if (!layout || !plot || width < 1 || width > 1024 || height < 1 || height > 1024 || !engine_memory_snapshot().active)
    return NSERROR_BAD_PARAMETER;
  state.width = width;
  state.height = height;
  tables.layout = layout;
  plotters = plot;
  probe_scheduler_begin(now_ms);
  nserror result = netsurf_register(&tables);
  if (result == NSERROR_OK) result = nsoption_init(NULL, NULL, NULL);
  if (result == NSERROR_OK) {
    nsoption_set_int(memory_cache_size, 2 * 1024 * 1024);
    nsoption_set_uint(disc_cache_size, 0);
    nsoption_set_bool(enable_javascript, false);
    nsoption_set_bool(block_advertisements, false);
    nsoption_set_bool(foreground_images, false);
    nsoption_set_bool(background_images, false);
    nsoption_set_bool(animate_images, false);
    nsoption_set_int(max_fetchers, 1);
    nsoption_set_int(max_fetchers_per_host, 1);
    const uint8_t* messages;
    size_t length;
    result = probe_resource_get("Messages", &messages, &length);
    if (result == NSERROR_OK) result = messages_add_from_inline(messages, length);
    if (result == NSERROR_OK) result = netsurf_init(NULL);
  }
  if (result == NSERROR_OK && schedule_error != NSERROR_OK) result = schedule_error;
  if (result != NSERROR_OK) {
    // Upstream initialization is not transactional. Do not call its full exit
    // on a partially initialized core or retry against dangling globals.
    probe_scheduler_reset();
    state.state = PROBE_BROWSER_INIT_FAILED;
    state.error = result;
    return result;
  }
  bitmap_set_format(&bitmap_bridge_format);
  initialized = true;
  state.state = PROBE_BROWSER_IDLE;
  return NSERROR_OK;
}

static const char fixture[] =
    "data:text/html;charset=utf-8,%3C!doctype%20html%3E%3Chtml%3E%3Chead%3E"
    "%3Ctitle%3EX4%20native%20browser%20test%3C/title%3E%3Cstyle%3E"
    "body{margin:18px;color:black;background:white;font-size:18px}"
    "h1{font-size:26px}p{margin:12px%200}.card{border:2px%20solid%20black;padding:12px}"
    "%3C/style%3E%3C/head%3E%3Cbody%3E%3Ch1%3ECrossLoop%20browser%3C/h1%3E"
    "%3Cdiv%20class=card%3E%3Cp%3EThis%20page%20is%20laid%20out%20on%20the%20reader."
    "%3C/p%3E%3Cp%3EHTML%20and%20CSS%20control%20the%20heading,%20border,%20spacing"
    "%20and%20line%20wrapping.%3C/p%3E%3C/div%3E%3Cp%3EAn%20offline%20development"
    "%20test.%20Live%20web%20browsing%20is%20not%20connected%20yet.%3C/p%3E%3C/body%3E%3C/html%3E";

static nserror open_url(const char* address, bool is_fixture, uint32_t now_ms) {
  if (!initialized || window.browser) return NSERROR_INVALID;
  if (!address) return NSERROR_BAD_PARAMETER;
  nserror result = probe_browser_pump(now_ms);
  if (result != NSERROR_OK) return result;
  struct nsurl* url = NULL;
  result = nsurl_create(address, &url);
  if (result != NSERROR_OK) return result;
  saw_start = saw_content = false;
  fixture_expected = is_fixture;
  state.scroll_y = 0;
  state.title[0] = state.status[0] = 0;
  state.error = NSERROR_OK;
  state.state = PROBE_BROWSER_LOADING;
  // Creating without a URL lets this owner check navigate's actual result.
  struct browser_window* browser = NULL;
  result = browser_window_create(BW_CREATE_HISTORY, NULL, NULL, NULL, &browser);
  if (result == NSERROR_OK) result = browser_window_navigate(browser, url, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
  nsurl_unref(url);
  if (result != NSERROR_OK) {
    probe_browser_close();
    state.state = PROBE_BROWSER_FAILED;
    state.error = result;
  }
  return result;
}
nserror probe_browser_open_fixture(uint32_t now_ms) { return open_url(fixture, true, now_ms); }
nserror probe_browser_open_url(const char* url, uint32_t now_ms) { return open_url(url, false, now_ms); }
nserror probe_browser_navigate(const char* text, uint32_t now_ms) {
  if (!window.browser) return probe_browser_open_url(text, now_ms);
  nserror result = probe_browser_pump(now_ms);
  if (result != NSERROR_OK) return result;
  struct nsurl* url = NULL;
  result = nsurl_create(text, &url);
  if (result != NSERROR_OK) return result;
  fixture_expected = false;
  result = browser_window_navigate(window.browser, url, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
  nsurl_unref(url);
  return result;
}
nserror probe_browser_reload(void) {
  if (!window.browser) return NSERROR_INVALID;
  return browser_window_reload(window.browser, true);
}
nserror probe_browser_stop(void) {
  if (!window.browser) return NSERROR_INVALID;
  browser_window_stop(window.browser);
  native_fetch_stop();
  if (browser_window_has_content(window.browser)) {
    state.state = PROBE_BROWSER_READY;
    state.error = NSERROR_OK;
    state.dirty = true;
    saw_start = false;
    saw_content = true;
  } else {
    probe_browser_close();
  }
  return NSERROR_OK;
}
nserror probe_browser_copy_url(char* buffer, size_t capacity) {
  if (!window.browser || !buffer || !capacity) return NSERROR_BAD_PARAMETER;
  struct nsurl* url = NULL;
  nserror result = browser_window_get_url(window.browser, true, &url);
  if (result != NSERROR_OK) return result;
  if (nsurl_length(url) >= capacity)
    result = NSERROR_BAD_SIZE;
  else
    memcpy(buffer, nsurl_access(url), nsurl_length(url) + 1);
  nsurl_unref(url);
  return result;
}
nserror probe_browser_click(int x, int y) {
  if (!window.browser || state.state != PROBE_BROWSER_READY || x < 0 || y < 0 || x >= state.width ||
      y >= state.height || y > INT_MAX - state.scroll_y)
    return NSERROR_INVALID;
  fixture_expected = false;
  browser_window_mouse_click(window.browser, BROWSER_MOUSE_PRESS_1, x, y + state.scroll_y);
  browser_window_mouse_click(window.browser, BROWSER_MOUSE_CLICK_1, x, y + state.scroll_y);
  return NSERROR_OK;
}
bool probe_browser_can_back(void) { return window.browser && browser_window_history_back_available(window.browser); }
nserror probe_browser_back(void) {
  if (!probe_browser_can_back()) return NSERROR_NOT_FOUND;
  fixture_expected = false;
  return browser_window_history_back(window.browser, false);
}
bool probe_browser_can_forward(void) {
  return window.browser && browser_window_history_forward_available(window.browser);
}
nserror probe_browser_forward(void) {
  if (!probe_browser_can_forward()) return NSERROR_NOT_FOUND;
  fixture_expected = false;
  return browser_window_history_forward(window.browser, false);
}
unsigned probe_browser_zoom_percent(void) {
  return window.browser ? (unsigned)(browser_window_get_scale(window.browser) * 100.0f + 0.5f) : 100;
}
nserror probe_browser_zoom(unsigned percent) {
  if (!window.browser || state.state != PROBE_BROWSER_READY || percent < 75 || percent > 200)
    return NSERROR_BAD_PARAMETER;
  nserror result = browser_window_set_scale(window.browser, percent / 100.0f, true);
  if (result == NSERROR_OK) result = probe_browser_scroll(0);
  state.dirty = true;
  return result;
}
bool probe_browser_can_type(void) {
  return window.browser && state.state == PROBE_BROWSER_READY &&
         (browser_window_get_editor_flags(window.browser) & BW_EDITOR_CAN_PASTE) != 0;
}
static bool next_codepoint(const unsigned char** text, uint32_t* codepoint) {
  const unsigned char first = *(*text)++;
  unsigned remaining = 0;
  uint32_t minimum = 0;
  if (first < 0x80)
    *codepoint = first;
  else if (first >= 0xc2 && first <= 0xdf) {
    remaining = 1;
    minimum = 0x80;
    *codepoint = first & 31;
  } else if (first >= 0xe0 && first <= 0xef) {
    remaining = 2;
    minimum = 0x800;
    *codepoint = first & 15;
  } else if (first >= 0xf0 && first <= 0xf4) {
    remaining = 3;
    minimum = 0x10000;
    *codepoint = first & 7;
  } else
    return false;
  while (remaining--) {
    const unsigned char next = **text;
    if (!next || (next & 0xc0) != 0x80) return false;
    ++*text;
    *codepoint = (*codepoint << 6) | (next & 63);
  }
  return *codepoint >= minimum && *codepoint >= 32 && *codepoint != 127 && *codepoint <= 0x10ffff &&
         !(*codepoint >= 0xd800 && *codepoint <= 0xdfff);
}
nserror probe_browser_replace_text(const char* text) {
  if (!text || !probe_browser_can_type()) return NSERROR_INVALID;
  if (strlen(text) > 256) return NSERROR_BAD_SIZE;
  const unsigned char* cursor = (const unsigned char*)text;
  uint32_t codepoint;
  while (*cursor)
    if (!next_codepoint(&cursor, &codepoint)) return NSERROR_BAD_PARAMETER;
  if (!browser_window_key_press(window.browser, NS_KEY_SELECT_ALL) ||
      !browser_window_key_press(window.browser, NS_KEY_DELETE_LEFT))
    return NSERROR_NOT_IMPLEMENTED;
  cursor = (const unsigned char*)text;
  while (*cursor) {
    next_codepoint(&cursor, &codepoint);
    if (!browser_window_key_press(window.browser, codepoint)) return NSERROR_NOT_IMPLEMENTED;
  }
  state.dirty = true;
  return NSERROR_OK;
}
nserror probe_browser_submit_field(void) {
  if (!probe_browser_can_type()) return NSERROR_INVALID;
  fixture_expected = false;
  return browser_window_key_press(window.browser, NS_KEY_CR) ? NSERROR_OK : NSERROR_NOT_IMPLEMENTED;
}
nserror probe_browser_resize(int width, int height) {
  if (!initialized || width < 1 || width > 1024 || height < 1 || height > 1024) return NSERROR_BAD_PARAMETER;
  state.width = width;
  state.height = height;
  if (window.browser) browser_window_reformat(window.browser, false, width, height);
  state.dirty = true;
  return NSERROR_OK;
}
nserror probe_browser_pump(uint32_t now_ms) {
  if (!initialized) return NSERROR_INVALID;
  probe_scheduler_pump_result result = probe_scheduler_pump(now_ms);
  nserror error = result.status == NSERROR_OK ? schedule_error : result.status;
  const nserror fetch_error = native_fetch_take_error();
  if (error == NSERROR_OK) error = fetch_error;
  if (error != NSERROR_OK) {
    state.state = PROBE_BROWSER_FAILED;
    state.error = error;
  }
  return error;
}
nserror probe_browser_draw(void) {
  if (!window.browser || state.state != PROBE_BROWSER_READY || !browser_window_redraw_ready(window.browser))
    return NSERROR_INVALID;
  const struct rect clip = {0, 0, state.width, state.height};
  const struct redraw_context context = {.interactive = true, .background_images = false, .plot = plotters};
  if (!browser_window_redraw(window.browser, 0, -state.scroll_y, &clip, &context)) {
    state.state = PROBE_BROWSER_FAILED;
    state.error = NSERROR_INVALID;
    return state.error;
  }
  ++state.redraws;
  state.dirty = false;
  return NSERROR_OK;
}
nserror probe_browser_scroll(int delta_y) {
  if (!window.browser || state.state != PROBE_BROWSER_READY) return NSERROR_INVALID;
  int64_t next = (int64_t)state.scroll_y + delta_y;
  int width, height;
  if (browser_window_get_extents(window.browser, true, &width, &height) == NSERROR_OK) {
    const int maximum = height > state.height ? height - state.height : 0;
    if (next > maximum) next = maximum;
  }
  if (next < 0) next = 0;
  if (next > INT_MAX) next = INT_MAX;
  state.scroll_y = (int)next;
  state.dirty = true;
  return NSERROR_OK;
}
void probe_browser_close(void) {
  if (!initialized) return;
  if (window.browser) browser_window_destroy(window.browser);
  native_fetch_stop();
  hlcache_release_unused();
  // Offline fetchers defer abort cleanup until their next poll. Complete that
  // cleanup before shutdown resets the scheduler and finalizes interned URLs.
  fd_set read_set, write_set, except_set;
  int maxfd;
  fetch_fdset(&read_set, &write_set, &except_set, &maxfd);
  state.state = PROBE_BROWSER_IDLE;
  state.dirty = false;
  saw_start = saw_content = false;
}
nserror probe_browser_shutdown(void) {
  if (!initialized) return NSERROR_INVALID;
  probe_browser_close();
  netsurf_exit();
  nsoption_finalise(NULL, NULL);
  probe_scheduler_reset();
  initialized = false;
  state.state = PROBE_BROWSER_STOPPED;
  return NSERROR_OK;
}
struct probe_browser_snapshot probe_browser_snapshot(void) { return state; }
