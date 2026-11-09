#include "CalendarDates.h"

#include <algorithm>

namespace crossloop::calendar {
namespace {
bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
int32_t beforeYear(int year) {
  --year;
  return 365 * year + year / 4 - year / 100 + year / 400;
}
bool equal(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i) {
    const auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; };
    if (upper(left[i]) != upper(right[i])) return false;
  }
  return true;
}
bool integer(std::string_view text, int& value, int minimum, int maximum) {
  if (text.empty()) return false;
  int sign = 1;
  if (text.front() == '-' || text.front() == '+') {
    sign = text.front() == '-' ? -1 : 1;
    text.remove_prefix(1);
  }
  if (text.empty() || text.size() > 7) return false;
  value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + c - '0';
  }
  value *= sign;
  return value >= minimum && value <= maximum;
}
bool digits(std::string_view text, int& value) {
  return !text.empty() && text.front() != '+' && text.front() != '-' && integer(text, value, 0, 9999);
}
int dayName(std::string_view value) {
  constexpr std::string_view names[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
  for (int i = 0; i < 7; ++i)
    if (equal(value, names[i])) return i;
  return -1;
}
template <typename Function>
bool list(std::string_view value, Function function) {
  while (!value.empty()) {
    const auto comma = value.find(',');
    if (!function(value.substr(0, comma))) return false;
    if (comma == std::string_view::npos) return true;
    value.remove_prefix(comma + 1);
  }
  return false;
}
bool dayFilter(const Date& current, const Date& start, const Rule& rule) {
  const int days = daysInMonth(current.year, current.month);
  if (rule.monthDayCount) {
    bool found = false;
    for (unsigned i = 0; i < rule.monthDayCount; ++i) {
      const int value = rule.monthDays[i];
      found |= current.day == (value > 0 ? value : days + 1 + value);
    }
    if (!found) return false;
  }
  if (rule.weekDayCount) {
    bool found = false;
    const int wd = weekday(current);
    for (unsigned i = 0; i < rule.weekDayCount; ++i) {
      const auto& by = rule.weekDays[i];
      if (wd != by.weekday) continue;
      const int positive = (current.day - 1) / 7 + 1;
      const int negative = -((days - current.day) / 7 + 1);
      found |= !by.ordinal || by.ordinal == positive || by.ordinal == negative;
    }
    if (!found) return false;
  }
  if (rule.frequency == Frequency::Weekly && !rule.weekDayCount && weekday(current) != weekday(start)) return false;
  if ((rule.frequency == Frequency::Monthly || rule.frequency == Frequency::Yearly) && !rule.weekDayCount &&
      !rule.monthDayCount && current.day != start.day)
    return false;
  return true;
}
bool matches(const Date& current, const Date& start, const Rule& rule) {
  if (rule.months && !(rule.months & (1u << (current.month - 1)))) return false;
  const auto days = dayNumber(current) - dayNumber(start);
  const int months = (current.year - start.year) * 12 + current.month - start.month;
  switch (rule.frequency) {
    case Frequency::Daily:
      if (days % rule.interval) return false;
      break;
    case Frequency::Weekly: {
      const int anchor = dayNumber(start) - (weekday(start) + 7 - rule.weekStart) % 7;
      if (((dayNumber(current) - anchor) / 7) % rule.interval) return false;
      break;
    }
    case Frequency::Monthly:
      if (months % rule.interval) return false;
      break;
    case Frequency::Yearly:
      if ((current.year - start.year) % rule.interval ||
          (!rule.months && !rule.monthDayCount && current.month != start.month))
        return false;
      break;
    default:
      return false;
  }
  if (!dayFilter(current, start, rule)) return false;
  if (rule.setPosition) {
    Date candidate = current;
    int selected = 0, total = 0;
    for (int day = 1; day <= daysInMonth(current.year, current.month); ++day) {
      candidate.day = static_cast<uint8_t>(day);
      if (dayFilter(candidate, start, rule)) {
        ++total;
        if (day == current.day) selected = total;
      }
    }
    if (selected != (rule.setPosition > 0 ? rule.setPosition : total + rule.setPosition + 1)) return false;
  }
  return true;
}
}  // namespace
int daysInMonth(int year, int month) {
  constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month >= 1 && month <= 12 ? days[month - 1] + (month == 2 && leap(year)) : 0;
}
bool valid(const Date& date) {
  return date.year >= 1900 && date.year <= 2199 && date.month >= 1 && date.month <= 12 && date.day >= 1 &&
         date.day <= daysInMonth(date.year, date.month) && date.hour < 24 && date.minute < 60 && date.second < 60;
}
bool parseDate(std::string_view text, Date& result) {
  Date date;
  if (text.size() != 8 && text.size() != 15 && text.size() != 16) return false;
  date.allDay = text.size() == 8;
  date.utc = text.size() == 16;
  if ((!date.allDay && text[8] != 'T' && text[8] != 't') || (date.utc && text[15] != 'Z' && text[15] != 'z'))
    return false;
  int number = 0;
  if (!digits(text.substr(0, 4), number)) return false;
  date.year = static_cast<int16_t>(number);
  if (!digits(text.substr(4, 2), number)) return false;
  date.month = static_cast<uint8_t>(number);
  if (!digits(text.substr(6, 2), number)) return false;
  date.day = static_cast<uint8_t>(number);
  if (!date.allDay) {
    if (!digits(text.substr(9, 2), number)) return false;
    date.hour = static_cast<uint8_t>(number);
    if (!digits(text.substr(11, 2), number)) return false;
    date.minute = static_cast<uint8_t>(number);
    if (!digits(text.substr(13, 2), number)) return false;
    date.second = static_cast<uint8_t>(number);
  }
  if (!valid(date)) return false;
  result = date;
  return true;
}
int32_t dayNumber(const Date& date) {
  int32_t days = beforeYear(date.year) - beforeYear(1970) + date.day - 1;
  for (int month = 1; month < date.month; ++month) days += daysInMonth(date.year, month);
  return days;
}
int64_t civilSeconds(const Date& date) {
  return int64_t(dayNumber(date)) * 86400 + date.hour * 3600 + date.minute * 60 + date.second;
}
Date dateFromSeconds(int64_t seconds) {
  Date date;
  int64_t days = seconds / 86400, remaining = seconds % 86400;
  if (remaining < 0) {
    remaining += 86400;
    --days;
  }
  if (days < -25567 || days > 84005) return date;  // 1900..2199
  int year = 1970 + static_cast<int>(days / 365);
  const auto absolute = days + beforeYear(1970);
  while (beforeYear(year) > absolute) --year;
  while (beforeYear(year + 1) <= absolute) ++year;
  date.year = static_cast<int16_t>(year);
  days = absolute - beforeYear(year);
  date.month = 1;
  while (days >= daysInMonth(year, date.month)) {
    days -= daysInMonth(year, date.month);
    ++date.month;
  }
  date.day = static_cast<uint8_t>(days + 1);
  date.hour = static_cast<uint8_t>(remaining / 3600);
  date.minute = static_cast<uint8_t>((remaining % 3600) / 60);
  date.second = static_cast<uint8_t>(remaining % 60);
  return date;
}
int weekday(const Date& date) {
  const int value = (dayNumber(date) + 4) % 7;
  return value < 0 ? value + 7 : value;
}
int compare(const Date& left, const Date& right) {
  const auto a = civilSeconds(left), b = civilSeconds(right);
  return a < b ? -1 : a > b ? 1 : 0;
}
ParseResult parseRule(std::string_view text, Rule& output) {
  if (text.empty() || text.size() > 512) return ParseResult::Invalid;
  Rule rule;
  unsigned seen = 0;
  while (!text.empty()) {
    const auto semicolon = text.find(';');
    const auto part = text.substr(0, semicolon);
    const auto equals = part.find('=');
    if (!equals || equals == std::string_view::npos || equals + 1 == part.size()) return ParseResult::Invalid;
    const auto name = part.substr(0, equals), value = part.substr(equals + 1);
    int number = 0;
    unsigned bit = 0;
    if (equal(name, "FREQ")) {
      bit = 1;
      if (equal(value, "DAILY"))
        rule.frequency = Frequency::Daily;
      else if (equal(value, "WEEKLY"))
        rule.frequency = Frequency::Weekly;
      else if (equal(value, "MONTHLY"))
        rule.frequency = Frequency::Monthly;
      else if (equal(value, "YEARLY"))
        rule.frequency = Frequency::Yearly;
      else
        return ParseResult::Unsupported;
    } else if (equal(name, "INTERVAL")) {
      bit = 2;
      if (!integer(value, number, 1, 366)) return ParseResult::Invalid;
      rule.interval = static_cast<uint16_t>(number);
    } else if (equal(name, "COUNT")) {
      bit = 4;
      if (!integer(value, number, 1, 1000000)) return ParseResult::Invalid;
      rule.count = static_cast<uint32_t>(number);
    } else if (equal(name, "UNTIL")) {
      bit = 8;
      if (!parseDate(value, rule.until)) return ParseResult::Invalid;
    } else if (equal(name, "WKST")) {
      bit = 16;
      number = dayName(value);
      if (number < 0) return ParseResult::Invalid;
      rule.weekStart = static_cast<uint8_t>(number);
    } else if (equal(name, "BYMONTH")) {
      bit = 32;
      if (!list(value, [&](auto part) {
            if (!integer(part, number, 1, 12) || (rule.months & (1u << (number - 1)))) return false;
            rule.months |= 1u << (number - 1);
            return true;
          }))
        return ParseResult::Invalid;
    } else if (equal(name, "BYMONTHDAY")) {
      bit = 64;
      if (!list(value, [&](auto part) {
            if (rule.monthDayCount == 31 || !integer(part, number, -31, 31) || !number) return false;
            for (unsigned i = 0; i < rule.monthDayCount; ++i)
              if (rule.monthDays[i] == number) return false;
            rule.monthDays[rule.monthDayCount++] = static_cast<int8_t>(number);
            return true;
          }))
        return ParseResult::Invalid;
    } else if (equal(name, "BYDAY")) {
      bit = 128;
      if (!list(value, [&](auto part) {
            if (rule.weekDayCount == 14 || part.size() < 2) return false;
            const int day = dayName(part.substr(part.size() - 2));
            number = 0;
            if (day < 0 || (part.size() > 2 && (!integer(part.substr(0, part.size() - 2), number, -5, 5) || !number)))
              return false;
            for (unsigned i = 0; i < rule.weekDayCount; ++i)
              if (rule.weekDays[i].weekday == day && rule.weekDays[i].ordinal == number) return false;
            rule.weekDays[rule.weekDayCount++] = {static_cast<int8_t>(number), static_cast<uint8_t>(day)};
            return true;
          }))
        return ParseResult::Invalid;
    } else if (equal(name, "BYSETPOS")) {
      bit = 256;
      if (value.find(',') != std::string_view::npos) return ParseResult::Unsupported;
      if (!integer(value, number, -31, 31) || !number) return ParseResult::Invalid;
      rule.setPosition = static_cast<int8_t>(number);
    } else
      return ParseResult::Unsupported;
    if (seen & bit) return ParseResult::Invalid;
    seen |= bit;
    if (semicolon == std::string_view::npos) break;
    text.remove_prefix(semicolon + 1);
    if (text.empty()) return ParseResult::Invalid;
  }
  if (!(seen & 1) || ((seen & 4) && (seen & 8))) return ParseResult::Invalid;
  if (rule.frequency == Frequency::Weekly && rule.monthDayCount) return ParseResult::Invalid;
  for (unsigned i = 0; i < rule.weekDayCount; ++i) {
    if (rule.weekDays[i].ordinal && rule.frequency != Frequency::Monthly && rule.frequency != Frequency::Yearly)
      return ParseResult::Invalid;
  }
  // Year-wide BYDAY ordinals and year-wide BYSETPOS need a different candidate
  // set. Report the unsupported rule instead of showing incorrect occurrences.
  if (rule.frequency == Frequency::Yearly && rule.weekDayCount && !rule.months) return ParseResult::Unsupported;
  if (rule.setPosition && (rule.frequency != Frequency::Monthly || !rule.weekDayCount)) return ParseResult::Unsupported;
  output = rule;
  return ParseResult::Ok;
}
bool expand(const Date& start, const Rule& rule, const Date& from, const Date& through, Occurrence callback,
            void* context) {
  if (!valid(start) || !valid(from) || !valid(through) || compare(from, through) > 0 || !callback || !rule.interval)
    return false;
  if (rule.until.year && (!valid(rule.until) || rule.until.allDay != start.allDay || rule.until.utc != start.utc))
    return false;
  const auto first = dayNumber(start), last = dayNumber(through);
  uint32_t count = 0, scanned = 0;
  const auto begin = rule.count ? first : std::max(first, dayNumber(from));
  for (int32_t day = begin; day <= last; ++day) {
    if (++scanned > 40000) return false;
    Date current = dateFromSeconds(int64_t(day) * 86400 + start.hour * 3600 + start.minute * 60 + start.second);
    current.allDay = start.allDay;
    current.utc = start.utc;
    if (rule.until.year && compare(current, rule.until) > 0) break;
    if (day == first || matches(current, start, rule)) {
      if (rule.count && count >= rule.count) break;
      ++count;
      if (compare(current, from) >= 0 && compare(current, through) <= 0 && !callback(context, current)) return false;
    }
    if (rule.frequency == Frequency::None) break;
  }
  return true;
}
}  // namespace crossloop::calendar
