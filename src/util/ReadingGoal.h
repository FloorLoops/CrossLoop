#pragma once
#include <cstdint>

namespace crossloop {
constexpr uint16_t DEFAULT_READING_GOAL = 20;
bool loadReadingGoal(uint16_t& minutes);
bool saveReadingGoal(uint16_t minutes);
constexpr unsigned readingGoalPercent(uint32_t seconds, uint16_t minutes) {
  if (!minutes) return 0;
  const uint32_t target = uint32_t(minutes) * 60;
  return seconds >= target ? 100 : static_cast<unsigned>((uint64_t(seconds) * 100) / target);
}
}  // namespace crossloop
