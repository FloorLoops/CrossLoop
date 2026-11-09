#pragma once

#include "ReadingStatsData.h"

namespace crossloop {
bool loadReadingStats(ReadingStatsData& stats);
bool recordReadingSession(uint32_t seconds, uint32_t turns, int64_t epoch);
bool resetReadingStats();
uint32_t readingDate(int64_t epoch);
}  // namespace crossloop
