#pragma once

#include <cstddef>

#include "CalendarDates.h"

namespace crossloop::personal {
// Select by civil day, so daylight-saving changes do not repeat or skip a card.
// A missing clock uses the first card and the renderer labels the date unknown.
inline size_t dailyCardIndex(int year, int month, int day, size_t count) {
  if (!count || year < 2025 || year >= 2100 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
  calendar::Date date{static_cast<int16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day)};
  if (!calendar::valid(date)) return 0;
  return static_cast<size_t>(calendar::dayNumber(date)) % count;
}
}  // namespace crossloop::personal
