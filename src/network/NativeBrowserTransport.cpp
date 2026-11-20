#include "NativeBrowserTransport.h"
#if CROSSLOOP_NETSURF_PROBE
#include <Arduino.h>
#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <strings.h>
#include <sys/socket.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "WifiPowerSaveGuard.h"

namespace {
constexpr size_t MAX_URL = 1024;
constexpr size_t RESPONSE_LIMIT = 512 * 1024;
constexpr uint32_t DEADLINE_MS = 45000;
constexpr unsigned QUEUE_ITEMS = 2;
struct Headers {
  char location[1000]{};
  char type[128]{};
  char length[32]{};
  char cache[256]{};
  char modified[96]{};
  char etag[256]{};
  char cookies[4][512]{};
  unsigned cookieCount = 0;
  bool encoded = false;
  bool invalid = false;
};
struct Stream {
  char url[MAX_URL + 1]{};
  char requestHeaders[8][256]{};
  char cookie[1024]{};
  unsigned headerCount = 0;
  Headers headers;
  StaticQueue_t queueState;
  alignas(4) uint8_t queueBytes[QUEUE_ITEMS * sizeof(http_transport_event)];
  QueueHandle_t queue = nullptr;
  StaticSemaphore_t socketMutexState;
  SemaphoreHandle_t socketMutex = nullptr;
  int socket = -1;
  TaskHandle_t task = nullptr;
  std::atomic<bool> cancelled{false};
  std::atomic<bool> complete{true};
  http_transport_result outcome = HTTP_TRANSPORT_BAD_REQUEST;
  uint32_t startedAt = 0;
} stream;

void cancel();

bool expired() { return stream.cancelled.load() || millis() - stream.startedAt >= DEADLINE_MS; }
bool emit(const http_transport_event& event) {
  while (!expired()) {
    if (xQueueSend(stream.queue, &event, pdMS_TO_TICKS(20)) == pdTRUE) return true;
  }
  return false;
}
template <size_t N>
void copyHeader(char (&target)[N], const char* value) {
  const size_t length = strlen(value);
  if (length >= N || strchr(value, '\r') || strchr(value, '\n'))
    stream.headers.invalid = true;
  else
    memcpy(target, value, length + 1);
}
esp_err_t onHttpEvent(esp_http_client_event_t* event) {
  if (event->event_id == HTTP_EVENT_ON_CONNECTED) {
    xSemaphoreTake(stream.socketMutex, portMAX_DELAY);
    stream.socket = esp_http_client_get_socket(event->client);
    if (stream.cancelled.load() && stream.socket >= 0) shutdown(stream.socket, SHUT_RDWR);
    xSemaphoreGive(stream.socketMutex);
  }
  if (event->event_id != HTTP_EVENT_ON_HEADER || !event->header_key || !event->header_value) return ESP_OK;
  auto& h = stream.headers;
  if (!strcasecmp(event->header_key, "Location"))
    copyHeader(h.location, event->header_value);
  else if (!strcasecmp(event->header_key, "Content-Type"))
    copyHeader(h.type, event->header_value);
  else if (!strcasecmp(event->header_key, "Content-Length"))
    copyHeader(h.length, event->header_value);
  else if (!strcasecmp(event->header_key, "Cache-Control"))
    copyHeader(h.cache, event->header_value);
  else if (!strcasecmp(event->header_key, "Last-Modified"))
    copyHeader(h.modified, event->header_value);
  else if (!strcasecmp(event->header_key, "ETag"))
    copyHeader(h.etag, event->header_value);
  else if (!strcasecmp(event->header_key, "Set-Cookie")) {
    if (h.cookieCount == 4)
      h.invalid = true;
    else
      copyHeader(h.cookies[h.cookieCount++], event->header_value);
  } else if (!strcasecmp(event->header_key, "Content-Encoding"))
    h.encoded = strcasecmp(event->header_value, "identity") != 0;
  return ESP_OK;
}
bool emitHeader(http_transport_event& event, const char* key, const char* value) {
  if (!*value) return true;
  event = {};
  event.kind = HTTP_TRANSPORT_HEADER;
  const int count = snprintf(reinterpret_cast<char*>(event.data), sizeof(event.data), "%s: %s\r\n", key, value);
  if (count < 0 || static_cast<size_t>(count) >= sizeof(event.data)) return false;
  event.length = static_cast<size_t>(count);
  return emit(event);
}
http_transport_result download() {
  if (WiFi.status() != WL_CONNECTED) return HTTP_TRANSPORT_NETWORK;
  if (!strncmp(stream.url, "https://", 8)) {
    const auto heap = HalMemory::getInternalHeap();
    if (heap.freeBytes < 80 * 1024 || heap.largestBlockBytes < 24 * 1024) return HTTP_TRANSPORT_MEMORY;
    if (time(nullptr) < 1735689600) {
      const char* configuredTimezone = getenv("TZ");
      const std::string timezone = configuredTimezone ? configuredTimezone : "UTC0";
      configTzTime(timezone.c_str(), "pool.ntp.org", "time.nist.gov");
      const uint32_t started = millis();
      while (time(nullptr) < 1735689600 && millis() - started < 5000 && !expired()) delay(50);
      if (expired()) return HTTP_TRANSPORT_CANCELLED;
      if (time(nullptr) < 1735689600) return HTTP_TRANSPORT_CLOCK;
    }
  }
  auto config = makeUniqueNoThrow<esp_http_client_config_t>();
  auto event = makeUniqueNoThrow<http_transport_event>();
  if (!config || !event) return HTTP_TRANSPORT_MEMORY;
  *config = {};
  config->url = stream.url;
  config->method = HTTP_METHOD_GET;
  config->timeout_ms = 3000;
  config->buffer_size = 1024;
  config->buffer_size_tx = 1024;
  config->disable_auto_redirect = true;
  config->crt_bundle_attach = esp_crt_bundle_attach;
  config->event_handler = onHttpEvent;
  const auto client = esp_http_client_init(config.get());
  if (!client) return HTTP_TRANSPORT_MEMORY;
  ScopedCleanup cleanup{[client] {
    // Clear the borrowed descriptor before close can let another task reuse it.
    xSemaphoreTake(stream.socketMutex, portMAX_DELAY);
    stream.socket = -1;
    xSemaphoreGive(stream.socketMutex);
    esp_http_client_cleanup(client);
  }};
  WifiPowerSaveGuard power;
  if (esp_http_client_set_header(client, "User-Agent", "CrossLoop/0.9 NetSurf/3.11") != ESP_OK ||
      esp_http_client_set_header(client, "Accept-Encoding", "identity") != ESP_OK ||
      esp_http_client_set_header(client, "Connection", "close") != ESP_OK)
    return HTTP_TRANSPORT_MEMORY;
  if (stream.cookie[0] && esp_http_client_set_header(client, "Cookie", stream.cookie) != ESP_OK)
    return HTTP_TRANSPORT_MEMORY;
  for (unsigned i = 0; i < stream.headerCount; ++i) {
    char* line = stream.requestHeaders[i];
    char* colon = strchr(line, ':');
    if (!colon) return HTTP_TRANSPORT_BAD_REQUEST;
    *colon = 0;
    const char* value = colon + 1;
    while (*value == ' ' || *value == '\t') ++value;
    // Encoding and connection policy belong to this bounded transport.
    if (strcasecmp(line, "Accept-Encoding") && strcasecmp(line, "Connection") &&
        esp_http_client_set_header(client, line, value) != ESP_OK)
      return HTTP_TRANSPORT_MEMORY;
  }
  if (expired()) return HTTP_TRANSPORT_CANCELLED;
  if (esp_http_client_open(client, 0) != ESP_OK) return HTTP_TRANSPORT_NETWORK;
  const int64_t length = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (length < 0 || status < 100 || stream.headers.invalid) return HTTP_TRANSPORT_NETWORK;
  if (stream.headers.encoded) return HTTP_TRANSPORT_ENCODING;
  if (static_cast<uint64_t>(length) > RESPONSE_LIMIT) return HTTP_TRANSPORT_TOO_LARGE;
  *event = {};
  event->kind = HTTP_TRANSPORT_STATUS;
  event->status = static_cast<unsigned>(status);
  if (!emit(*event)) return HTTP_TRANSPORT_CANCELLED;
  const auto& h = stream.headers;
  if (!emitHeader(*event, "Location", h.location) || !emitHeader(*event, "Content-Type", h.type) ||
      !emitHeader(*event, "Content-Length", h.length) || !emitHeader(*event, "Cache-Control", h.cache) ||
      !emitHeader(*event, "Last-Modified", h.modified) || !emitHeader(*event, "ETag", h.etag))
    return HTTP_TRANSPORT_CANCELLED;
  for (unsigned i = 0; i < h.cookieCount; ++i)
    if (!emitHeader(*event, "Set-Cookie", h.cookies[i])) return HTTP_TRANSPORT_CANCELLED;
  if (status >= 300 && status < 400) return HTTP_TRANSPORT_OK;
  size_t bytes = 0;
  while (!expired()) {
    const int count = esp_http_client_read(client, reinterpret_cast<char*>(event->data), sizeof(event->data));
    if (count < 0) return HTTP_TRANSPORT_NETWORK;
    if (count == 0)
      return esp_http_client_is_complete_data_received(client) ? HTTP_TRANSPORT_OK : HTTP_TRANSPORT_NETWORK;
    if (static_cast<size_t>(count) > RESPONSE_LIMIT - bytes) return HTTP_TRANSPORT_TOO_LARGE;
    bytes += static_cast<size_t>(count);
    event->kind = HTTP_TRANSPORT_DATA;
    event->length = static_cast<size_t>(count);
    if (!emit(*event)) return HTTP_TRANSPORT_CANCELLED;
  }
  return stream.cancelled.load() ? HTTP_TRANSPORT_CANCELLED : HTTP_TRANSPORT_NETWORK;
}
void worker(void*) {
  stream.outcome = download();
  if (stream.cancelled.load()) stream.outcome = HTTP_TRANSPORT_CANCELLED;
  LOG_INF("NSHTTP", "request finished: result=%u stack=%u", static_cast<unsigned>(stream.outcome),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  stream.complete.store(true, std::memory_order_release);
  vTaskSuspend(nullptr);
}
bool start(const char* url, const char** headers, const char* cookie) {
  if (stream.task) return false;
  stream.complete.store(false);
  stream.cancelled.store(false);
  stream.outcome = HTTP_TRANSPORT_BAD_REQUEST;
  stream.headerCount = 0;
  stream.headers = {};
  stream.startedAt = millis();
  if (!stream.socketMutex) stream.socketMutex = xSemaphoreCreateMutexStatic(&stream.socketMutexState);
  if (!stream.socketMutex) {
    stream.outcome = HTTP_TRANSPORT_MEMORY;
    stream.complete.store(true);
    return true;
  }
  if (!stream.queue)
    stream.queue = xQueueCreateStatic(QUEUE_ITEMS, sizeof(http_transport_event), stream.queueBytes, &stream.queueState);
  if (!stream.queue) {
    stream.outcome = HTTP_TRANSPORT_MEMORY;
    stream.complete.store(true);
    return true;
  }
  xQueueReset(stream.queue);
  const size_t length = url ? strlen(url) : 0;
  if (!length || length > MAX_URL || (strncmp(url, "https://", 8) && strncmp(url, "http://", 7))) {
    stream.complete.store(true);
    return true;
  }
  memcpy(stream.url, url, length + 1);
  stream.cookie[0] = 0;
  if (cookie) {
    if (strlen(cookie) >= sizeof(stream.cookie) || strchr(cookie, '\r') || strchr(cookie, '\n')) {
      stream.complete.store(true);
      return true;
    }
    strcpy(stream.cookie, cookie);
  }
  if (headers) {
    while (headers[stream.headerCount]) {
      if (stream.headerCount == 8 || strlen(headers[stream.headerCount]) >= sizeof(stream.requestHeaders[0])) {
        stream.complete.store(true);
        return true;
      }
      strcpy(stream.requestHeaders[stream.headerCount], headers[stream.headerCount]);
      ++stream.headerCount;
    }
  }
  if (xTaskCreate(worker, "native-web", 8192, nullptr, 1, &stream.task) != pdPASS) {
    stream.task = nullptr;
    stream.outcome = HTTP_TRANSPORT_MEMORY;
    stream.complete.store(true);
  }
  return true;
}
bool pop(http_transport_event* event) {
  if (expired()) cancel();
  return stream.queue && xQueueReceive(stream.queue, event, 0) == pdTRUE;
}
bool done() {
  return stream.complete.load(std::memory_order_acquire) &&
         (!stream.queue || uxQueueMessagesWaiting(stream.queue) == 0);
}
http_transport_result result() { return stream.outcome; }
void cancel() {
  stream.cancelled.store(true);
  if (!stream.socketMutex) return;
  xSemaphoreTake(stream.socketMutex, portMAX_DELAY);
  // shutdown wakes a blocked read without freeing the worker's TLS/client state.
  if (stream.socket >= 0) shutdown(stream.socket, SHUT_RDWR);
  xSemaphoreGive(stream.socketMutex);
}
void stop() {
  cancel();
  if (stream.task) {
    while (!stream.complete.load(std::memory_order_acquire)) vTaskDelay(pdMS_TO_TICKS(10));
    vTaskDelete(stream.task);
    stream.task = nullptr;
  }
  if (stream.queue) xQueueReset(stream.queue);
}
}  // namespace

const http_transport nativeBrowserTransport = {start, pop, done, result, cancel, stop};
#endif
