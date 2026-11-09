#pragma once

#include <cstdint>
#include <string_view>

namespace crossloop::calendar {
struct Date {
  int16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
  bool allDay = false;
  bool utc = false;
};
bool valid(const Date& date);
int daysInMonth(int year, int month);
bool parseDate(std::string_view text, Date& date);
// Civil arithmetic, without consulting or modifying the process timezone.
int32_t dayNumber(const Date& date);
int64_t civilSeconds(const Date& date);
Date dateFromSeconds(int64_t seconds);
int weekday(const Date& date);  // Sunday = 0
int compare(const Date& left, const Date& right);

enum class Frequency : uint8_t { None, Daily, Weekly, Monthly, Yearly };
struct ByDay {
  int8_t ordinal = 0;
  uint8_t weekday = 0;
};
struct Rule {
  Frequency frequency = Frequency::None;
  uint16_t interval = 1;
  uint32_t count = 0;
  Date until;
  uint16_t months = 0;
  int8_t monthDays[31]{};
  ByDay weekDays[14]{};
  uint8_t monthDayCount = 0, weekDayCount = 0, weekStart = 1;
  int8_t setPosition = 0;
};
enum class ParseResult { Ok, Invalid, Unsupported };
ParseResult parseRule(std::string_view text, Rule& rule);
// Callers resolve UNTIL into the event's civil timezone before expansion.
// COUNT counts recurrence candidates before EXDATE exclusions. The scan is
// bounded even for malicious dates/counts; overflow is an explicit failure.
using Occurrence = bool (*)(void* context, const Date& date);
bool expand(const Date& start, const Rule& rule, const Date& from, const Date& through, Occurrence callback,
            void* context);
}  // namespace crossloop::calendar
