#include "VerifiedDownload.h"

#include <Arduino.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <strings.h>

#include <cstring>
#include <ctime>

#include "WebUrl.h"
#include "WifiPowerSaveGuard.h"

namespace {
constexpr uint32_t DEADLINE_MS = 45000;
constexpr int SOCKET_TIMEOUT_MS = 3000;
constexpr uint32_t STACK_BYTES = 8192;
struct ResponseHeaders {
  char location[crossloop::web::MAX_URL + 1]{};
  char type[96]{};
  bool encoded = false;
  bool invalid = false;
};

esp_err_t onHttpEvent(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_HEADER) return ESP_OK;
  auto& headers = *static_cast<ResponseHeaders*>(event->user_data);
  if (!event->header_key || !event->header_value) return ESP_OK;
  const size_t length = strlen(event->header_value);
  if (!strcasecmp(event->header_key, "Location")) {
    if (length >= sizeof(headers.location))
      headers.invalid = true;
    else
      memcpy(headers.location, event->header_value, length + 1);
  } else if (!strcasecmp(event->header_key, "Content-Type")) {
    if (length >= sizeof(headers.type))
      headers.invalid = true;
    else
      memcpy(headers.type, event->header_value, length + 1);
  } else if (!strcasecmp(event->header_key, "Content-Encoding")) {
    headers.encoded = strcasecmp(event->header_value, "identity") != 0;
  }
  return ESP_OK;
}
}  // namespace

bool VerifiedDownload::start(std::string requestedUrl, std::string temporaryPath, const size_t maxBytes,
                             std::string bearerToken, Method requestedMethod) {
  if (task) return false;
  url = std::move(requestedUrl);
  path = std::move(temporaryPath);
  token = std::move(bearerToken);
  method = requestedMethod;
  limit = maxBytes;
  cancelled.store(false);
  completed.store(false);
  outcome = Result::Pending;
  mediaType[0] = '\0';
  status = 0;
  startedAt = millis();
  if (xTaskCreate(worker, "crossloop-net", STACK_BYTES, this, 1, &task) != pdPASS) {
    task = nullptr;
    token.clear();
    outcome = Result::Memory;
    completed.store(true, std::memory_order_release);
    return false;
  }
  return true;
}

bool VerifiedDownload::expired() const { return cancelled.load() || millis() - startedAt >= DEADLINE_MS; }

void VerifiedDownload::stop() {
  if (!task) return;
  cancel();
  // All socket operations have a finite timeout. Let their owners unwind;
  // deleting a worker in TLS/SD code would strand locks and file handles.
  while (!done()) vTaskDelay(pdMS_TO_TICKS(10));
  vTaskDelete(task);
  task = nullptr;
}

