#include "CalendarZone.h"

#include <algorithm>
#include <limits>

namespace crossloop::calendar {
namespace {
struct Choice {
  int64_t target;
  bool wall;
  const Observance* observance = nullptr;
  int64_t latest = std::numeric_limits<int64_t>::min();
  int64_t earliest = std::numeric_limits<int64_t>::max();
  int32_t offset = 0, fallback = 0;
};
bool consider(void* user, const Date& date) {
  auto& choice = *static_cast<Choice*>(user);
  const auto& transition = *choice.observance;
  const auto local = civilSeconds(date);
  const auto instant = local - transition.offsetFrom;
  const auto key = choice.wall ? local : instant;
  if (instant < choice.earliest) {
    choice.earliest = instant;
    choice.fallback = transition.offsetFrom;
  }
  if (key <= choice.target && key > choice.latest) {
    choice.latest = key;
    choice.offset = transition.offsetTo;
    const auto gap = transition.offsetTo - transition.offsetFrom;
    if (choice.wall && gap > 0 && choice.target < local + gap) choice.offset = transition.offsetFrom;
  }
  return true;
}
bool offsetAt(const Zone& zone, int64_t instant, bool wall, int32_t& result) {
  const Date target = dateFromSeconds(instant);
  if (!zone.count || zone.count > 8 || !valid(target)) return false;
  Choice choice{instant, wall};
  for (unsigned i = 0; i < zone.count; ++i) {
    const auto& ob = zone.observances[i];
    if (!valid(ob.start) || ob.start.allDay || ob.start.utc || ob.offsetFrom <= -86400 || ob.offsetFrom >= 86400 ||
        ob.offsetTo <= -86400 || ob.offsetTo >= 86400 || ob.recurrence.count || ob.recurrence.interval != 1 ||
        (ob.recurrence.frequency != Frequency::None && ob.recurrence.frequency != Frequency::Yearly))
      return false;
    choice.observance = &ob;
    consider(&choice, ob.start);
    if (ob.recurrence.frequency == Frequency::None) continue;
    Rule rule = ob.recurrence;
    if (rule.until.year && rule.until.utc) {
      rule.until = dateFromSeconds(civilSeconds(rule.until) + ob.offsetFrom);
      if (!valid(rule.until)) return false;
    }
    // Only the two most recent recurrence years can determine the current
    // offset. For an expired historical rule, inspect its final two years.
    int lastYear = target.year + 1;
    if (rule.until.year) lastYear = std::min(lastYear, int(rule.until.year));
    const int firstYear = std::max(int(ob.start.year), lastYear - 2);
    if (firstYear > lastYear) continue;
    Date from, through;
    from.year = static_cast<int16_t>(firstYear);
    from.month = 1;
    from.day = 1;
    through.year = static_cast<int16_t>(std::min(lastYear, 2199));
    through.month = 12;
    through.day = 31;
    through.hour = 23;
    through.minute = 59;
    through.second = 59;
    if (!expand(ob.start, rule, from, through, consider, &choice)) return false;
  }
  result = choice.latest == std::numeric_limits<int64_t>::min() ? choice.fallback : choice.offset;
  return true;
}
}  // namespace
bool parseOffset(std::string_view value, int32_t& seconds) {
  if ((value.size() != 5 && value.size() != 7) || (value[0] != '+' && value[0] != '-')) return false;
  int numbers[3]{};
  for (unsigned i = 1; i < value.size(); ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    numbers[(i - 1) / 2] = numbers[(i - 1) / 2] * 10 + value[i] - '0';
  }
  if (numbers[0] > 23 || numbers[1] > 59 || numbers[2] > 59) return false;
  const int offset = numbers[0] * 3600 + numbers[1] * 60 + numbers[2];
  if (!offset && value[0] == '-') return false;
  seconds = value[0] == '-' ? -offset : offset;
  return true;
}
bool toUtc(const Zone& zone, const Date& wall, int64_t& utc) {
  if (!valid(wall) || wall.utc || wall.allDay) return false;
  int32_t offset = 0;
  if (!offsetAt(zone, civilSeconds(wall), true, offset)) return false;
  utc = civilSeconds(wall) - offset;
  return valid(dateFromSeconds(utc));
}
bool fromUtc(const Zone& zone, int64_t utc, Date& wall) {
  int32_t offset = 0;
  if (!offsetAt(zone, utc, false, offset)) return false;
  const auto converted = dateFromSeconds(utc + offset);
  if (!valid(converted)) return false;
  wall = converted;
  return true;
}
}  // namespace crossloop::calendar
