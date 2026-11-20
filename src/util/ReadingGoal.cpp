#include "ReadingGoal.h"

#include <Memory.h>
#include <nvs.h>

namespace crossloop {
bool loadReadingGoal(uint16_t& minutes) {
  minutes = DEFAULT_READING_GOAL;
  nvs_handle_t handle;
  const auto opened = nvs_open("crossloop_stats", NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  ScopedCleanup close{[handle] { nvs_close(handle); }};
  uint8_t bytes[4]{};
  size_t length = sizeof(bytes);
  const auto result = nvs_get_blob(handle, "goal", bytes, &length);
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  if (result != ESP_OK || length != sizeof(bytes) || bytes[0] != 1 || bytes[1] < 5 || bytes[1] > 120 ||
      bytes[2] != static_cast<uint8_t>(bytes[1] ^ 0xa5) || bytes[3])
    return false;
  minutes = bytes[1];
  return true;
}
bool saveReadingGoal(uint16_t minutes) {
  if (minutes < 5 || minutes > 120) return false;
  uint16_t previous;
  if (!loadReadingGoal(previous)) return false;
  if (previous == minutes) return true;
  nvs_handle_t handle;
  if (nvs_open("crossloop_stats", NVS_READWRITE, &handle) != ESP_OK) return false;
  ScopedCleanup close{[handle] { nvs_close(handle); }};
  const uint8_t bytes[4] = {1, static_cast<uint8_t>(minutes), static_cast<uint8_t>(minutes ^ 0xa5), 0};
  return nvs_set_blob(handle, "goal", bytes, sizeof(bytes)) == ESP_OK && nvs_commit(handle) == ESP_OK;
}
}  // namespace crossloop
