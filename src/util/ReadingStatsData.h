#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace crossloop {
struct ReadingDay {
  uint32_t date = 0;
  uint32_t seconds = 0;
};
struct ReadingStatsData {
  uint64_t seconds = 0;
  uint32_t turns = 0;
  uint32_t sessions = 0;
  ReadingDay days[7]{};

  static uint32_t saturatedAdd(uint32_t a, uint32_t b) { return b > UINT32_MAX - a ? UINT32_MAX : a + b; }
  void add(uint32_t duration, uint32_t forwardTurns, uint32_t date) {
    if (!duration) return;
    seconds = duration > UINT64_MAX - seconds ? UINT64_MAX : seconds + duration;
    turns = saturatedAdd(turns, forwardTurns);
    sessions = saturatedAdd(sessions, 1);
    if (!date) return;  // Clockless reading still contributes to lifetime totals.
    for (auto& day : days) {
      if (day.date == date) {
        day.seconds = saturatedAdd(day.seconds, duration);
        return;
      }
    }
    for (unsigned i = 0; i < 7; ++i) {
      if (date <= days[i].date) continue;
      for (unsigned j = 6; j > i; --j) days[j] = days[j - 1];
      days[i] = {date, duration};
      return;
    }
  }
  uint32_t secondsOn(uint32_t date) const {
    for (const auto& day : days)
      if (day.date == date) return day.seconds;
    return 0;
  }
};
}  // namespace crossloop
