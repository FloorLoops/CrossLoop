#include <iostream>
#include <string>
#include <vector>

#include "util/CalendarDates.h"
#include "util/CalendarZone.h"
#include "util/DailyCardPolicy.h"
#include "util/IcalReader.h"

int checks = 0, failures = 0;
#define CHECK(x)                             \
  do {                                       \
    ++checks;                                \
    if (!(x)) {                              \
      ++failures;                            \
      std::cerr << __LINE__ << ": " #x "\n"; \
    }                                        \
  } while (0)
using namespace crossloop::calendar;
Date date(const char* text) {
  Date result;
  CHECK(parseDate(text, result));
  return result;
}
std::vector<std::string> occurrences(const char* start, const char* rule, const char* from, const char* through) {
  Rule parsed;
  CHECK(parseRule(rule, parsed) == ParseResult::Ok);
  std::vector<std::string> result;
  CHECK(expand(
      date(start), parsed, date(from), date(through),
      [](void* user, const Date& d) {
        char formatted[32];
        snprintf(formatted, sizeof(formatted), "%04d%02u%02uT%02u%02u%02u", d.year, d.month, d.day, d.hour, d.minute,
                 d.second);
        static_cast<std::vector<std::string>*>(user)->emplace_back(formatted);
        return true;
      },
      &result));
  return result;
}
int main() {
  using crossloop::personal::dailyCardIndex;
  CHECK(dailyCardIndex(1970, 1, 1, 32) == 0);
  CHECK(dailyCardIndex(2100, 1, 1, 32) == 0);
  CHECK(dailyCardIndex(2026, 2, 29, 32) == 0);
  CHECK(dailyCardIndex(2026, 1, 1, 0) == 0);
  for (const auto first : {"20280228", "20280229", "20260308", "20261101", "20261231"}) {
    const Date current = date(first);
    const Date next = dateFromSeconds(civilSeconds(current) + 86400);
    CHECK(dailyCardIndex(next.year, next.month, next.day, 32) ==
          (dailyCardIndex(current.year, current.month, current.day, 32) + 1) % 32);
  }
  CHECK(dayNumber(date("19700101")) == 0);
  CHECK(dayNumber(date("19000101")) == -25567);
  CHECK(dayNumber(date("21991231")) == 84005);
  CHECK(weekday(date("20261108")) == 0);
  CHECK(weekday(date("19700101")) == 4);
  for (int day = -25567; day <= 84005; ++day) {
    const auto d = dateFromSeconds(int64_t(day) * 86400 + 86399);
    CHECK(valid(d) && dayNumber(d) == day && d.hour == 23 && d.minute == 59 && d.second == 59);
  }
  Date invalid;
  for (const auto* text : {"20260229", "19000229", "21000229", "20261131", "20261301", "20260001", "20260100",
                           "20260101T240000", "20260101T120060", "20260101T126000", "20260101T000000X",
                           "20260101T000000ZZ", "2026-01-01", "+0260101", "20261108T1230", "20260101Z"})
    CHECK(!parseDate(text, invalid));
  CHECK(parseDate("20000229", invalid));
  CHECK(parseDate("20261108t123000z", invalid) && invalid.utc);
  CHECK((occurrences("19970902T090000", "FREQ=DAILY;COUNT=4", "19970901T000000", "19970920T235959") ==
         std::vector<std::string>{"19970902T090000", "19970903T090000", "19970904T090000", "19970905T090000"}));
  CHECK((occurrences("19970902T090000", "FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,TH", "19970901T000000",
                     "19970930T235959") ==
         std::vector<std::string>{"19970902T090000", "19970904T090000", "19970916T090000", "19970918T090000"}));
  CHECK((occurrences("20260131T090000", "FREQ=MONTHLY;COUNT=3", "20260101T000000", "20260731T235959") ==
         std::vector<std::string>{"20260131T090000", "20260331T090000", "20260531T090000"}));
  CHECK((occurrences("20260227T090000", "FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1;COUNT=3", "20260201T000000",
                     "20260531T235959") ==
         std::vector<std::string>{"20260227T090000", "20260331T090000", "20260430T090000"}));
  CHECK((occurrences("20261108T090000", "FREQ=MONTHLY;BYDAY=2SU;COUNT=3", "20261101T000000", "20270201T000000") ==
         std::vector<std::string>{"20261108T090000", "20261213T090000", "20270110T090000"}));
  CHECK((occurrences("20240229T090000", "FREQ=YEARLY;COUNT=3", "20240101T000000", "20321231T235959") ==
         std::vector<std::string>{"20240229T090000", "20280229T090000", "20320229T090000"}));
  CHECK((occurrences("20260120T090000", "FREQ=YEARLY;BYMONTHDAY=20;COUNT=3", "20260101T000000", "20261231T235959") ==
         std::vector<std::string>{"20260120T090000", "20260220T090000", "20260320T090000"}));
  CHECK((occurrences("20260101T090000", "FREQ=DAILY;UNTIL=20260103T090000", "20260101T000000", "20261231T235959") ==
         std::vector<std::string>{"20260101T090000", "20260102T090000", "20260103T090000"}));
  CHECK((occurrences("20260101T090000", "freq=daily;interval=2;count=3", "20260103T000000", "20261231T235959") ==
         std::vector<std::string>{"20260103T090000", "20260105T090000"}));
  CHECK((occurrences("20261108", "FREQ=DAILY;COUNT=2", "20261108", "20261110") ==
         std::vector<std::string>{"20261108T000000", "20261109T000000"}));
  Rule rule;
  for (const auto* text : {"", "FREQ=DAILY;FREQ=WEEKLY", "FREQ=DAILY;COUNT=0", "FREQ=DAILY;INTERVAL=0", "COUNT=2",
                           "FREQ=DAILY;COUNT=2;UNTIL=20261201", "FREQ=MONTHLY;BYMONTHDAY=0", "FREQ=MONTHLY;BYDAY=0MO",
                           "FREQ=WEEKLY;BYDAY=2MO", "FREQ=WEEKLY;BYMONTHDAY=2", "FREQ=DAILY;", "FREQ=MONTHLY;BYDAY=MO,",
                           "FREQ=YEARLY;BYMONTH=1,1", "FREQ=MONTHLY;BYMONTHDAY=1,1", "FREQ=WEEKLY;BYDAY=MO,MO"})
    CHECK(parseRule(text, rule) == ParseResult::Invalid);
  for (const auto* text : {"FREQ=HOURLY", "FREQ=YEARLY;BYYEARDAY=3", "FREQ=YEARLY;BYDAY=2MO",
                           "FREQ=MONTHLY;BYDAY=MO;BYSETPOS=1,-1", "FREQ=WEEKLY;BYDAY=MO;BYSETPOS=1"})
    CHECK(parseRule(text, rule) == ParseResult::Unsupported);
  CHECK(parseRule("FREQ=DAILY;COUNT=999999", rule) == ParseResult::Ok);
  CHECK(!expand(
      date("19000101"), rule, date("21990101"), date("21991231"), [](void*, const Date&) { return true; }, nullptr));
  CHECK(parseRule("FREQ=DAILY;COUNT=3", rule) == ParseResult::Ok);
  CHECK(!expand(
      date("20261108"), rule, date("20261108"), date("20261130"), [](void*, const Date&) { return false; }, nullptr));
  Zone zone;
  zone.count = 2;
  zone.observances[0].start = date("19700308T020000");
  zone.observances[0].offsetFrom = -5 * 3600;
  zone.observances[0].offsetTo = -4 * 3600;
  CHECK(parseRule("FREQ=YEARLY;BYMONTH=3;BYDAY=2SU", zone.observances[0].recurrence) == ParseResult::Ok);
  zone.observances[1].start = date("19701101T020000");
  zone.observances[1].offsetFrom = -4 * 3600;
  zone.observances[1].offsetTo = -5 * 3600;
  CHECK(parseRule("FREQ=YEARLY;BYMONTH=11;BYDAY=1SU", zone.observances[1].recurrence) == ParseResult::Ok);
  int64_t utc = 0;
  CHECK(toUtc(zone, date("20260115T120000"), utc) && utc == civilSeconds(date("20260115T170000Z")));
  CHECK(toUtc(zone, date("20260715T120000"), utc) && utc == civilSeconds(date("20260715T160000Z")));
  CHECK(toUtc(zone, date("20260308T013000"), utc) && utc == civilSeconds(date("20260308T063000Z")));
  CHECK(toUtc(zone, date("20260308T023000"), utc) &&
        utc == civilSeconds(date("20260308T073000Z")));  // Gap: old offset.
  CHECK(toUtc(zone, date("20260308T033000"), utc) && utc == civilSeconds(date("20260308T073000Z")));
  CHECK(toUtc(zone, date("20261101T013000"), utc) &&
        utc == civilSeconds(date("20261101T053000Z")));  // Fold: first occurrence.
  CHECK(toUtc(zone, date("20261101T023000"), utc) && utc == civilSeconds(date("20261101T073000Z")));
  Date local;
  CHECK(fromUtc(zone, civilSeconds(date("20261101T063000Z")), local) && compare(local, date("20261101T013000")) == 0);
  CHECK(fromUtc(zone, civilSeconds(date("20260308T070000Z")), local) && compare(local, date("20260308T030000")) == 0);
  int32_t offset;
  CHECK(parseOffset("-0530", offset) && offset == -19800);
  CHECK(parseOffset("+054530", offset) && offset == 20730);
  for (const auto* bad : {"0530", "+2400", "+2360", "-0000", "+010060", "+1", "+01:00"})
    CHECK(!parseOffset(bad, offset));
  Zone fixed;
  fixed.count = 1;
  fixed.observances[0].start = date("19000101T000000");
  fixed.observances[0].offsetFrom = fixed.observances[0].offsetTo = 19800;
  CHECK(toUtc(fixed, date("20261108T090000"), utc) && utc == civilSeconds(date("20261108T033000Z")));
  Zone south;
  south.count = 2;
  south.observances[0].start = date("19701004T020000");
  south.observances[0].offsetFrom = 10 * 3600;
  south.observances[0].offsetTo = 11 * 3600;
  CHECK(parseRule("FREQ=YEARLY;BYMONTH=10;BYDAY=1SU", south.observances[0].recurrence) == ParseResult::Ok);
  south.observances[1].start = date("19700405T030000");
  south.observances[1].offsetFrom = 11 * 3600;
  south.observances[1].offsetTo = 10 * 3600;
  CHECK(parseRule("FREQ=YEARLY;BYMONTH=4;BYDAY=1SU", south.observances[1].recurrence) == ParseResult::Ok);
  CHECK(toUtc(south, date("20260101T120000"), utc) && utc == civilSeconds(date("20260101T010000Z")));
  CHECK(toUtc(south, date("20260601T120000"), utc) && utc == civilSeconds(date("20260601T020000Z")));
  CHECK(parseRule("FREQ=YEARLY;BYMONTH=3;BYDAY=2SU;UNTIL=20101231T235959Z", zone.observances[0].recurrence) ==
        ParseResult::Ok);
  CHECK(parseRule("FREQ=YEARLY;BYMONTH=11;BYDAY=1SU;UNTIL=20101231T235959Z", zone.observances[1].recurrence) ==
        ParseResult::Ok);
  CHECK(toUtc(zone, date("20260715T120000"), utc) && utc == civilSeconds(date("20260715T170000Z")));
  zone.observances[0].recurrence.interval = 2;
  CHECK(!toUtc(zone, date("20260715T120000"), utc));
  const auto raw = [](const std::string& value) {
    fake::files["/calendar"] = std::vector<uint8_t>(value.begin(), value.end());
  };
  IcalReader reader;
  raw("\xef\xbb\xbf"
      "BEGIN:VCALENDAR\r\nSUMMARY:hello\r\n world\r\nEND:VCALENDAR\r\n");
  CHECK(reader.open("/calendar"));
  CHECK(reader.next() == LineResult::Line && std::string(reader.text()) == "BEGIN:VCALENDAR");
  CHECK(reader.next() == LineResult::Line && std::string(reader.text()) == "SUMMARY:helloworld");
  CHECK(reader.next() == LineResult::Line && std::string(reader.text()) == "END:VCALENDAR");
  CHECK(reader.next() == LineResult::End);
  reader.close();
  raw("SUMMARY:" + std::string(503, 'a') + "\r\n " + std::string(500, 'b') + "\nEND:VCALENDAR");
  CHECK(reader.open("/calendar"));
  CHECK(reader.next() == LineResult::Line && reader.length() == 1011);
  CHECK(reader.next() == LineResult::Line && std::string(reader.text()) == "END:VCALENDAR");
  CHECK(reader.next() == LineResult::End);
  reader.close();
  raw(std::string(8193, 'a'));
  CHECK(reader.open("/calendar") && reader.next() == LineResult::TooLarge);
  reader.close();
  raw(std::string("SUMMARY:bad\0text", 16));
  CHECK(reader.open("/calendar") && reader.next() == LineResult::Invalid);
  reader.close();
  raw("SUMMARY:bad\ronly CR");
  CHECK(reader.open("/calendar") && reader.next() == LineResult::Invalid);
  reader.close();
  raw(std::string(1025, 'a'));
  fake::shortReadAt = 512;
  CHECK(reader.open("/calendar") && reader.next() == LineResult::StorageError);
  fake::shortReadAt = -1;
  reader.close();
  Property prop;
  std::string_view parameterValue;
  CHECK(property("DTSTART;TZID=America/New_York:20261108T120000", prop));
  CHECK(parameter(prop, "TZID", parameterValue) && parameterValue == "America/New_York");
  CHECK(property("DTSTART;TZID=\"Custom:Zone\";VALUE=DATE-TIME:20261108T120000", prop));
  CHECK(parameter(prop, "TZID", parameterValue) && parameterValue == "Custom:Zone");
  CHECK(parameter(prop, "value", parameterValue) && parameterValue == "DATE-TIME");
  CHECK(property("DTSTART;TZID=A;TZID=B:20261108T120000", prop));
  CHECK(!parameter(prop, "TZID", parameterValue));
  CHECK(!property("DTSTART;TZID=\"open:20261108T120000", prop));
  char small[9]{};
  CHECK(copyText("a\\,b\\;c\\n", small, sizeof(small), false) && std::string(small) == "a,b;c ");
  CHECK(!copyText("a\\q", small, sizeof(small), false));
  CHECK(!copyText("123456789", small, sizeof(small), false));
  CHECK(copyText("1234567\xc3\xa9", small, sizeof(small), true) && std::string(small) == "1234567");
  CHECK(copyText("123456\xe2\x82\xac", small, sizeof(small), true) && std::string(small) == "123456");
  std::cout << checks << " calendar date checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}
