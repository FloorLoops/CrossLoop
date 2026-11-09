#include "LocalReadingStats.h"

#include <Memory.h>
#include <nvs.h>

#include <cstring>
#include <ctime>

namespace crossloop {
namespace {
constexpr size_t BYTES = 84;
void put32(uint8_t* out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}
uint32_t get32(const uint8_t* in) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= uint32_t(in[i]) << (8 * i);
  return value;
}
uint32_t checksum(const uint8_t* bytes) {
  uint32_t result = 2166136261u;
  for (unsigned i = 0; i < BYTES - 4; ++i) result = (result ^ bytes[i]) * 16777619u;
  return result;
}
bool save(const ReadingStatsData& stats) {
  uint8_t bytes[BYTES]{};
  memcpy(bytes, "CLRST01", 8);
  put32(bytes + 8, static_cast<uint32_t>(stats.seconds));
  put32(bytes + 12, static_cast<uint32_t>(stats.seconds >> 32));
  put32(bytes + 16, stats.turns);
  put32(bytes + 20, stats.sessions);
  for (unsigned i = 0; i < 7; ++i) {
    put32(bytes + 24 + 8 * i, stats.days[i].date);
    put32(bytes + 28 + 8 * i, stats.days[i].seconds);
  }
  put32(bytes + 80, checksum(bytes));
  nvs_handle_t handle;
  if (nvs_open("crossloop_stats", NVS_READWRITE, &handle) != ESP_OK) return false;
  ScopedCleanup close{[handle] { nvs_close(handle); }};
  return nvs_set_blob(handle, "reading", bytes, sizeof(bytes)) == ESP_OK && nvs_commit(handle) == ESP_OK;
}
}  // namespace
uint32_t readingDate(int64_t epoch) {
  if (epoch < 1735689600LL || epoch >= 4102444800LL) return 0;
  const time_t value = static_cast<time_t>(epoch);
  tm local{};
  if (!localtime_r(&value, &local)) return 0;
  return (local.tm_year + 1900) * 10000u + (local.tm_mon + 1) * 100u + local.tm_mday;
}
bool loadReadingStats(ReadingStatsData& stats) {
  stats = {};
  nvs_handle_t handle;
  const auto opened = nvs_open("crossloop_stats", NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  ScopedCleanup close{[handle] { nvs_close(handle); }};
  uint8_t bytes[BYTES]{};
  size_t length = sizeof(bytes);
  const auto result = nvs_get_blob(handle, "reading", bytes, &length);
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  if (result != ESP_OK || length != sizeof(bytes) || memcmp(bytes, "CLRST01", 8) ||
      get32(bytes + 80) != checksum(bytes))
    return false;
  stats.seconds = get32(bytes + 8) | (uint64_t(get32(bytes + 12)) << 32);
  stats.turns = get32(bytes + 16);
  stats.sessions = get32(bytes + 20);
  for (unsigned i = 0; i < 7; ++i) {
    stats.days[i] = {get32(bytes + 24 + 8 * i), get32(bytes + 28 + 8 * i)};
    if (stats.days[i].date && (stats.days[i].date < 20250101 || stats.days[i].date > 20991231 ||
                               (i && stats.days[i].date >= stats.days[i - 1].date))) {
      stats = {};
      return false;
    }
  }
  return true;
}
bool recordReadingSession(uint32_t seconds, uint32_t turns, int64_t epoch) {
  if (!seconds) return true;
  ReadingStatsData stats;
  if (!loadReadingStats(stats)) return false;
  stats.add(seconds, turns, readingDate(epoch));
  return save(stats);
}
bool resetReadingStats() { return save(ReadingStatsData{}); }
}  // namespace crossloop
