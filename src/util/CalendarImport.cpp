#include "CalendarImport.h"

#include <Arduino.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>

#include "CalendarDates.h"
#include "CalendarZone.h"
#include "IcalReader.h"

namespace crossloop::calendar {
namespace {
constexpr unsigned MAX_ZONES = 4, MAX_OVERRIDES = 64, MAX_EXDATES = 128, MAX_RDATES = 64;
constexpr int64_t MAX_DURATION = 366LL * 86400;
enum class Pass { Zones, Overrides, Events };
enum Field : uint32_t {
  Uid = 1,
  Start = 2,
  End = 4,
  Duration = 8,
  Recurrence = 16,
  RecurrenceId = 32,
  Status = 64,
  Sequence = 128,
  Stamp = 256,
  Summary = 512,
  Location = 1024,
  Description = 2048
};
struct ZonedDate {
  Date date;
  char zone[129]{};
};
struct Instant {
  int64_t value = 0;
  bool allDay = false;
};
struct Override {
  char uid[129]{};
  Instant original;
  uint32_t sequence = 0, ordinal = 0;
  int64_t stamp = 0;
};
struct Event {
  personal::Item item;
  ZonedDate start, end, original;
  Rule rule;
  Instant excluded[MAX_EXDATES]{}, added[MAX_RDATES]{};
  int64_t emitted[personal::MAX_RECORDS]{};
  uint32_t fields = 0, sequence = 0;
  int64_t stamp = 0, exactDuration = 0, untilUtc = 0;
  unsigned excludedCount = 0, addedCount = 0, emittedCount = 0;
  unsigned durationDays = 0, durationSeconds = 0;
  bool cancelled = false, registered = false, hasUntilUtc = false, filteringRule = false;
};
struct Context {
  Context(int64_t from, int64_t through) : from(from), through(through) {}
  std::unique_ptr<IcalReader> reader;
  std::unique_ptr<Zone[]> zones;
  std::unique_ptr<Override[]> overrides;
  personal::CacheWriter writer;
  Event event;
  int64_t from, through;
  char calendarName[193]{};
  char components[8][32]{};
  uint64_t masters[personal::MAX_RECORDS]{};
  unsigned depth = 0, zoneCount = 0, overrideCount = 0, masterCount = 0, ordinal = 0, observationFields = 0;
  int zone = -1;
  bool version = false, ended = false;
  Pass pass = Pass::Zones;
  ImportResult error = ImportResult::Invalid;
  bool fail(ImportResult reason) {
    error = reason;
    return false;
  }
  void resetEvent() {
    std::destroy_at(&event);
    std::construct_at(&event);
  }
};
bool number(std::string_view value, uint32_t maximum, uint32_t& result) {
  if (value.empty() || value.size() > 10) return false;
  uint64_t parsed = 0;
  for (char c : value) {
    if (c < '0' || c > '9') return false;
    parsed = parsed * 10 + c - '0';
    if (parsed > maximum) return false;
  }
  result = static_cast<uint32_t>(parsed);
  return true;
}
bool localEpoch(const Date& date, int64_t& epoch) {
  if (!valid(date)) return false;
  tm value{};
  value.tm_year = date.year - 1900;
  value.tm_mon = date.month - 1;
  value.tm_mday = date.day;
  value.tm_hour = date.hour;
  value.tm_min = date.minute;
  value.tm_sec = date.second;
  value.tm_isdst = -1;
  const time_t result = mktime(&value);
  if (result == time_t(-1)) return false;
  epoch = static_cast<int64_t>(result);
  return true;
}
bool localDate(int64_t epoch, Date& date) {
  const time_t instant = static_cast<time_t>(epoch);
  tm value{};
  if (static_cast<int64_t>(instant) != epoch || !localtime_r(&instant, &value)) return false;
  date = {};
  date.year = static_cast<int16_t>(value.tm_year + 1900);
  date.month = static_cast<uint8_t>(value.tm_mon + 1);
  date.day = static_cast<uint8_t>(value.tm_mday);
  date.hour = static_cast<uint8_t>(value.tm_hour);
  date.minute = static_cast<uint8_t>(value.tm_min);
  date.second = static_cast<uint8_t>(value.tm_sec);
  return valid(date);
}
const Zone* findZone(const Context& c, const char* id) {
  for (unsigned i = 0; i < c.zoneCount; ++i)
    if (!strcmp(c.zones[i].id, id)) return &c.zones[i];
  return nullptr;
}
bool instant(Context& c, const ZonedDate& value, Instant& result) {
  result.allDay = value.date.allDay;
  if (value.date.allDay || value.date.utc) {
    result.value = civilSeconds(value.date);
    return true;
  }
  if (!value.zone[0]) return localEpoch(value.date, result.value) || c.fail(ImportResult::Clock);
  const auto* zone = findZone(c, value.zone);
  return (zone && toUtc(*zone, value.date, result.value)) || c.fail(ImportResult::Unsupported);
}
bool comparable(const Instant& value, int64_t& epoch) {
  if (!value.allDay) {
    epoch = value.value;
    return true;
  }
  return localEpoch(dateFromSeconds(value.value), epoch);
}
bool parseZoned(Context& c, const Property& p, ZonedDate& result) {
  std::string_view type, zone;
  if (!parameter(p, "VALUE", type) || !parameter(p, "TZID", zone) || !parseDate(p.value, result.date))
    return c.fail(ImportResult::Invalid);
  if ((!type.empty() && !same(type, result.date.allDay ? "DATE" : "DATE-TIME")) ||
      (!zone.empty() && (result.date.utc || result.date.allDay)) || zone.size() >= sizeof(result.zone))
    return c.fail(ImportResult::Invalid);
  memcpy(result.zone, zone.data(), zone.size());
  result.zone[zone.size()] = 0;
  return true;
}
bool once(Context& c, Field field) {
  if (c.event.fields & field) return c.fail(ImportResult::Invalid);
  c.event.fields |= field;
  return true;
}
bool duration(std::string_view text, unsigned& days, unsigned& seconds) {
  if (text.empty() || (text.front() != 'P' && text.front() != 'p')) return false;
  text.remove_prefix(1);
  days = seconds = 0;
  bool time = false, any = false, timeValue = false;
  unsigned seen = 0;
  while (!text.empty()) {
    if (text.front() == 'T' || text.front() == 't') {
      if (time) return false;
      time = true;
      text.remove_prefix(1);
      continue;
    }
    size_t n = 0;
    while (n < text.size() && text[n] >= '0' && text[n] <= '9') ++n;
    uint32_t value = 0;
    if (!n || n == text.size() || !number(text.substr(0, n), 31622400, value)) return false;
    char unit = text[n];
    if (unit >= 'a' && unit <= 'z') unit = unit - 'a' + 'A';
    unsigned bit = 0;
    uint64_t total = seconds;
    if (!time && unit == 'W') {
      if (seen || n + 1 != text.size()) return false;
      days = value * 7u;
      bit = 1;
    } else if (!time && unit == 'D') {
      days = value;
      bit = 2;
    } else if (time && unit == 'H') {
      total += uint64_t(value) * 3600;
      bit = 4;
    } else if (time && unit == 'M') {
      total += uint64_t(value) * 60;
      bit = 8;
    } else if (time && unit == 'S') {
      total += value;
      bit = 16;
    } else
      return false;
    if ((seen & bit) || (seen && bit < seen) || days > 366 || uint64_t(days) * 86400 + total > MAX_DURATION)
      return false;
    seen |= bit;
    seconds = static_cast<unsigned>(total);
    any = true;
    timeValue |= time;
    text.remove_prefix(n + 1);
  }
  return any && (!time || timeValue);
}
bool readDates(Context& c, const Property& p, bool excluded) {
  auto rest = p.value;
  while (!rest.empty()) {
    const auto comma = rest.find(',');
    Property part = p;
    part.value = rest.substr(0, comma);
    ZonedDate value;
    Instant stamp;
    if (!parseZoned(c, part, value) || !instant(c, value, stamp)) return false;
    auto& count = excluded ? c.event.excludedCount : c.event.addedCount;
    if (count >= (excluded ? MAX_EXDATES : MAX_RDATES)) return c.fail(ImportResult::TooLarge);
    (excluded ? c.event.excluded : c.event.added)[count++] = stamp;
    if (comma == std::string_view::npos) return true;
    rest.remove_prefix(comma + 1);
  }
  return c.fail(ImportResult::Invalid);
}
bool readEventProperty(Context& c, const Property& p) {
  auto& e = c.event;
  if (same(p.name, "UID"))
    return once(c, Uid) && copyText(p.value, e.item.id, sizeof(e.item.id), false) && e.item.id[0];
  if (same(p.name, "SUMMARY")) return once(c, Summary) && copyText(p.value, e.item.title, sizeof(e.item.title), true);
  if (same(p.name, "LOCATION"))
    return once(c, Location) && copyText(p.value, e.item.detail, sizeof(e.item.detail), true);
  if (same(p.name, "DESCRIPTION"))
    return once(c, Description) && copyText(p.value, e.item.target, sizeof(e.item.target), true);
  if (same(p.name, "DTSTART")) return once(c, Start) && parseZoned(c, p, e.start);
  if (same(p.name, "DTEND")) return once(c, End) && parseZoned(c, p, e.end);
  if (same(p.name, "RECURRENCE-ID")) {
    std::string_view range;
    if (!parameter(p, "RANGE", range)) return c.fail(ImportResult::Invalid);
    if (!range.empty()) return c.fail(ImportResult::Unsupported);
    return once(c, RecurrenceId) && parseZoned(c, p, e.original);
  }
  if (same(p.name, "DURATION")) return once(c, Duration) && duration(p.value, e.durationDays, e.durationSeconds);
  if (same(p.name, "RRULE")) {
    if (!once(c, Recurrence)) return false;
    const auto parsed = parseRule(p.value, e.rule);
    return parsed == ParseResult::Ok ||
           c.fail(parsed == ParseResult::Unsupported ? ImportResult::Unsupported : ImportResult::Invalid);
  }
  if (same(p.name, "EXRULE")) return c.fail(ImportResult::Unsupported);
  if (same(p.name, "EXDATE")) return readDates(c, p, true);
  if (same(p.name, "RDATE")) return readDates(c, p, false);
  if (same(p.name, "STATUS")) {
    if (!once(c, Status)) return false;
    e.cancelled = same(p.value, "CANCELLED");
    return e.cancelled || same(p.value, "CONFIRMED") || same(p.value, "TENTATIVE");
  }
  if (same(p.name, "SEQUENCE")) return once(c, Sequence) && number(p.value, 2147483647, e.sequence);
  if (same(p.name, "DTSTAMP")) {
    Date stamp;
    if (!once(c, Stamp) || !parseDate(p.value, stamp) || !stamp.utc) return c.fail(ImportResult::Invalid);
    e.stamp = civilSeconds(stamp);
  }
  return true;
}
bool sameInstant(const Instant& left, const Instant& right) {
  return left.value == right.value && left.allDay == right.allDay;
}
bool addOverride(Context& c) {
  auto& e = c.event;
  if (!(e.fields & RecurrenceId)) return true;
  if (!e.item.id[0] || (!e.cancelled && !(e.fields & Start))) return c.fail(ImportResult::Invalid);
  Instant original, moved;
  if (!instant(c, e.original, original)) return false;
  int64_t originalTime = 0, movedTime = 0;
  if (!comparable(original, originalTime)) return c.fail(ImportResult::Clock);
  movedTime = originalTime;
  if ((e.fields & Start) && (!instant(c, e.start, moved) || !comparable(moved, movedTime))) return false;
  if ((originalTime < c.from - MAX_DURATION || originalTime > c.through) &&
      (movedTime < c.from - MAX_DURATION || movedTime > c.through))
    return true;
  if (!c.overrides) {
    c.overrides = makeUniqueNoThrow<Override[]>(MAX_OVERRIDES);
    if (!c.overrides) return c.fail(ImportResult::Memory);
  }
  unsigned index = 0;
  for (; index < c.overrideCount; ++index) {
    const auto& prior = c.overrides[index];
    if (!strcmp(prior.uid, e.item.id) && sameInstant(prior.original, original)) break;
  }
  if (index == c.overrideCount) {
    if (index == MAX_OVERRIDES) return c.fail(ImportResult::TooLarge);
    ++c.overrideCount;
  } else if (e.sequence < c.overrides[index].sequence ||
             (e.sequence == c.overrides[index].sequence && e.stamp < c.overrides[index].stamp))
    return true;
  auto& target = c.overrides[index];
  snprintf(target.uid, sizeof(target.uid), "%s", e.item.id);
  target.original = original;
  target.sequence = e.sequence;
  target.stamp = e.stamp;
  target.ordinal = c.ordinal;
  return true;
}
bool span(Context& c, const Date& candidate, Instant& start, Instant& end) {
  auto& e = c.event;
  ZonedDate value = e.start;
  value.date = candidate;
  if (!instant(c, value, start)) return false;
  end = start;
  if (e.fields & End)
    end.value += e.exactDuration;
  else if (e.fields & Duration) {
    value.date = dateFromSeconds(civilSeconds(candidate) + int64_t(e.durationDays) * 86400);
    value.date.allDay = candidate.allDay;
    value.date.utc = candidate.utc;
    if (!valid(value.date) || !instant(c, value, end)) return false;
    end.value += e.durationSeconds;
  } else if (candidate.allDay)
    end.value += 86400;
  return true;
}
bool emit(void* user, const Date& candidate) {
  auto& c = *static_cast<Context*>(user);
  auto& e = c.event;
  Instant start, end;
  if (!span(c, candidate, start, end)) return false;
  if (e.filteringRule && e.hasUntilUtc && start.value > e.untilUtc) return true;
  int64_t begin = 0, finish = 0;
  if (!comparable(start, begin) || !comparable(end, finish)) return c.fail(ImportResult::Clock);
  if (begin > c.through || (finish == begin ? begin < c.from : finish <= c.from)) return true;
  for (unsigned i = 0; i < e.excludedCount; ++i)
    if (sameInstant(start, e.excluded[i])) return true;
  if (!(e.fields & RecurrenceId)) {
    for (unsigned i = 0; i < c.overrideCount; ++i)
      if (!strcmp(c.overrides[i].uid, e.item.id) && sameInstant(c.overrides[i].original, start)) return true;
  }
  for (unsigned i = 0; i < e.emittedCount; ++i)
    if (e.emitted[i] == start.value) return true;
  if (c.writer.size() >= personal::MAX_RECORDS || e.emittedCount == personal::MAX_RECORDS)
    return c.fail(ImportResult::TooLarge);
  if (!(e.fields & RecurrenceId) && !e.registered) {
    uint64_t hash = 14695981039346656037ULL;
    for (const char* p = e.item.id; *p; ++p) hash = (hash ^ static_cast<unsigned char>(*p)) * 1099511628211ULL;
    // A collision rejects the import; it never silently hides an event.
    for (unsigned i = 0; i < c.masterCount; ++i)
      if (c.masters[i] == hash) return c.fail(ImportResult::Invalid);
    if (c.masterCount == personal::MAX_RECORDS) return c.fail(ImportResult::TooLarge);
    c.masters[c.masterCount++] = hash;
    e.registered = true;
  }
  e.emitted[e.emittedCount++] = start.value;
  e.item.flags = (start.allDay ? personal::AllDay : 0) |
                 ((e.fields & (Recurrence | RecurrenceId)) || e.addedCount ? personal::Recurring : 0);
  snprintf(e.item.group, sizeof(e.item.group), "%s", e.start.zone);
  if (start.allDay) {
    const auto endDate = dateFromSeconds(end.value);
    snprintf(e.item.when, sizeof(e.item.when), "%04d%02u%02u/%04d%02u%02u", candidate.year, candidate.month,
             candidate.day, endDate.year, endDate.month, endDate.day);
  } else
    snprintf(e.item.when, sizeof(e.item.when), "%lld/%lld", static_cast<long long>(start.value),
             static_cast<long long>(end.value));
  return c.writer.append(e.item) || c.fail(ImportResult::StorageError);
}
bool eventDate(Context& c, int64_t utc, Date& value) {
  const auto& start = c.event.start;
  if (start.date.allDay || start.date.utc) {
    value = dateFromSeconds(utc);
  } else if (!start.zone[0]) {
    if (!localDate(utc, value)) return c.fail(ImportResult::Clock);
  } else {
    const auto* zone = findZone(c, start.zone);
    if (!zone || !fromUtc(*zone, utc, value)) return c.fail(ImportResult::Unsupported);
  }
  value.allDay = start.date.allDay;
  value.utc = start.date.utc;
  return valid(value);
}
bool finishEvent(Context& c) {
  auto& e = c.event;
  if (c.pass == Pass::Overrides) return addOverride(c);
  if (e.cancelled) return true;
  if (!(e.fields & Start) || !(e.fields & Uid) || ((e.fields & End) && (e.fields & Duration)))
    return c.fail(ImportResult::Invalid);
  Instant originalStart;
  if (!instant(c, e.start, originalStart)) return false;
  for (unsigned i = 0; i < e.excludedCount; ++i)
    if (e.excluded[i].allDay != originalStart.allDay) return c.fail(ImportResult::Invalid);
  for (unsigned i = 0; i < e.addedCount; ++i)
    if (e.added[i].allDay != originalStart.allDay) return c.fail(ImportResult::Invalid);
  if (e.fields & End) {
    Instant end;
    if (!instant(c, e.end, end) || end.allDay != originalStart.allDay) return c.fail(ImportResult::Invalid);
    e.exactDuration = end.value - originalStart.value;
    if (e.exactDuration < 0 || e.exactDuration > MAX_DURATION || (end.allDay && !e.exactDuration))
      return c.fail(ImportResult::Invalid);
  }
  if (e.start.date.allDay && (e.fields & Duration) && (!e.durationDays || e.durationSeconds))
    return c.fail(ImportResult::Invalid);
  if (e.fields & RecurrenceId) {
    Instant id;
    if (!instant(c, e.original, id) || (e.fields & Recurrence) || e.addedCount) return c.fail(ImportResult::Invalid);
    for (unsigned i = 0; i < c.overrideCount; ++i) {
      const auto& replacement = c.overrides[i];
      if (replacement.ordinal == c.ordinal && sameInstant(replacement.original, id) &&
          !strcmp(replacement.uid, e.item.id))
        return emit(&c, e.start.date);
    }
    return true;
  }
  if (e.rule.until.year && e.rule.until.utc && !e.start.date.utc) {
    if (e.start.date.allDay) return c.fail(ImportResult::Invalid);
    // Keep an absolute cutoff absolute through a repeated local hour.
    e.untilUtc = civilSeconds(e.rule.until);
    e.hasUntilUtc = true;
    e.rule.until = {};
  }
  Date from, through;
  // Include ongoing events; duration itself is bounded to one year.
  if (!eventDate(c, c.from - MAX_DURATION, from) || !eventDate(c, c.through + 86400, through)) return false;
  e.filteringRule = true;
  if (!expand(e.start.date, e.rule, from, through, emit, &c)) return false;
  e.filteringRule = false;
  for (unsigned i = 0; i < e.addedCount; ++i) {
    Date added;
    if (!eventDate(c, e.added[i].value, added) || !emit(&c, added)) return false;
  }
  return true;
}
bool zoneProperty(Context& c, const Property& p) {
  auto& zone = c.zones[c.zone];
  if (c.depth == 2) {
    if (same(p.name, "TZID")) {
      if (zone.id[0] || !copyText(p.value, zone.id, sizeof(zone.id), false) || !zone.id[0])
        return c.fail(ImportResult::Invalid);
    }
    return true;
  }
  if (c.depth != 3 || (!same(c.components[2], "STANDARD") && !same(c.components[2], "DAYLIGHT"))) return true;
  auto& ob = zone.observances[zone.count];
  unsigned bit = 0;
  bool ok = true;
  if (same(p.name, "DTSTART")) {
    bit = 1;
    ok = parseDate(p.value, ob.start) && !ob.start.utc && !ob.start.allDay;
  } else if (same(p.name, "TZOFFSETFROM")) {
    bit = 2;
    ok = parseOffset(p.value, ob.offsetFrom);
  } else if (same(p.name, "TZOFFSETTO")) {
    bit = 4;
    ok = parseOffset(p.value, ob.offsetTo);
  } else if (same(p.name, "RRULE")) {
    bit = 8;
    const auto result = parseRule(p.value, ob.recurrence);
    if (result == ParseResult::Unsupported) return c.fail(ImportResult::Unsupported);
    ok = result == ParseResult::Ok && ob.recurrence.frequency == Frequency::Yearly && !ob.recurrence.count &&
         ob.recurrence.interval == 1;
    if (!ok) return c.fail(ImportResult::Unsupported);
  } else if (same(p.name, "RDATE"))
    return c.fail(ImportResult::Unsupported);
  if (!ok || (c.observationFields & bit)) return c.fail(ImportResult::Invalid);
  c.observationFields |= bit;
  return true;
}
bool line(Context& c, const Property& p) {
  if (same(p.name, "BEGIN")) {
    if (c.ended || c.depth == 8 || p.value.empty() || p.value.size() >= sizeof(c.components[0]) ||
        !p.parameters.empty())
      return c.fail(ImportResult::Invalid);
    if (!c.depth && !same(p.value, "VCALENDAR")) return c.fail(ImportResult::Invalid);
    if (c.depth && same(p.value, "VCALENDAR")) return c.fail(ImportResult::Invalid);
    if (same(p.value, "VEVENT")) {
      if (c.depth != 1 || ++c.ordinal > 4096) return c.fail(ImportResult::TooLarge);
      c.resetEvent();
    }
    if (c.pass == Pass::Zones && same(p.value, "VTIMEZONE")) {
      if (c.depth != 1 || c.zoneCount == MAX_ZONES) return c.fail(ImportResult::TooLarge);
      c.zone = c.zoneCount++;
    }
    if (c.pass == Pass::Zones && c.depth == 2 && c.zone >= 0 &&
        (same(p.value, "STANDARD") || same(p.value, "DAYLIGHT"))) {
      if (c.zones[c.zone].count == 8) return c.fail(ImportResult::TooLarge);
      c.observationFields = 0;
    }
    memcpy(c.components[c.depth], p.value.data(), p.value.size());
    c.components[c.depth][p.value.size()] = 0;
    ++c.depth;
    return true;
  }
  if (same(p.name, "END")) {
    if (!c.depth || !p.parameters.empty() || !same(p.value, c.components[c.depth - 1]))
      return c.fail(ImportResult::Invalid);
    if (c.depth == 2 && same(p.value, "VEVENT") && c.pass != Pass::Zones && !finishEvent(c)) return false;
    if (c.pass == Pass::Zones && c.depth == 3 && c.zone >= 0 &&
        (same(p.value, "STANDARD") || same(p.value, "DAYLIGHT"))) {
      if ((c.observationFields & 7) != 7) return c.fail(ImportResult::Invalid);
      ++c.zones[c.zone].count;
    }
    if (c.pass == Pass::Zones && c.depth == 2 && same(p.value, "VTIMEZONE")) {
      const auto& zone = c.zones[c.zone];
      if (!zone.id[0] || !zone.count) return c.fail(ImportResult::Invalid);
      for (unsigned i = 0; i < c.zoneCount; ++i)
        if (int(i) != c.zone && !strcmp(c.zones[i].id, zone.id)) return c.fail(ImportResult::Invalid);
      c.zone = -1;
    }
    if (--c.depth == 0) c.ended = true;
    return true;
  }
  if (!c.depth || c.ended) return c.fail(ImportResult::Invalid);
  if (c.depth == 1) {
    if (same(p.name, "VERSION")) {
      if (c.version || p.value != "2.0") return c.fail(ImportResult::Unsupported);
      c.version = true;
    } else if (same(p.name, "CALSCALE") && !same(p.value, "GREGORIAN"))
      return c.fail(ImportResult::Unsupported);
    else if (same(p.name, "METHOD") && !same(p.value, "PUBLISH"))
      return c.fail(ImportResult::Unsupported);
    else if (c.pass == Pass::Zones && (same(p.name, "X-WR-CALNAME") || same(p.name, "NAME")))
      return copyText(p.value, c.calendarName, sizeof(c.calendarName), true);
    return true;
  }
  if (c.pass == Pass::Zones && c.zone >= 0) return zoneProperty(c, p);
  if (c.pass != Pass::Zones && c.depth == 2 && same(c.components[1], "VEVENT")) return readEventProperty(c, p);
  return true;
}
bool readPass(Context& c, const char* path, Pass pass) {
  c.pass = pass;
  c.depth = c.ordinal = 0;
  c.zone = -1;
  c.version = c.ended = false;
  if (!c.reader->open(path)) return c.fail(ImportResult::StorageError);
  unsigned lines = 0;
  while (true) {
    const auto result = c.reader->next();
    if (result == LineResult::End) break;
    if (result != LineResult::Line)
      return c.fail(result == LineResult::TooLarge       ? ImportResult::TooLarge
                    : result == LineResult::StorageError ? ImportResult::StorageError
                                                         : ImportResult::Invalid);
    if (!c.reader->length()) continue;
    Property p;
    if (!property(std::string_view(c.reader->text(), c.reader->length()), p) || !line(c, p)) return false;
    if ((++lines & 31) == 0) delay(1);
  }
  c.reader->close();
  return !c.depth && c.ended && c.version;
}
bool epochPair(std::string_view value, int64_t& first, int64_t& second) {
  const auto slash = value.find('/');
  if (slash == std::string_view::npos) return false;
  uint32_t a = 0, b = 0;
  if (!number(value.substr(0, slash), 4102444799u, a) || !number(value.substr(slash + 1), 4102444799u, b) || b < a)
    return false;
  first = a;
  second = b;
  return true;
}
}  // namespace
ImportResult importCalendar(const char* path, int64_t fromUtc, int64_t throughUtc) {
  if (fromUtc < 1735689600 || throughUtc >= 4102444800LL || throughUtc < fromUtc || throughUtc - fromUtc > 31LL * 86400)
    return ImportResult::Clock;
  auto source = Storage.open(path);
  if (!source || source.isDirectory()) return ImportResult::StorageError;
  if (source.size() > 2 * 1024 * 1024) return ImportResult::TooLarge;
  source.close();
  auto c = makeUniqueNoThrow<Context>(fromUtc, throughUtc);
  if (!c) return ImportResult::Memory;
  c->reader = makeUniqueNoThrow<IcalReader>();
  c->zones = makeUniqueNoThrow<Zone[]>(MAX_ZONES);
  if (!c->reader || !c->zones) return ImportResult::Memory;
  if (!readPass(*c, path, Pass::Zones) || !readPass(*c, path, Pass::Overrides)) return c->error;
  if (!c->writer.begin(personal::Dataset::Calendar)) return ImportResult::StorageError;
  c->resetEvent();
  auto& metadata = c->event.item;
  snprintf(metadata.id, sizeof(metadata.id), "crossloop-calendar-v1");
  snprintf(metadata.title, sizeof(metadata.title), "%s", c->calendarName);
  snprintf(metadata.when, sizeof(metadata.when), "%lld/%lld", static_cast<long long>(fromUtc),
           static_cast<long long>(throughUtc));
  if (!c->writer.append(metadata)) return ImportResult::StorageError;
  if (!readPass(*c, path, Pass::Events)) return c->error;
  return c->writer.commit() ? ImportResult::Ok : ImportResult::StorageError;
}
bool eventSpan(const personal::Item& item, int64_t& start, int64_t& end) {
  if (!(item.flags & personal::AllDay)) return epochPair(item.when, start, end);
  const std::string_view value(item.when);
  if (value.size() != 17 || value[8] != '/') return false;
  Date a, b;
  return parseDate(value.substr(0, 8), a) && parseDate(value.substr(9), b) && localEpoch(a, start) &&
         localEpoch(b, end) && end > start;
}
}  // namespace crossloop::calendar
