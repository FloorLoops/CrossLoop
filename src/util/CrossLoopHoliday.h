#pragma once

#include <cstdint>

namespace crossloop {

enum class Holiday : uint8_t { Standard, NewYear, Valentine, StPatrick, Independence, Halloween, Thanksgiving, Winter };

constexpr Holiday holidayForDate(bool enabled, int year, int month, int day, int weekday) {
  if (!enabled || year < 2025 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31 || weekday < 0 ||
      weekday > 6) {
    return Holiday::Standard;
  }
  if (month == 1 && day == 1) return Holiday::NewYear;
  if (month == 2 && day == 14) return Holiday::Valentine;
  if (month == 3 && day == 17) return Holiday::StPatrick;
  if (month == 7 && day == 4) return Holiday::Independence;
  if (month == 10 && day == 31) return Holiday::Halloween;
  if (month == 11 && weekday == 4 && day >= 22 && day <= 28) return Holiday::Thanksgiving;
  if (month == 12 && day >= 24 && day <= 26) return Holiday::Winter;
  return Holiday::Standard;
}

}  // namespace crossloop
