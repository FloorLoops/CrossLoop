#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

// One bounded download per owner. The worker never touches UI state. Its
// completed result is published with release/acquire; join before destruction.
class VerifiedDownload final {
 public:
  enum class Result : uint8_t { Pending, Ok, Cancelled, BadUrl, Clock, Memory, Network, Http, TooLarge, StorageError };
  enum class Method : uint8_t { Get, PostEmpty };
  VerifiedDownload() = default;
  ~VerifiedDownload() { stop(); }
  VerifiedDownload(const VerifiedDownload&) = delete;
  VerifiedDownload& operator=(const VerifiedDownload&) = delete;

  bool start(std::string url, std::string temporaryPath, size_t maxBytes, std::string bearerToken = {},
             Method method = Method::Get);
  void cancel() { cancelled.store(true); }
  void stop();
  bool done() const { return completed.load(std::memory_order_acquire); }
  bool running() const { return task != nullptr && !done(); }
  Result result() const { return done() ? outcome : Result::Pending; }
  const char* contentType() const { return done() ? mediaType : ""; }
  const std::string& finalUrl() const { return url; }  // only read after done()
  int httpStatus() const { return done() ? status : 0; }

 private:
  static void worker(void* context);
  Result download();
  bool expired() const;

  TaskHandle_t task = nullptr;
  std::atomic<bool> cancelled{false};
  std::atomic<bool> completed{false};
  Result outcome = Result::Pending;
  std::string url;
  std::string path;
  std::string token;
  Method method = Method::Get;
  size_t limit = 0;
  uint32_t startedAt = 0;
  int status = 0;
  char mediaType[96]{};
};
