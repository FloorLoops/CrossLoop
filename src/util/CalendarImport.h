#pragma once

#include <cstdint>

#include "PersonalCache.h"

namespace crossloop::calendar {
enum class ImportResult { Ok, Invalid, Unsupported, TooLarge, Memory, StorageError, Clock };
// Read-only subscription import. The previous agenda remains published unless
// all timezone, override and event passes succeed. Window is at most 31 days.
ImportResult importCalendar(const char* path, int64_t fromUtc, int64_t throughUtc);
// Decode a cached event into the reader's current local day / UTC interval.
// All-day dates keep their civil meaning if the reader's timezone changes.
bool eventSpan(const personal::Item& item, int64_t& start, int64_t& end);
}  // namespace crossloop::calendar
