/* Single bounded stream; transport workers must never call the browser core. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "utils/errors.h"
#ifdef __cplusplus
extern "C" {
#endif
enum http_transport_kind { HTTP_TRANSPORT_STATUS, HTTP_TRANSPORT_HEADER, HTTP_TRANSPORT_DATA };
struct http_transport_event {
  enum http_transport_kind kind;
  unsigned status;
  size_t length;
  unsigned char data[1024];
};
enum http_transport_result {
  HTTP_TRANSPORT_OK,
  HTTP_TRANSPORT_CANCELLED,
  HTTP_TRANSPORT_NETWORK,
  HTTP_TRANSPORT_MEMORY,
  HTTP_TRANSPORT_TOO_LARGE,
  HTTP_TRANSPORT_ENCODING,
  HTTP_TRANSPORT_CLOCK,
  HTTP_TRANSPORT_BAD_REQUEST
};
struct http_transport {
  /* start copies inputs before returning. False means another stream is busy. */
  bool (*start)(const char* url, const char** headers, const char* cookie);
  bool (*pop)(struct http_transport_event* event);
  bool (*done)(void);
  enum http_transport_result (*result)(void);
  void (*cancel)(void);
  /* Cancel and join after socket cleanup, then discard any unread events. */
  void (*stop)(void);
};
nserror native_fetch_register(const struct http_transport* transport);
/* Serialized owner only, after closing its browser. Joins active transport. */
void native_fetch_stop(void);
nserror native_fetch_take_error(void);
#ifdef __cplusplus
}
#endif
