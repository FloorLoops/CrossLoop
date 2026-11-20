/* GET-only on-reader transport adapter. No network work in browser callbacks. */
#include <string.h>

#include "EngineMemory.h"
#include "HttpTransport.h"
#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/urldb.h"
#include "utils/corestrings.h"
#include "utils/nsurl.h"

#define MAX_REQUESTS 16
#define MAX_URL 1024
#define MAX_HEADERS 8
#define MAX_HEADER 256
struct native_fetch {
  struct fetch* parent;
  struct nsurl* url;
  bool aborted, unsupported, only_2xx;
  unsigned status;
  char location[MAX_URL + 1];
  char headers[MAX_HEADERS][MAX_HEADER];
  const char* header_list[MAX_HEADERS + 1];
};
static const struct http_transport* io;
static struct native_fetch* requests[MAX_REQUESTS];
static struct native_fetch* active;
static bool polling;
static nserror resource_error;
static struct http_transport_event event_buffer;

static bool initialize(lwc_string* scheme) {
  (void)scheme;
  return true;
}
static bool acceptable(const struct nsurl* url) {
  const char* text = nsurl_access(url);
  if (nsurl_length(url) > MAX_URL) return false;
  if (strncmp(text, "https://", 8) && strncmp(text, "http://", 7)) return false;
  lwc_string* user = nsurl_get_component(url, NSURL_USERNAME);
  if (user) {
    lwc_string_unref(user);
    return false;
  }
  return true;
}
static void* setup(struct fetch* parent, struct nsurl* url, bool only_2xx, bool downgrade_tls, const char* post,
                   const struct fetch_multipart_data* multipart, const char** headers) {
  unsigned slot;
  for (slot = 0; slot < MAX_REQUESTS && requests[slot]; ++slot) {
  }
  if (slot == MAX_REQUESTS) {
    resource_error = NSERROR_NOMEM;
    return NULL;
  }
  struct native_fetch* ctx = engine_memory_calloc(1, sizeof(*ctx));
  if (!ctx) {
    resource_error = NSERROR_NOMEM;
    return NULL;
  }
  ctx->parent = parent;
  ctx->url = nsurl_ref(url);
  ctx->only_2xx = only_2xx;
  ctx->unsupported = post || multipart || downgrade_tls;
  if (headers) {
    unsigned index;
    for (index = 0; headers[index] && index < MAX_HEADERS; ++index) {
      const size_t length = strlen(headers[index]);
      if (length >= MAX_HEADER || strchr(headers[index], '\r') || strchr(headers[index], '\n')) {
        ctx->unsupported = true;
        break;
      }
      memcpy(ctx->headers[index], headers[index], length + 1);
      ctx->header_list[index] = ctx->headers[index];
    }
    if (index == MAX_HEADERS && headers[index]) ctx->unsupported = true;
  }
  requests[slot] = ctx;
  return ctx;
}
static bool start(void* opaque) {
  struct native_fetch* ctx = opaque;
  if (active) return false;
  if (!ctx->unsupported) {
    char* cookie = urldb_get_cookie(ctx->url, true);
    const bool started = io->start(nsurl_access(ctx->url), ctx->header_list, cookie);
    engine_memory_free(cookie);
    if (!started) return false;
  }
  active = ctx;
  return true;
}
static void abort_fetch(void* opaque) {
  struct native_fetch* ctx = opaque;
  ctx->aborted = true;
  if (ctx == active && !ctx->unsupported) io->cancel();
}
static void free_fetch(void* opaque) {
  struct native_fetch* ctx = opaque;
  if (ctx == active) {
    if (!ctx->unsupported) io->stop();
    active = NULL;
  }
  for (unsigned i = 0; i < MAX_REQUESTS; ++i)
    if (requests[i] == ctx) requests[i] = NULL;
  nsurl_unref(ctx->url);
  engine_memory_free(ctx);
}
static void send_error(struct native_fetch* ctx, const char* message) {
  fetch_msg msg = {.type = FETCH_ERROR};
  msg.data.error = message;
  fetch_send_callback(&msg, ctx->parent);
}
static const char* error_text(enum http_transport_result result) {
  switch (result) {
    case HTTP_TRANSPORT_MEMORY:
      return "Not enough memory for this connection";
    case HTTP_TRANSPORT_TOO_LARGE:
      return "This response is too large for the reader";
    case HTTP_TRANSPORT_ENCODING:
      return "This response encoding is not supported";
    case HTTP_TRANSPORT_CLOCK:
      return "Set the reader clock before using HTTPS";
    case HTTP_TRANSPORT_BAD_REQUEST:
      return "This request is not supported";
    case HTTP_TRANSPORT_CANCELLED:
      return "Request cancelled";
    default:
      return "The reader could not complete this connection";
  }
}
static void poll_fetch(lwc_string* scheme) {
  (void)scheme;
  if (!active || polling) return;
  polling = true;
  struct native_fetch* ctx = active;
  bool finished = ctx->unsupported;
  if (ctx->unsupported && !ctx->aborted) send_error(ctx, "Request exceeds the supported method or header limits");
  // Bound work per pump; the transport applies back-pressure to its worker.
  for (unsigned count = 0; !finished && !ctx->aborted && count < 4 && io->pop(&event_buffer); ++count) {
    struct http_transport_event* e = &event_buffer;
    if (e->kind == HTTP_TRANSPORT_STATUS) {
      ctx->status = e->status;
      fetch_set_http_code(ctx->parent, ctx->status);
    } else if (e->length <= sizeof(e->data)) {
      fetch_msg msg = {.type = e->kind == HTTP_TRANSPORT_HEADER ? FETCH_HEADER : FETCH_DATA};
      msg.data.header_or_data.buf = e->data;
      msg.data.header_or_data.len = e->length;
      if (e->kind == HTTP_TRANSPORT_HEADER && e->length > 10 && !memcmp(e->data, "Location: ", 10)) {
        size_t length = e->length - 10;
        while (length && (e->data[10 + length - 1] == '\r' || e->data[10 + length - 1] == '\n')) --length;
        if (length <= MAX_URL) {
          memcpy(ctx->location, e->data + 10, length);
          ctx->location[length] = 0;
        }
      }
      if (e->kind == HTTP_TRANSPORT_HEADER && e->length > 12 && e->length < sizeof(e->data) &&
          !memcmp(e->data, "Set-Cookie: ", 12)) {
        e->data[e->length] = 0;
        fetch_set_cookie(ctx->parent, (const char*)e->data + 12);
      }
      fetch_send_callback(&msg, ctx->parent);
    } else {
      io->cancel();
      send_error(ctx, "Invalid transport event");
      ctx->aborted = true;
    }
  }
  // done() implies an empty queue as well as a fully unwound worker.
  if (!finished && io->done()) {
    finished = true;
    if (!ctx->aborted) {
      const enum http_transport_result result = io->result();
      if (result != HTTP_TRANSPORT_OK)
        send_error(ctx, error_text(result));
      else if (ctx->status == 304) {
        fetch_msg msg = {.type = FETCH_NOTMODIFIED};
        fetch_send_callback(&msg, ctx->parent);
      } else if (ctx->status >= 300 && ctx->status < 400 && ctx->location[0]) {
        if (!strncmp(nsurl_access(ctx->url), "https://", 8) && !strncmp(ctx->location, "http://", 7)) {
          send_error(ctx, "An HTTPS page cannot redirect to an unencrypted connection");
        } else {
          fetch_msg msg = {.type = FETCH_REDIRECT};
          msg.data.redirect = ctx->location;
          fetch_send_callback(&msg, ctx->parent);
        }
      } else if (ctx->only_2xx && (ctx->status < 200 || ctx->status >= 300)) {
        send_error(ctx, "The server returned an unsuccessful response");
      } else {
        fetch_msg msg = {.type = FETCH_FINISHED};
        fetch_send_callback(&msg, ctx->parent);
      }
    }
  }
  if (ctx->aborted && !ctx->unsupported) {
    // stop joins after cleanup, discards queued data and cannot call NetSurf.
    io->stop();
    finished = true;
  }
  if (finished) {
    fetch_remove_from_queues(ctx->parent);
    fetch_free(ctx->parent);
  }
  polling = false;
}
static void finalize(lwc_string* scheme) { (void)scheme; }
nserror native_fetch_register(const struct http_transport* transport) {
  if (io || !transport || !transport->start || !transport->pop || !transport->done || !transport->result ||
      !transport->cancel || !transport->stop)
    return NSERROR_BAD_PARAMETER;
  const struct fetcher_operation_table ops = {.initialise = initialize,
                                              .acceptable = acceptable,
                                              .setup = setup,
                                              .start = start,
                                              .abort = abort_fetch,
                                              .free = free_fetch,
                                              .poll = poll_fetch,
                                              .finalise = finalize};
  io = transport;
  lwc_string* scheme = lwc_string_ref(corestring_lwc_http);
  nserror result = fetcher_add(scheme, &ops);
  if (result != NSERROR_OK) {
    lwc_string_unref(scheme);
    io = NULL;
    return result;
  }
  scheme = lwc_string_ref(corestring_lwc_https);
  result = fetcher_add(scheme, &ops);
  if (result != NSERROR_OK) lwc_string_unref(scheme);
  return result;
}
void native_fetch_stop(void) {
  if (!io) return;
  // Abort through NetSurf so queued requests follow its normal free path.
  for (unsigned i = 0; i < MAX_REQUESTS; ++i) {
    struct native_fetch* ctx = requests[i];
    if (!ctx) continue;
    fetch_abort(ctx->parent);
    if (ctx != active) {
      fetch_remove_from_queues(ctx->parent);
      fetch_free(ctx->parent);
    }
  }
  if (active) poll_fetch(NULL);
  resource_error = NSERROR_OK;
}
nserror native_fetch_take_error(void) {
  const nserror error = resource_error;
  resource_error = NSERROR_OK;
  return error;
}
