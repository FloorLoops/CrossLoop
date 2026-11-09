#include "CrossLoopPreferences.h"

#include <HalMemory.h>
#include <Memory.h>
#include <nvs.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "network/WebUrl.h"

namespace crossloop::preferences {
namespace {
constexpr char NAMESPACE[] = "crossloop";
const char* name(Key key) {
  switch (key) {
    case Key::TickTickToken:
      return "tick_token";
    case Key::TickTickProjects:
      return "tick_projects";
    case Key::FeedUrls:
      return "feeds";
    case Key::CalendarUrl:
      return "calendar";
    case Key::Latitude:
      return "latitude";
    case Key::Longitude:
      return "longitude";
    case Key::LocationName:
      return "place";
    case Key::Fahrenheit:
      return "fahrenheit";
  }
  return nullptr;
}
bool coordinate(const std::string& value, double limit) {
  char* end = nullptr;
  const double number = strtod(value.c_str(), &end);
  return end != value.c_str() && *end == 0 && std::isfinite(number) && std::abs(number) <= limit;
}
esp_err_t getValue(nvs_handle_t handle, Key key, char* value, size_t* length) {
  // Four full URLs exceed NVS's 4000-byte single-string limit. A blob can span
  // pages; it still includes a terminating NUL and uses the same validation.
  return key == Key::FeedUrls ? nvs_get_blob(handle, name(key), value, length)
                              : nvs_get_str(handle, name(key), value, length);
}
}  // namespace
size_t maxLength(Key key) {
  switch (key) {
    case Key::TickTickToken:
      return 2048;
    case Key::TickTickProjects:
      return 1031;
    case Key::FeedUrls:
      return 4099;
    case Key::CalendarUrl:
      return 1024;
    case Key::Latitude:
    case Key::Longitude:
      return 16;
    case Key::LocationName:
      return 64;
    case Key::Fahrenheit:
      return 1;
  }
  return 0;
}
bool validValue(Key key, const std::string& value) {
  if (value.size() > maxLength(key) || value.find('\0') != std::string::npos) return false;
  if (value.empty()) return true;
  switch (key) {
    case Key::TickTickToken:
      return std::all_of(value.begin(), value.end(), [](unsigned char c) { return c > 32 && c < 127; });
    case Key::TickTickProjects: {
      size_t length = 0, count = 1;
      for (unsigned char c : value) {
        if (c == ',') {
          if (!length || ++count > 8) return false;
          length = 0;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
          if (++length > 128) return false;
        } else
          return false;
      }
      return length > 0;
    }
    case Key::FeedUrls: {
      size_t at = 0, count = 0;
      while (at < value.size()) {
        const auto end = value.find('\n', at);
        if (++count > 4 || !web::validUrl(value.substr(at, end == std::string::npos ? end : end - at))) return false;
        if (end == std::string::npos) return true;
        at = end + 1;
      }
      return false;
    }
    case Key::CalendarUrl:
      return web::validUrl(value, true);
    case Key::Latitude:
      return coordinate(value, 90);
    case Key::Longitude:
      return coordinate(value, 180);
    case Key::LocationName:
      return std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
    case Key::Fahrenheit:
      return value == "0" || value == "1";
  }
  return false;
}
bool read(Key key, std::string& value) {
  value.clear();
  if (!name(key)) return false;
  nvs_handle_t handle;
  const auto opened = nvs_open(NAMESPACE, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  ScopedCleanup close{[handle] { nvs_close(handle); }};
  size_t length = 0;
  const auto status = getValue(handle, key, nullptr, &length);
  if (status == ESP_ERR_NVS_NOT_FOUND) return true;
  if (status != ESP_OK || !length || length > maxLength(key) + 1) return false;
  const auto heap = HalMemory::getInternalHeap();
  if (heap.freeBytes < 2 * length + 8192 || heap.largestBlockBytes < length) return false;
  auto buffer = makeUniqueNoThrow<char[]>(length);
  if (!buffer || getValue(handle, key, buffer.get(), &length) != ESP_OK || !length || buffer[length - 1]) return false;
  value.assign(buffer.get(), length - 1);
  memset(buffer.get(), 0, length);
  if (validValue(key, value)) return true;
  std::fill(value.begin(), value.end(), '\0');
  value.clear();
  return false;
}
bool erase(Key key) {
  if (!name(key)) return false;
  nvs_handle_t handle;
  const auto opened = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
  if (opened != ESP_OK) return false;
  ScopedCleanup close{[handle] { nvs_close(handle); }};
  const auto result = nvs_erase_key(handle, name(key));
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  return result == ESP_OK && nvs_commit(handle) == ESP_OK;
}
bool write(Key key, const std::string& value) {
  if (!validValue(key, value) || !name(key)) return false;
  if (value.empty()) return erase(key);
  // Setup is a cold path; reuse read's bounded allocation and avoid flash writes
  // when a user saves the value already stored. Clear the temporary secret.
  std::string previous;
  const bool unchanged = read(key, previous) && previous == value;
  std::fill(previous.begin(), previous.end(), '\0');
  if (unchanged) return true;
  nvs_handle_t handle;
  if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  ScopedCleanup close{[handle] { nvs_close(handle); }};
  const auto written = key == Key::FeedUrls ? nvs_set_blob(handle, name(key), value.c_str(), value.size() + 1)
                                            : nvs_set_str(handle, name(key), value.c_str());
  return written == ESP_OK && nvs_commit(handle) == ESP_OK;
}
}  // namespace crossloop::preferences