void VerifiedDownload::worker(void* context) {
  auto* self = static_cast<VerifiedDownload*>(context);
  self->outcome = self->download();
  self->token.clear();
  LOG_INF("CLNET", "request done: result=%u stack=%u", static_cast<unsigned>(self->outcome),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  self->completed.store(true, std::memory_order_release);
  vTaskSuspend(nullptr);
}

VerifiedDownload::Result VerifiedDownload::download() {
  using namespace crossloop::web;
  if (!validUrl(url, !token.empty()) || token.size() > 2048 || token.find_first_of("\r\n") != std::string::npos ||
      path.empty() || !limit || limit > 4 * 1024 * 1024)
    return Result::BadUrl;
  if (WiFi.status() != WL_CONNECTED) return Result::Network;
  auto buffer = makeUniqueNoThrow<uint8_t[]>(1024);
  auto headers = makeUniqueNoThrow<ResponseHeaders>();
  auto config = makeUniqueNoThrow<esp_http_client_config_t>();
  if (!buffer || !headers || !config) return Result::Memory;
  const std::string credentialOrigin = origin(url);
  WifiPowerSaveGuard powerGuard;
  for (unsigned redirect = 0; redirect <= 4 && !expired(); ++redirect) {
    if (url.starts_with("https://")) {
      // Check after the previous client has been destroyed. Counting both TLS
      // clients here would reject otherwise affordable HTTPS redirects.
      const auto heap = HalMemory::getInternalHeap();
      LOG_INF("CLNET", "before TLS: free=%u largest=%u", static_cast<unsigned>(heap.freeBytes),
              static_cast<unsigned>(heap.largestBlockBytes));
      if (heap.freeBytes < 80 * 1024 || heap.largestBlockBytes < 24 * 1024) return Result::Memory;
      if (time(nullptr) < 1735689600) {
        const char* configuredTimezone = getenv("TZ");
        const std::string timezone = configuredTimezone ? configuredTimezone : "UTC0";
        configTzTime(timezone.c_str(), "pool.ntp.org", "time.nist.gov");
        const uint32_t start = millis();
        while (time(nullptr) < 1735689600 && millis() - start < 5000 && !expired()) delay(50);
        if (expired()) return cancelled.load() ? Result::Cancelled : Result::Network;
        if (time(nullptr) < 1735689600) return Result::Clock;
      }
    }
    *headers = {};
    *config = {};
    config->url = url.c_str();
    config->method = method == Method::PostEmpty ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    config->timeout_ms = SOCKET_TIMEOUT_MS;
    config->buffer_size = 1024;
    config->buffer_size_tx = 1024;
    config->disable_auto_redirect = true;
    config->crt_bundle_attach = esp_crt_bundle_attach;
    config->event_handler = onHttpEvent;
    config->user_data = headers.get();
    const auto client = esp_http_client_init(config.get());
    if (!client) return Result::Memory;
    ScopedCleanup cleanup{[client] { esp_http_client_cleanup(client); }};
    esp_http_client_set_header(client, "User-Agent", "CrossLoop/0.9 TextWeb");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Connection", "close");
    if (!token.empty()) {
      const std::string authorization = "Bearer " + token;
      if (esp_http_client_set_header(client, "Authorization", authorization.c_str()) != ESP_OK) return Result::Memory;
    }
    if (esp_http_client_open(client, 0) != ESP_OK) return Result::Network;
    const int64_t length = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    if (headers->invalid || headers->encoded) return Result::Http;
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      // Never replay a task mutation at a redirected URL.
      if (method != Method::Get) return Result::Http;
      const std::string next = resolveUrl(url, headers->location);
      if (!headers->location[0] || next.empty() || (url.starts_with("https://") && !next.starts_with("https://")) ||
          (!token.empty() && origin(next) != credentialOrigin))
        return Result::BadUrl;
      url = next;
      continue;
    }
    const bool accepted = status == 200 || (method == Method::PostEmpty && (status == 201 || status == 204));
    if (!accepted || length < 0) return Result::Http;
    if (static_cast<uint64_t>(length) > limit) return Result::TooLarge;
    HalFile output;
    if (!Storage.openFileForWrite("CLNET", path, output)) return Result::StorageError;
    bool keep = false;
    ScopedCleanup fileCleanup{[&] {
      output.close();
      if (!keep) Storage.remove(path.c_str());
    }};
    size_t bytes = 0;
    while (!expired()) {
      const int count = esp_http_client_read(client, reinterpret_cast<char*>(buffer.get()), 1024);
      if (count < 0) return Result::Network;
      if (count == 0) {
        if (!esp_http_client_is_complete_data_received(client)) return Result::Network;
        if (!output.close()) return Result::StorageError;
        memcpy(mediaType, headers->type, sizeof(mediaType));
        keep = true;
        return Result::Ok;
      }
      if (static_cast<size_t>(count) > limit - bytes) return Result::TooLarge;
      if (output.write(buffer.get(), count) != static_cast<size_t>(count)) return Result::StorageError;
      bytes += count;
      delay(1);
    }
    return cancelled.load() ? Result::Cancelled : Result::Network;
  }
  return cancelled.load() ? Result::Cancelled : Result::Network;
}
