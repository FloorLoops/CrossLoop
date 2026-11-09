#pragma once

#include "CalendarDates.h"

namespace crossloop::calendar {
struct Observance {
  Date start;
  Rule recurrence;
  int32_t offsetFrom = 0;
  int32_t offsetTo = 0;
};
struct Zone {
  char id[129]{};
  Observance observances[8]{};
  uint8_t count = 0;
};
bool parseOffset(std::string_view value, int32_t& seconds);
// Uses the subscription's VTIMEZONE, without changing the reader's timezone.
// Folds choose the first occurrence; gaps use the offset before the change,
// following RFC 5545's DATE-TIME interpretation.
bool toUtc(const Zone& zone, const Date& wall, int64_t& utc);
bool fromUtc(const Zone& zone, int64_t utc, Date& wall);
}  // namespace crossloop::calendar
